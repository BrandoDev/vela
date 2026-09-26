#include "windowcapture.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

// ------------------------------------------------------------------ stato --

struct WindowCapture::Window {
    ext_foreign_toplevel_handle_v1* handle = nullptr;
    QString identifier;
    QString title;
    QString appId;
};

// Una cattura in corso: sorgente -> sessione (ci dice dimensione e formati)
// -> frame con un nostro buffer in memoria condivisa -> "ready".
struct WindowCapture::Job {
    WindowCapture* owner = nullptr;
    QString identifier;
    ext_image_capture_source_v1* source = nullptr;
    ext_image_copy_capture_session_v1* session = nullptr;
    ext_image_copy_capture_frame_v1* frame = nullptr;
    wl_shm_pool* pool = nullptr;
    wl_buffer* buffer = nullptr;
    int fd = -1;
    void* pixels = nullptr;
    size_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int format = -1; // formato wl_shm scelto, -1 = nessuno utilizzabile
};

namespace {

// Formati wl_shm che sappiamo leggere, e il QImage corrispondente.
QImage::Format imageFormat(uint32_t shmFormat)
{
    switch (shmFormat) {
    case WL_SHM_FORMAT_XRGB8888: return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB8888: return QImage::Format_ARGB32_Premultiplied;
    case WL_SHM_FORMAT_XBGR8888: return QImage::Format_RGBX8888;
    case WL_SHM_FORMAT_ABGR8888: return QImage::Format_RGBA8888_Premultiplied;
    default: return QImage::Format_Invalid;
    }
}

} // namespace

// I listener C di libwayland: rimandano alla classe.
struct CaptureCallbacks {
    using Window = WindowCapture::Window;
    using Job = WindowCapture::Job;

    // --- registry ---
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
    {
        auto* self = static_cast<WindowCapture*>(data);
        if (!strcmp(interface, wl_shm_interface.name)) {
            self->m_shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        } else if (!strcmp(interface, ext_foreign_toplevel_list_v1_interface.name)) {
            self->m_list = static_cast<ext_foreign_toplevel_list_v1*>(
                wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1));
            ext_foreign_toplevel_list_v1_add_listener(self->m_list, &listListener, self);
        } else if (!strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name)) {
            self->m_sources = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(
                wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
        } else if (!strcmp(interface, ext_image_copy_capture_manager_v1_interface.name)) {
            self->m_copier = static_cast<ext_image_copy_capture_manager_v1*>(
                wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
        }
    }
    static void globalRemove(void*, wl_registry*, uint32_t) { }
    static constexpr wl_registry_listener registryListener { global, globalRemove };

    // --- elenco finestre ---
    static void toplevel(void* data, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* handle)
    {
        auto* self = static_cast<WindowCapture*>(data);
        auto* window = new Window;
        window->handle = handle;
        self->m_windows.insert(handle, window);
        ext_foreign_toplevel_handle_v1_add_listener(handle, &handleListener, self);
    }
    static void finished(void*, ext_foreign_toplevel_list_v1*) { }
    static constexpr ext_foreign_toplevel_list_v1_listener listListener { toplevel, finished };

    static void closed(void* data, ext_foreign_toplevel_handle_v1* handle)
    {
        auto* self = static_cast<WindowCapture*>(data);
        if (Window* window = self->m_windows.take(handle)) {
            self->m_thumbnails.remove(window->identifier);
            delete window;
        }
        ext_foreign_toplevel_handle_v1_destroy(handle);
    }
    static void done(void*, ext_foreign_toplevel_handle_v1*) { }
    static void title(void* data, ext_foreign_toplevel_handle_v1* handle, const char* value)
    {
        if (Window* w = static_cast<WindowCapture*>(data)->m_windows.value(handle)) {
            w->title = QString::fromUtf8(value);
        }
    }
    static void appId(void* data, ext_foreign_toplevel_handle_v1* handle, const char* value)
    {
        if (Window* w = static_cast<WindowCapture*>(data)->m_windows.value(handle)) {
            w->appId = QString::fromUtf8(value);
        }
    }
    static void identifier(void* data, ext_foreign_toplevel_handle_v1* handle, const char* value)
    {
        if (Window* w = static_cast<WindowCapture*>(data)->m_windows.value(handle)) {
            w->identifier = QString::fromUtf8(value);
        }
    }
    static constexpr ext_foreign_toplevel_handle_v1_listener handleListener {
        closed, done, title, appId, identifier
    };

    // --- sessione di cattura ---
    static void bufferSize(void* data, ext_image_copy_capture_session_v1*, uint32_t width, uint32_t height)
    {
        auto* job = static_cast<Job*>(data);
        job->width = width;
        job->height = height;
    }
    static void shmFormat(void* data, ext_image_copy_capture_session_v1*, uint32_t format)
    {
        auto* job = static_cast<Job*>(data);
        // Il primo formato leggibile va bene; XRGB/ARGB sono i più comuni.
        if (job->format < 0 && imageFormat(format) != QImage::Format_Invalid) {
            job->format = static_cast<int>(format);
        }
    }
    static void dmabufDevice(void*, ext_image_copy_capture_session_v1*, wl_array*) { }
    static void dmabufFormat(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) { }
    static void sessionDone(void* data, ext_image_copy_capture_session_v1* session)
    {
        auto* job = static_cast<Job*>(data);
        if (job->frame) {
            return; // arriva di nuovo se la finestra cambia dimensione
        }
        if (job->format < 0 || job->width == 0 || job->height == 0) {
            job->owner->finishJob(job, false);
            return;
        }
        const uint32_t stride = job->width * 4;
        job->size = size_t(stride) * job->height;
        job->fd = memfd_create("vela-thumbnail", MFD_CLOEXEC);
        if (job->fd < 0 || ftruncate(job->fd, off_t(job->size)) < 0) {
            job->owner->finishJob(job, false);
            return;
        }
        job->pixels = mmap(nullptr, job->size, PROT_READ | PROT_WRITE, MAP_SHARED, job->fd, 0);
        if (job->pixels == MAP_FAILED) {
            job->pixels = nullptr;
            job->owner->finishJob(job, false);
            return;
        }
        job->pool = wl_shm_create_pool(job->owner->m_shm, job->fd, int32_t(job->size));
        job->buffer = wl_shm_pool_create_buffer(job->pool, 0, int32_t(job->width), int32_t(job->height),
            int32_t(stride), uint32_t(job->format));

        job->frame = ext_image_copy_capture_session_v1_create_frame(session);
        ext_image_copy_capture_frame_v1_add_listener(job->frame, &frameListener, job);
        ext_image_copy_capture_frame_v1_attach_buffer(job->frame, job->buffer);
        ext_image_copy_capture_frame_v1_damage_buffer(job->frame, 0, 0, int32_t(job->width), int32_t(job->height));
        ext_image_copy_capture_frame_v1_capture(job->frame);
    }
    static void stopped(void* data, ext_image_copy_capture_session_v1*)
    {
        auto* job = static_cast<Job*>(data);
        job->owner->finishJob(job, false);
    }
    static constexpr ext_image_copy_capture_session_v1_listener sessionListener {
        bufferSize, shmFormat, dmabufDevice, dmabufFormat, sessionDone, stopped
    };

    // --- frame ---
    static void transform(void*, ext_image_copy_capture_frame_v1*, uint32_t) { }
    static void damage(void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) { }
    static void presentationTime(void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) { }
    static void ready(void* data, ext_image_copy_capture_frame_v1*)
    {
        auto* job = static_cast<Job*>(data);
        job->owner->finishJob(job, true);
    }
    static void failed(void* data, ext_image_copy_capture_frame_v1*, uint32_t)
    {
        auto* job = static_cast<Job*>(data);
        job->owner->finishJob(job, false);
    }
    static constexpr ext_image_copy_capture_frame_v1_listener frameListener {
        transform, damage, presentationTime, ready, failed
    };
};

