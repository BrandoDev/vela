// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layer.h"

#include "focus.h"
#include "output.h"
#include "popup.h"
#include "scene/scene.h"
#include "server.h"
#include "util.h"

#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_shell.h>

// Lo strato della scena per una superficie: lo strato "top" con la tastiera
// sta sopra lo schermo intero.
static struct vela_tree *layer_tree(struct vela_server *server, uint32_t layer, bool wants_keyboard)
{
    struct vela_layers *layers = &server->layers;
    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return layers->background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return layers->bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return wants_keyboard ? layers->top_above_fullscreen : layers->top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return layers->overlay;
    }
    return layers->top;
}

struct vela_output *vela_layer_surface_output(const struct vela_layer_surface *layer)
{
    return layer->wlr->output ? layer->wlr->output->data : NULL;
}

bool vela_layer_surface_wants_keyboard(const struct vela_layer_surface *layer)
{
    return layer->wlr->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
}

static void handle_map(struct wl_listener *listener, void *data)
{
    struct vela_layer_surface *layer = wl_container_of(listener, layer, map);
    // Menu Start, launcher & co. ricevono subito la tastiera.
    uint32_t which = layer->wlr->current.layer;
    if (vela_layer_surface_wants_keyboard(layer)
        && (which == ZWLR_LAYER_SHELL_V1_LAYER_TOP || which == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)) {
        vela_focus_layer(layer->server, layer);
    }
}

static void handle_unmap(struct wl_listener *listener, void *data)
{
    struct vela_layer_surface *layer = wl_container_of(listener, layer, unmap);
    vela_focus_layer_unmapped(layer->server, layer);
}

static void handle_commit(struct wl_listener *listener, void *data)
{
    struct vela_layer_surface *layer = wl_container_of(listener, layer, commit);
    struct wlr_layer_surface_v1 *wlr = layer->wlr;
    uint32_t committed = wlr->current.committed;

    // Il client può spostarsi di strato (es. da "bottom" a "top"), o
    // chiedere la tastiera (e salire sopra lo schermo intero).
    if (wlr->initialized
        && (committed & (WLR_LAYER_SURFACE_V1_STATE_LAYER | WLR_LAYER_SURFACE_V1_STATE_KEYBOARD_INTERACTIVITY))) {
        layer->layer = wlr->current.layer;
        struct vela_tree *target = layer_tree(layer->server, layer->layer, vela_layer_surface_wants_keyboard(layer));
        if (layer->tree->node.parent != target) {
            vela_node_reparent(&layer->tree->node, target);
        }
    }

    // Si ridispone solo quando cambia qualcosa che conta: ogni configure
    // costringe il client a ridisegnare.
    if (wlr->initial_commit || committed || wlr->surface->mapped != layer->mapped) {
        layer->mapped = wlr->surface->mapped;
        struct vela_output *output = vela_layer_surface_output(layer);
        if (output) {
            vela_output_arrange_layers(output);
        }
    }
}

static void handle_new_popup(struct wl_listener *listener, void *data)
{
    struct vela_layer_surface *layer = wl_container_of(listener, layer, new_popup);
    vela_popup_create(data, layer->tree, &layer->owner);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct vela_layer_surface *layer = wl_container_of(listener, layer, destroy);
    wl_list_remove(&layer->link);
    vela_focus_forget_layer(layer->server, layer);
    struct vela_output *output = vela_layer_surface_output(layer);
    if (output) {
        vela_output_arrange_layers(output); // libera lo spazio che occupava
    }
    wl_list_remove(&layer->map.link);
    wl_list_remove(&layer->unmap.link);
    wl_list_remove(&layer->commit.link);
    wl_list_remove(&layer->destroy.link);
    wl_list_remove(&layer->new_popup.link);
    vela_node_destroy(&layer->surface_node->node);
    vela_node_destroy(&layer->tree->node);
    free(layer);
}

