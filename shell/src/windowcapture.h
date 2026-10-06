#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QVariantList>

struct wl_registry;
struct wl_shm;
struct ext_foreign_toplevel_list_v1;
struct ext_foreign_toplevel_handle_v1;
struct ext_foreign_toplevel_image_capture_source_manager_v1;
struct ext_output_image_capture_source_manager_v1;
struct ext_image_copy_capture_manager_v1;

// Anteprime delle finestre (per Alt+Tab), con i protocolli standard
// ext-foreign-toplevel-list (ogni finestra ha un identificativo) ed
// ext-image-copy-capture. Ogni cattura è una fotografia: si fa quando il
// selettore si apre, poi si tiene solo la miniatura.
//
// Usa libwayland direttamente, sulla stessa connessione di Qt: gli eventi
// arrivano nella coda predefinita, che Qt smista nel thread principale.
class WindowCapture : public QObject {
    Q_OBJECT

public:
    explicit WindowCapture(QObject* parent = nullptr);
    ~WindowCapture() override;

    Q_INVOKABLE void capture(const QStringList& identifiers);
    // Una fotografia di ogni schermo: le miniature si chiamano "screen:<nome>".
    Q_INVOKABLE void captureScreens();
    // Lo stesso a piena risoluzione (lo Strumento di cattura): poi
    // screenCaptured e fullImage(nome). Il numero di schermi catturati.
    int captureScreensFull();
    QImage fullImage(const QString& screen) const { return m_full.value(screen); }
    void clearFull() { m_full.clear(); }
    // Le finestre aperte: [{id, title, appId}], nell'ordine di apertura.
    Q_INVOKABLE QVariantList windowList() const;
    Q_INVOKABLE QString title(const QString& identifier) const;
    Q_INVOKABLE QString appId(const QString& identifier) const;
    QImage thumbnail(const QString& identifier) const { return m_thumbnails.value(identifier); }

    // Lato più lungo delle miniature, in pixel.
    static constexpr int thumbnailSize = 400;

signals:
    void thumbnailReady(const QString& identifier);
    void screenCaptured(const QString& screen, bool ok);

private:
    struct Window;
    struct Job;
    friend struct CaptureCallbacks;

    void finishJob(Job* job, bool ok);

    wl_registry* m_registry = nullptr;
    wl_shm* m_shm = nullptr;
    ext_foreign_toplevel_list_v1* m_list = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1* m_sources = nullptr;
    ext_output_image_capture_source_manager_v1* m_outputSources = nullptr;
    ext_image_copy_capture_manager_v1* m_copier = nullptr;

    QHash<ext_foreign_toplevel_handle_v1*, Window*> m_windows;
    QHash<QString, QImage> m_thumbnails;
    QHash<QString, QImage> m_full; // schermo -> immagine intera
    QList<Job*> m_jobs;
};

// Le miniature per il QML: "image://thumbnail/<identificativo>/<versione>"
// (la versione cambia a ogni nuova cattura, così Qt non usa quella vecchia).
class ThumbnailProvider : public QQuickImageProvider {
public:
    explicit ThumbnailProvider(WindowCapture* capture)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_capture(capture)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize&) override
    {
        const QImage image = m_capture->thumbnail(id.section(u'/', 0, 0));
        if (size) {
            *size = image.size();
        }
        return image;
    }

private:
    WindowCapture* m_capture;
};
