// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_LAYER_H
#define VELA_LAYER_H

// Shell pieces (wlr-layer-shell): taskbar, Start menu, wallpaper,
// notifications. Each sits in the scene layer it asks for; those that want the
// keyboard and sit in the "top" layer rise above fullscreen (like Windows: the
// Start menu opens above a game too, which instead covers the taskbar).

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "view.h"

struct vela_output;
struct vela_server;
struct vela_surface_node;
struct vela_tree;
struct wlr_layer_surface_v1;

struct vela_layer_surface {
    struct vela_owner owner; // first field: the tree's vela_node.data
    struct wl_list link; // vela_server.layer_surfaces
    struct vela_server *server;
    struct wlr_layer_surface_v1 *wlr;
    struct vela_tree *tree;
    struct vela_surface_node *surface_node;
    uint32_t layer; // zwlr_layer_shell_v1_layer: the layer the tree is in
    bool mapped;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener new_popup;
};

// The protocol and the server's list of surfaces.
void vela_layers_init(struct vela_server *server);

struct vela_output *vela_layer_surface_output(const struct vela_layer_surface *layer);
bool vela_layer_surface_wants_keyboard(const struct vela_layer_surface *layer);

// Lays out an output's surfaces by anchors and margins (first those reserving
// space, then the others, top to bottom: sway's order) and takes the reserved
// space out of `usable`.
void vela_layers_configure(struct vela_server *server, struct vela_output *output, const struct wlr_box *full,
    struct wlr_box *usable);

// The output goes away: its surfaces are closed.
void vela_layers_close_output(struct vela_server *server, struct vela_output *output);

#endif