static void handle_new_surface(struct wl_listener *listener, void *data)
{
    struct vela_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *wlr = data;
    // Se il client non ha scelto uno schermo, quello sotto il cursore.
    if (!wlr->output) {
        struct vela_output *output = vela_output_under_cursor(server);
        if (!output) {
            wlr_layer_surface_v1_destroy(wlr);
            return;
        }
        wlr->output = output->wlr;
    }
    struct vela_layer_surface *layer = calloc(1, sizeof(*layer));
    layer->owner.kind = VELA_OWNER_LAYER;
    layer->server = server;
    layer->wlr = wlr;
    layer->layer = wlr->pending.layer;
    struct vela_tree *parent = layer_tree(server, wlr->pending.layer,
        wlr->pending.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    layer->tree = vela_tree_create(parent);
    layer->surface_node = vela_surface_node_create(layer->tree, wlr->surface);
    layer->tree->node.data = &layer->owner;

    layer->map.notify = handle_map;
    wl_signal_add(&wlr->surface->events.map, &layer->map);
    layer->unmap.notify = handle_unmap;
    wl_signal_add(&wlr->surface->events.unmap, &layer->unmap);
    layer->commit.notify = handle_commit;
    wl_signal_add(&wlr->surface->events.commit, &layer->commit);
    layer->destroy.notify = handle_destroy;
    wl_signal_add(&wlr->events.destroy, &layer->destroy);
    layer->new_popup.notify = handle_new_popup;
    wl_signal_add(&wlr->events.new_popup, &layer->new_popup);
    wl_list_insert(server->layer_surfaces.prev, &layer->link);
}

void vela_layers_init(struct vela_server *server)
{
    wl_list_init(&server->layer_surfaces);
    server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
    server->new_layer_surface.notify = handle_new_surface;
    wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
}

// ------------------------------------------------------------ disposizione --

// Lo spazio che una superficie "esclusiva" (la taskbar) toglie all'area utile.
static void apply_exclusive_zone(const struct wlr_layer_surface_v1_state *state, enum wlr_edges edge,
    struct wlr_box *usable)
{
    switch (edge) {
    case WLR_EDGE_NONE:
        return;
    case WLR_EDGE_TOP:
        usable->y += state->exclusive_zone + state->margin.top;
        usable->height -= state->exclusive_zone + state->margin.top;
        break;
    case WLR_EDGE_BOTTOM:
        usable->height -= state->exclusive_zone + state->margin.bottom;
        break;
    case WLR_EDGE_LEFT:
        usable->x += state->exclusive_zone + state->margin.left;
        usable->width -= state->exclusive_zone + state->margin.left;
        break;
    case WLR_EDGE_RIGHT:
        usable->width -= state->exclusive_zone + state->margin.right;
        break;
    }
    usable->width = vela_max(usable->width, 0);
    usable->height = vela_max(usable->height, 0);
}

// Come wlr_scene_layer_surface_v1_configure di wlroots: dimensione e
// posizione secondo ancore e margini, dentro l'area utile (o tutto lo
// schermo se la superficie lo chiede con exclusive_zone = -1).
static void configure(struct vela_layer_surface *layer, const struct wlr_box *full, struct wlr_box *usable)
{
    struct wlr_layer_surface_v1 *wlr = layer->wlr;
    const struct wlr_layer_surface_v1_state *state = &wlr->current;
    const struct wlr_box bounds = state->exclusive_zone == -1 ? *full : *usable;
    uint32_t anchor = state->anchor;
    bool left = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    bool right = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    bool top = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    bool bottom = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    struct wlr_box box = { 0, 0, (int)state->desired_width, (int)state->desired_height };
    if (box.width == 0) {
        box.x = bounds.x + state->margin.left;
        box.width = bounds.width - (state->margin.left + state->margin.right);
    } else if (left && right) {
        box.x = bounds.x + bounds.width / 2 - box.width / 2;
    } else if (left) {
        box.x = bounds.x + state->margin.left;
    } else if (right) {
        box.x = bounds.x + bounds.width - box.width - state->margin.right;
    } else {
        box.x = bounds.x + bounds.width / 2 - box.width / 2;
    }
    if (box.height == 0) {
        box.y = bounds.y + state->margin.top;
        box.height = bounds.height - (state->margin.top + state->margin.bottom);
    } else if (top && bottom) {
        box.y = bounds.y + bounds.height / 2 - box.height / 2;
    } else if (top) {
        box.y = bounds.y + state->margin.top;
    } else if (bottom) {
        box.y = bounds.y + bounds.height - box.height - state->margin.bottom;
    } else {
        box.y = bounds.y + bounds.height / 2 - box.height / 2;
    }

    vela_node_set_position(&layer->tree->node, box.x, box.y);
    wlr_layer_surface_v1_configure(wlr, (uint32_t)vela_max(box.width, 0), (uint32_t)vela_max(box.height, 0));
    if (wlr->surface->mapped && state->exclusive_zone > 0) {
        apply_exclusive_zone(state, wlr_layer_surface_v1_get_exclusive_edge(wlr), usable);
    }
}

void vela_layers_configure(struct vela_server *server, struct vela_output *output, const struct wlr_box *full,
    struct wlr_box *usable)
{
    const uint32_t order[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };
    for (int exclusive = 1; exclusive >= 0; --exclusive) {
        for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
            struct vela_layer_surface *layer;
            wl_list_for_each (layer, &server->layer_surfaces, link) {
                if (layer->wlr->output != output->wlr || layer->layer != order[i] || !layer->wlr->initialized) {
                    continue;
                }
                if ((layer->wlr->current.exclusive_zone > 0) != (bool)exclusive) {
                    continue;
                }
                configure(layer, full, usable);
            }
        }
    }
}

void vela_layers_close_output(struct vela_server *server, struct vela_output *output)
{
    struct vela_layer_surface *layer, *next;
    wl_list_for_each_safe (layer, next, &server->layer_surfaces, link) {
        if (layer->wlr->output == output->wlr) {
            layer->wlr->output = NULL;
            wlr_layer_surface_v1_destroy(layer->wlr);
        }
    }
}
