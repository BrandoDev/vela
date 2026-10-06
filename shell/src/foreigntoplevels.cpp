// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foreigntoplevels.h"

#include <QGuiApplication>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
// API privata di Qt (la usa anche KDE): l'unico modo per avere la
// wl_surface di una finestra. Può cambiare tra versioni minori di Qt.
#include <QtGui/qpa/qplatformwindow_p.h>

namespace {

quint64 activationCounter = 0;

} // namespace

// ------------------------------------------------------- ForeignToplevel --

ForeignToplevel::ForeignToplevel(::zwlr_foreign_toplevel_handle_v1* handle, QObject* parent)
    : QObject(parent)
    , QtWayland::zwlr_foreign_toplevel_handle_v1(handle)
{
}

ForeignToplevel::~ForeignToplevel()
{
    if (isInitialized()) {
        destroy();
    }
}

void ForeignToplevel::requestActivate()
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (wayland && wayland->seat()) {
        activate(wayland->seat());
    }
}

void ForeignToplevel::requestMinimize()
{
    set_minimized();
}

void ForeignToplevel::setButtonRect(QWindow* panel, const QRect& rect)
{
    if (!panel || rect == m_buttonRect) {
        return;
    }
    auto* wayland = panel->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!wayland || !wayland->surface()) {
        return;
    }
    m_buttonRect = rect;
    set_rectangle(wayland->surface(), rect.x(), rect.y(), rect.width(), rect.height());
}

void ForeignToplevel::zwlr_foreign_toplevel_handle_v1_title(const QString& value)
{
    m_pending.title = value;
}

void ForeignToplevel::zwlr_foreign_toplevel_handle_v1_app_id(const QString& value)
{
    m_pending.appId = value;
}

void ForeignToplevel::zwlr_foreign_toplevel_handle_v1_state(wl_array* state)
{
    m_pending.activated = false;
    m_pending.minimized = false;
    m_pending.maximized = false;
    m_pending.fullscreen = false;
    const auto* values = static_cast<const uint32_t*>(state->data);
    for (size_t i = 0; i < state->size / sizeof(uint32_t); ++i) {
        switch (values[i]) {
        case state_activated: m_pending.activated = true; break;
        case state_minimized: m_pending.minimized = true; break;
        case state_maximized: m_pending.maximized = true; break;
        case state_fullscreen: m_pending.fullscreen = true; break;
        default: break;
        }
    }
}

void ForeignToplevel::zwlr_foreign_toplevel_handle_v1_parent(::zwlr_foreign_toplevel_handle_v1* parent)
{
    m_pending.parentWindow = parent ? static_cast<ForeignToplevel*>(fromObject(parent)) : nullptr;
}

void ForeignToplevel::zwlr_foreign_toplevel_handle_v1_done()
{
    if (m_pending.activated && !activated) {
        lastActivated = ++activationCounter;
    }
    title = m_pending.title;
    appId = m_pending.appId;
    activated = m_pending.activated;
    minimized = m_pending.minimized;
    maximized = m_pending.maximized;
    fullscreen = m_pending.fullscreen;
    parentWindow = m_pending.parentWindow;
    ready = true;
    emit changed();
}

void ForeignToplevel::zwlr_foreign_toplevel_handle_v1_closed()
{
    emit closed();
}

// ------------------------------------------------ ForeignToplevelManager --

ForeignToplevelManager::ForeignToplevelManager()
    : QWaylandClientExtensionTemplate(3)
{
    initialize();
}

void ForeignToplevelManager::zwlr_foreign_toplevel_manager_v1_toplevel(::zwlr_foreign_toplevel_handle_v1* handle)
{
    auto* window = new ForeignToplevel(handle, this);

    connect(window, &ForeignToplevel::changed, this, [this, window] {
        if (!m_windows.contains(window)) {
            m_windows.append(window); // al primo "done": ora sappiamo chi è
        }
        emit windowsChanged();
    });
    connect(window, &ForeignToplevel::closed, this, [this, window] {
        // Chi punta a questa finestra come genitore non deve restare appeso.
        for (ForeignToplevel* other : std::as_const(m_windows)) {
            if (other->parentWindow == window) {
                other->parentWindow = nullptr;
            }
        }
        const bool wasListed = m_windows.removeOne(window);
        window->deleteLater();
        if (wasListed) {
            emit windowsChanged();
        }
    });
}
