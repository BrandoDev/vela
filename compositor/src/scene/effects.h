// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_EFFECTS_H
#define VELA_SCENE_EFFECTS_H

// ext-background-effect-v1 (docs/renderer.md §8.3): surfaces ask to blur what
// lies behind a region of theirs (the shell for the taskbar, Start menu,
// menus, notifications; apps that support it). wlroots doesn't implement it,
// so it's done here. The region is part of the surface state (applied on
// commit), in surface coordinates.

#include <pixman.h>

struct vela_scene;
struct wl_display;
struct wlr_surface;

// The protocol global. A new region asks the scene for a frame.
void vela_background_effects_init(struct wl_display *display, struct vela_scene *scene);

// The region to blur behind the surface, or NULL if there is none.
const pixman_region32_t *vela_blur_region(struct wlr_surface *surface);

#endif
