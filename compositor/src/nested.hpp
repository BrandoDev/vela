// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Vela in una finestra dentro un'altra sessione (es. KDE), per le prove.
// Il backend annidato di wlroots non sa due cose, e le facciamo noi parlando
// direttamente con il compositor ospite:
//
// - Nitidezza: l'ospite a scala frazionaria (KDE al 125%) ingrandirebbe la
//   nostra finestra e la sfocherebbe. Chiediamo la sua scala
//   (fractional-scale-v1) e disegniamo un buffer grande quanto i suoi pixel
//   fisici, dichiarando con viewporter la dimensione logica.
// - Scorciatoie: chiediamo all'ospite di non intercettare Super, Alt+Tab & co.
//   quando la finestra di Vela ha la tastiera (keyboard-shortcuts-inhibit).

#include "wlr.hpp"

struct wp_viewporter;
struct wp_viewport;
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;
struct zwp_keyboard_shortcuts_inhibit_manager_v1;
struct zwp_keyboard_shortcuts_inhibitor_v1;

namespace vela {

struct Output;

class NestedWindow {
public:
    explicit NestedWindow(Output& output);
    ~NestedWindow();
    NestedWindow(const NestedWindow&) = delete;
    NestedWindow& operator=(const NestedWindow&) = delete;

    // L'ospite chiede una nuova dimensione per la finestra (in unità sue).
    void resize(int width, int height);

    // Pixel del nostro buffer per ogni unità della finestra ospite: il
    // backend annidato riporta il puntatore in pixel del buffer.
    double pointerScaleX() const;
    double pointerScaleY() const;

private:
    static void onGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void onPreferredScale(void* data, wp_fractional_scale_v1* fractional, uint32_t scale120);
    void setup();
    void apply();

    Output& m_output;
    wl_display* m_remote;
    wl_surface* m_surface;
    wl_registry* m_registry = nullptr;
    wp_viewporter* m_viewporter = nullptr;
    wp_viewport* m_viewport = nullptr;
    wp_fractional_scale_manager_v1* m_fractionalManager = nullptr;
    wp_fractional_scale_v1* m_fractional = nullptr;
    zwp_keyboard_shortcuts_inhibit_manager_v1* m_inhibitManager = nullptr;
    zwp_keyboard_shortcuts_inhibitor_v1* m_inhibitor = nullptr;
    wl_seat* m_seat = nullptr;

    int m_width; // dimensione logica della finestra ospite
    int m_height;
    double m_hostScale = 1.0; // scala dell'ospite (KDE al 125%: 1.25)
    double m_extraScale = 1.0; // VELA_SCALE, sopra quella dell'ospite
};

} // namespace vela
