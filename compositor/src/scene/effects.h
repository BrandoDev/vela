// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_EFFECTS_H
#define VELA_SCENE_EFFECTS_H

// ext-background-effect-v1 (docs/renderer.md §8.3): le superfici chiedono
// di sfocare ciò che sta dietro una loro regione (la shell per taskbar,
// menu Start, menu, notifiche; le app che lo supportano). wlroots non lo
// implementa: lo facciamo qui. La regione è nello stato della superficie
// (si applica al commit), in coordinate della superficie.

#include <pixman.h>

struct vela_scene;
struct wl_display;
struct wlr_surface;

// Il global del protocollo. Una regione nuova chiede un frame alla scena.
void vela_background_effects_init(struct wl_display *display, struct vela_scene *scene);

// La regione da sfocare dietro la superficie, o NULL se non ce n'è.
const pixman_region32_t *vela_blur_region(struct wlr_surface *surface);

#endif
