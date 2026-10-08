// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "focus.h"

#include "input.h"
#include "interact.h"
#include "layer.h"
#include "scene/scene.h"
#include "server.h"
#include "snap.h"
#include "switcher.h"
#include "view.h"
#include "workspace.h"

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>

void vela_focus_view(struct vela_server *server, struct vela_view *view)
{
    if (!view || !view->mapped || server->locked) {
        return;
    }
    // Una finestra di un altro desktop: si va su quel desktop, come Windows.
    if (!vela_view_on_current_workspace(view)) {
        vela_workspaces_switch(server, view->workspace, false);
    }
    if (view->minimized) {
        vela_view_set_minimized(view, false); // la riaccende e torna qui
        return;
    }

    // Porta in primo piano e in testa alla lista in ogni caso. I dialoghi di
    // sistema restano sopra (ma la tastiera va dove ha cliccato l'utente).
    vela_node_raise_to_top(&view->tree->node);
    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);
    if (!vela_view_is_system_prompt(view)) {
        struct vela_view *prompt = vela_views_system_prompt(server);
        if (prompt) {
            vela_node_raise_to_top(&prompt->tree->node);
        }
    }

    // Un pannello che ha chiesto la tastiera in modo esclusivo (es.
    // schermata di blocco) non la cede a una finestra.
    struct vela_layer_surface *layer = server->focused_layer;
    if (layer && layer->wlr->current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
        return;
    }
    server->focused_layer = NULL;
    server->previous_layer = NULL;

    struct wlr_surface *surface = vela_view_surface(view);
    if (server->seat->keyboard_state.focused_surface == surface) {
        return;
    }
    // La finestra attiva si cerca tra le nostre finestre vive: una appena
    // chiusa è già fuori dalla lista, e non le si manda nulla.
    struct vela_view *previous = vela_views_focused(server);
    if (previous && previous != view) {
        vela_view_set_activated(previous, false);
    }
    vela_view_set_activated(view, true);
    vela_input_keyboard_enter(server->input, surface);
}

void vela_focus_new_view(struct vela_server *server, struct vela_view *view)
{
    vela_focus_view(server, view);
    if (!vela_view_is_system_prompt(view)) {
        struct vela_view *prompt = vela_views_system_prompt(server);
        if (prompt) {
            vela_focus_view(server, prompt);
        }
    }
}

void vela_focus_layer(struct vela_server *server, struct vela_layer_surface *layer)
{
    // Si usa lo stato della superficie: l'evento "map" arriva prima del
    // commit in cui si aggiorna layer->mapped.
    if (!layer || !layer->wlr->surface->mapped || server->locked) {
        return;
    }
    // Come su Windows: aprendo il menu Start la finestra attiva si "spegne".
    struct vela_view *active = vela_views_focused(server);
    if (active) {
        vela_view_set_activated(active, false);
    }
    // Un menu aperto da un pannello (es. dal menu Start): chiuso il menu, la
    // tastiera torna al pannello.
    if (server->focused_layer && server->focused_layer != layer) {
        server->previous_layer = server->focused_layer;
    }
    server->focused_layer = layer;
    vela_input_keyboard_enter(server->input, layer->wlr->surface);
}

void vela_focus_refocus(struct vela_server *server)
{
    if (server->locked) {
        return; // la tastiera è della schermata di blocco
    }
    server->focused_layer = NULL;
    struct vela_layer_surface *previous = server->previous_layer;
    server->previous_layer = NULL;
    if (previous && previous->wlr->surface->mapped && vela_layer_surface_wants_keyboard(previous)) {
        vela_focus_layer(server, previous);
        return;
    }
    struct vela_view *view;
    wl_list_for_each (view, &server->views, link) {
        if (view->mapped && !view->minimized && vela_view_on_current_workspace(view)) {
            vela_focus_view(server, view);
            return;
        }
    }
    // Nessuna finestra su questo desktop: la tastiera non va a nessuno.
    struct vela_view *focused = vela_views_focused(server);
    if (focused) {
        vela_view_set_activated(focused, false);
    }
    wlr_seat_keyboard_notify_clear_focus(server->seat);
}

void vela_focus_forget_view(struct vela_server *server, struct vela_view *view, bool was_focused)
{
    vela_snap_forget(server, view);
    vela_interact_forget(server, view);
    // Una finestra che sparisce durante Alt+Tab: si chiude il selettore.
    vela_switcher_forget(server, view);
    if (server->grabbed == view) {
        vela_snap_end_zone(server, false);
        server->grabbed = NULL;
        server->cursor_mode = VELA_CURSOR_PASSTHROUGH;
    }
    if (was_focused && !server->focused_layer) {
        vela_focus_refocus(server);
    }
}

void vela_focus_layer_unmapped(struct vela_server *server, struct vela_layer_surface *layer)
{
    if (server->focused_layer == layer) {
        vela_focus_refocus(server);
    }
}

void vela_focus_forget_layer(struct vela_server *server, struct vela_layer_surface *layer)
{
    if (server->previous_layer == layer) {
        server->previous_layer = NULL;
    }
    if (server->focused_layer == layer) {
        vela_focus_refocus(server);
    }
}
