// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_LAYER_H
#define VELA_LAYER_H

// I pezzi della shell (wlr-layer-shell): taskbar, menu Start, sfondo,
// notifiche. Ognuno sta nello strato della scena che chiede; quelli che
// vogliono la tastiera e stanno nello strato "top" salgono sopra lo
// schermo intero (come su Windows: il menu Start si apre anche sopra un
// gioco, che invece copre la taskbar).

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
    struct vela_owner owner; // primo campo: il vela_node.data dell'albero
    struct wl_list link; // vela_server.layer_surfaces
    struct vela_server *server;
    struct wlr_layer_surface_v1 *wlr;
    struct vela_tree *tree;
    struct vela_surface_node *surface_node;
    uint32_t layer; // zwlr_layer_shell_v1_layer: lo strato in cui sta l'albero
    bool mapped;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener new_popup;
};

// Il protocollo e la lista delle superfici del server.
void vela_layers_init(struct vela_server *server);

struct vela_output *vela_layer_surface_output(const struct vela_layer_surface *layer);
bool vela_layer_surface_wants_keyboard(const struct vela_layer_surface *layer);

// Dispone le superfici di uno schermo secondo ancore e margini (prima quelle
// che riservano spazio, poi le altre, dall'alto verso il basso: l'ordine di
// sway) e toglie da `usable` lo spazio riservato.
void vela_layers_configure(struct vela_server *server, struct vela_output *output, const struct wlr_box *full,
    struct wlr_box *usable);

// Lo schermo se ne va: le sue superfici si chiudono.
void vela_layers_close_output(struct vela_server *server, struct vela_output *output);

#endif
