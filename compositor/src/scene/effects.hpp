// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// ext-background-effect-v1 (docs/renderer.md §8.3): le superfici chiedono
// di sfocare ciò che sta dietro una loro regione (la shell per taskbar,
// menu Start, menu, notifiche; le app che lo supportano). wlroots non lo
// implementa: lo facciamo qui. La regione è nello stato della superficie
// (si applica al commit), in coordinate della superficie.

#include "wlr.hpp"

namespace vela::scene {

void initBackgroundEffects(wl_display* display);

// La regione da sfocare dietro la superficie, o null se non ce n'è.
const pixman_region32_t* blurRegion(wlr_surface* surface);

} // namespace vela::scene