// ---------------------------------------------------------- WindowCapture --

WindowCapture::WindowCapture(QObject* parent)
    : QObject(parent)
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display()) {
        return;
    }
    m_registry = wl_display_get_registry(wayland->display());
    wl_registry_add_listener(m_registry, &CaptureCallbacks::registryListener, this);
    wl_display_flush(wayland->display());
}

WindowCapture::~WindowCapture()
{
    const QList<Job*> jobs = m_jobs; // finishJob li toglie dalla lista
    for (Job* job : jobs) {
        finishJob(job, false);
    }
    qDeleteAll(m_windows);
}

QString WindowCapture::title(const QString& identifier) const
{
    for (const Window* window : m_windows) {
        if (window->identifier == identifier) {
            return window->title;
        }
    }
    return {};
}

QString WindowCapture::appId(const QString& identifier) const
{
    for (const Window* window : m_windows) {
        if (window->identifier == identifier) {
            return window->appId;
        }
    }
    return {};
}

void WindowCapture::capture(const QStringList& identifiers)
{
    if (!m_shm || !m_sources || !m_copier) {
        qWarning("vela-shell: il compositor non offre la cattura delle finestre");
        return;
    }
    for (const Window* window : std::as_const(m_windows)) {
        if (!identifiers.contains(window->identifier)) {
            continue;
        }
        auto* job = new Job;
        job->owner = this;
        job->identifier = window->identifier;
        job->source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(m_sources, window->handle);
        job->session = ext_image_copy_capture_manager_v1_create_session(m_copier, job->source, 0);
        ext_image_copy_capture_session_v1_add_listener(job->session, &CaptureCallbacks::sessionListener, job);
        m_jobs.append(job);
    }
    if (auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        wl_display_flush(wayland->display());
    }
}

void WindowCapture::finishJob(Job* job, bool ok)
{
    if (ok && job->pixels) {
        // Si tiene solo la miniatura: la cattura intera può essere grande
        // quanto lo schermo.
        const QImage full(static_cast<const uchar*>(job->pixels), int(job->width), int(job->height),
            int(job->width * 4), imageFormat(uint32_t(job->format)));
        m_thumbnails.insert(job->identifier,
            full.scaled(thumbnailSize, thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    if (job->frame) {
        ext_image_copy_capture_frame_v1_destroy(job->frame);
    }
    if (job->session) {
        ext_image_copy_capture_session_v1_destroy(job->session);
    }
    if (job->source) {
        ext_image_capture_source_v1_destroy(job->source);
    }
    if (job->buffer) {
        wl_buffer_destroy(job->buffer);
    }
    if (job->pool) {
        wl_shm_pool_destroy(job->pool);
    }
    if (job->pixels) {
        munmap(job->pixels, job->size);
    }
    if (job->fd >= 0) {
        close(job->fd);
    }
    const QString identifier = job->identifier;
    m_jobs.removeOne(job);
    delete job;

    if (ok) {
        emit thumbnailReady(identifier);
    }
}
