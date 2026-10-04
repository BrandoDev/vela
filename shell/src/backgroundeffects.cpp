#include "backgroundeffects.h"

#include <QGuiApplication>
#include <QRectF>
#include <QtDebug>
#include <QtGui/qguiapplication_platform.h>
// API privata di Qt (come in foreigntoplevels.cpp): la wl_surface di una finestra.
#include <QtGui/qpa/qplatformwindow_p.h>

#include <cstring>

#include <wayland-client.h>

#include "ext-background-effect-v1-client-protocol.h"

struct EffectCallbacks {
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
    {
        auto* self = static_cast<BackgroundEffects*>(data);
        if (!strcmp(interface, wl_compositor_interface.name)) {
            self->m_compositor = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
        } else if (!strcmp(interface, ext_background_effect_manager_v1_interface.name)) {
            self->m_manager = static_cast<ext_background_effect_manager_v1*>(
                wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
            ext_background_effect_manager_v1_add_listener(self->m_manager, &managerListener, self);
        }
    }
    static void globalRemove(void*, wl_registry*, uint32_t) { }
    static constexpr wl_registry_listener registryListener { global, globalRemove };

    static void capabilities(void* data, ext_background_effect_manager_v1*, uint32_t flags)
    {
        auto* self = static_cast<BackgroundEffects*>(data);
        const bool blur = flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR;
        qInfo("vela-shell: sfocatura del compositor %s", blur ? "disponibile" : "assente");
        if (blur != self->m_blurAvailable) {
            self->m_blurAvailable = blur;
            // Le regioni chieste prima che la capacità arrivasse.
            for (auto& entry : self->m_entries) {
                self->apply(entry);
            }
            emit self->blurAvailableChanged();
        }
    }
    static constexpr ext_background_effect_manager_v1_listener managerListener { capabilities };
};

BackgroundEffects::BackgroundEffects(QObject* parent)
    : QObject(parent)
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display()) {
        return;
    }
    m_registry = wl_display_get_registry(wayland->display());
    wl_registry_add_listener(m_registry, &EffectCallbacks::registryListener, this);
    // Global e capacità arrivano con gli eventi di Qt: le regioni chieste
    // prima si applicano allora (vedi capabilities).
    wl_display_flush(wayland->display());
}

BackgroundEffects::~BackgroundEffects()
{
    for (auto& entry : m_entries) {
        drop(entry);
    }
}

void BackgroundEffects::setBlur(QWindow* window, const QVariantList& rects)
{
    if (!window) {
        return;
    }
    QRegion region;
    for (const QVariant& value : rects) {
        region += value.toRectF().toAlignedRect();
    }
    Entry& entry = m_entries[window];
    entry.window = window;
    if (entry.region == region && entry.effect) {
        return;
    }
    entry.region = region;
    if (!entry.connected) {
        entry.connected = true;
        connect(window, &QObject::destroyed, this, [this, window] { m_entries.remove(window); });
        // L'interfaccia nativa c'è solo da quando la finestra esiste davvero.
        connect(window, &QWindow::visibleChanged, this, [this, window](bool visible) {
            if (visible && m_entries.contains(window)) {
                follow(m_entries[window]);
                apply(m_entries[window]);
            }
        });
    }
    follow(entry);
    apply(entry);
}

void BackgroundEffects::follow(Entry& entry)
{
    // La wl_surface cambia quando la finestra si nasconde e torna: si segue.
    if (entry.following || !entry.window) {
        return;
    }
    auto* native = entry.window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!native) {
        return;
    }
    entry.following = true;
    QWindow* window = entry.window;
    connect(native, &QNativeInterface::Private::QWaylandWindow::surfaceCreated, this, [this, window] {
        if (m_entries.contains(window)) {
            apply(m_entries[window]);
        }
    });
    connect(native, &QNativeInterface::Private::QWaylandWindow::surfaceDestroyed, this, [this, window] {
        if (m_entries.contains(window)) {
            drop(m_entries[window]);
        }
    });
}

void BackgroundEffects::drop(Entry& entry)
{
    if (entry.effect) {
        ext_background_effect_surface_v1_destroy(entry.effect);
        entry.effect = nullptr;
    }
}

void BackgroundEffects::apply(Entry& entry)
{
    if (!m_manager || !m_compositor || !m_blurAvailable || !entry.window) {
        return;
    }
    auto* native = entry.window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    wl_surface* surface = native ? native->surface() : nullptr;
    if (!surface) {
        return; // la finestra non è ancora visibile: si applica a surfaceCreated
    }
    if (!entry.effect) {
        entry.effect = ext_background_effect_manager_v1_get_background_effect(m_manager, surface);
    }
    if (entry.region.isEmpty()) {
        ext_background_effect_surface_v1_set_blur_region(entry.effect, nullptr);
    } else {
        wl_region* region = wl_compositor_create_region(m_compositor);
        for (const QRect& rect : entry.region) {
            wl_region_add(region, rect.x(), rect.y(), rect.width(), rect.height());
        }
        ext_background_effect_surface_v1_set_blur_region(entry.effect, region);
        wl_region_destroy(region);
    }
    // Stato doppio: vale dal prossimo commit, che chiediamo subito.
    entry.window->requestUpdate();
}
