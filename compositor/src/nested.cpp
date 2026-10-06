// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nested.hpp"

#include "server.hpp"

#include "fractional-scale-v1-client-protocol.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

namespace vela {

NestedWindow::NestedWindow(Output& output)
    : m_output(output)
    , m_remote(wlr_wl_backend_get_remote_display(output.wlr->backend))
    , m_surface(wlr_wl_output_get_surface(output.wlr))
    , m_width(output.wlr->width)
    , m_height(output.wlr->height)
{
    if (const char* scale = std::getenv("VELA_SCALE"); scale && *scale && !std::strchr(scale, '=')) {
        m_extraScale = std::max(0.25, std::strtod(scale, nullptr));
    }
    // I proxy stanno nella coda predefinita: i loro eventi li smista il
    // backend di wlroots insieme ai suoi.
    static const wl_registry_listener registryListener {
        .global = &NestedWindow::onGlobal,
        .global_remove = [](void*, wl_registry*, uint32_t) {},
    };
    m_registry = wl_display_get_registry(m_remote);
    wl_registry_add_listener(m_registry, &registryListener, this);
    wl_display_flush(m_remote);
}

NestedWindow::~NestedWindow()
{
    if (m_inhibitor) {
        zwp_keyboard_shortcuts_inhibitor_v1_destroy(m_inhibitor);
    }
    if (m_fractional) {
        wp_fractional_scale_v1_destroy(m_fractional);
    }
    if (m_viewport) {
        wp_viewport_destroy(m_viewport);
    }
    if (m_inhibitManager) {
        zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(m_inhibitManager);
    }
    if (m_fractionalManager) {
        wp_fractional_scale_manager_v1_destroy(m_fractionalManager);
    }
    if (m_viewporter) {
        wp_viewporter_destroy(m_viewporter);
    }
    if (m_seat) {
        wl_seat_destroy(m_seat);
    }
    wl_registry_destroy(m_registry);
    wl_display_flush(m_remote);
}

void NestedWindow::onGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
{
    auto* self = static_cast<NestedWindow*>(data);
    if (std::strcmp(interface, wp_viewporter_interface.name) == 0 && !self->m_viewporter) {
        self->m_viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
    } else if (std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0 && !self->m_fractionalManager) {
        self->m_fractionalManager = static_cast<wp_fractional_scale_manager_v1*>(
            wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
    } else if (std::strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) == 0
        && !self->m_inhibitManager) {
        self->m_inhibitManager = static_cast<zwp_keyboard_shortcuts_inhibit_manager_v1*>(
            wl_registry_bind(registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self->m_seat) {
        self->m_seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else {
        return;
    }
    self->setup();
}

void NestedWindow::onPreferredScale(void* data, wp_fractional_scale_v1*, uint32_t scale120)
{
    auto* self = static_cast<NestedWindow*>(data);
    const double scale = scale120 / 120.0;
    if (scale == self->m_hostScale) {
        return;
    }
    self->m_hostScale = scale;
    wlr_log(WLR_INFO, "%s: the host window has scale %.3f", self->m_output.wlr->name, scale);
    self->apply();
}

// Man mano che arrivano i protocolli dell'ospite.
void NestedWindow::setup()
{
    if (m_viewporter && m_fractionalManager && !m_fractional) {
        static const wp_fractional_scale_v1_listener fractionalListener {
            .preferred_scale = &NestedWindow::onPreferredScale,
        };
        m_viewport = wp_viewporter_get_viewport(m_viewporter, m_surface);
        m_fractional = wp_fractional_scale_manager_v1_get_fractional_scale(m_fractionalManager, m_surface);
        wp_fractional_scale_v1_add_listener(m_fractional, &fractionalListener, this);
    }
    if (m_inhibitManager && m_seat && !m_inhibitor) {
        m_inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(m_inhibitManager, m_surface, m_seat);
        wlr_log(WLR_INFO, "%s: asked the host for the keyboard shortcuts", m_output.wlr->name);
    }
    wl_display_flush(m_remote);
}

void NestedWindow::resize(int width, int height)
{
    // Se l'ospite lascia scegliere a noi, il backend ripropone la dimensione
    // attuale del buffer: non è una dimensione logica.
    // KWin manda un configure a ogni cambio di stato (attivazione, focus):
    // se la dimensione non cambia non si tocca nulla, o ogni volta si
    // rifarebbe il modo dello schermo.
    const bool bufferSize = width == m_output.wlr->width && height == m_output.wlr->height;
    if (width <= 0 || height <= 0 || bufferSize || (width == m_width && height == m_height)) {
        return;
    }
    m_width = width;
    m_height = height;
    apply();
}

// Buffer grande quanto i pixel fisici della finestra ospite; la scala dello
// schermo di Vela è quella dell'ospite (per VELA_SCALE): così le app ci
// disegnano alla risoluzione piena e nulla viene ingrandito due volte.
void NestedWindow::apply()
{
    const int bufferWidth = int(std::lround(m_width * m_hostScale));
    const int bufferHeight = int(std::lround(m_height * m_hostScale));
    const float scale = float(m_hostScale * m_extraScale);
    wlr_output* output = m_output.wlr;
    if (bufferWidth == output->width && bufferHeight == output->height && scale == output->scale) {
        return;
    }
    if (m_viewport) {
        wp_viewport_set_destination(m_viewport, m_width, m_height);
    }
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_custom_mode(&state, bufferWidth, bufferHeight, 0);
    wlr_output_state_set_scale(&state, scale);
    m_output.commitMode(state);
    wlr_output_state_finish(&state);
    wl_display_flush(m_remote);
}

double NestedWindow::pointerScaleX() const
{
    return m_width > 0 ? double(m_output.wlr->width) / m_width : 1.0;
}

double NestedWindow::pointerScaleY() const
{
    return m_height > 0 ? double(m_output.wlr->height) / m_height : 1.0;
}

} // namespace vela
