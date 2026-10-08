// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Le app X11 (Steam, molti giochi, app vecchie) attraverso Xwayland.
//
// Xwayland parte solo quando la prima app X11 si collega. Ogni sua finestra
// "gestita" diventa una vela_view come quelle Wayland: stesse animazioni,
// snap, taskbar e Alt+Tab. Le finestre "override-redirect" (menu, tooltip,
// tendine) si mettono dove dice l'app, sopra tutto, in uno strato loro.
//
// Le app X11 non conoscono la scala frazionaria: disegnano a 1× e il
// compositor le ingrandisce con il filtro di qualità (docs/renderer.md §3.9).

#include "view.h"

#include "interact.h"
#include "focus.h"
#include "bindings.h"
#include "input.h"
#include "output.h"
#include "scene/scene.h"
#include "server.h"
#include "snap.h"

#include <math.h>
#include <stdlib.h>
#include <wlr/config.h>
#include <wlr/util/log.h>

#if WLR_HAS_XWAYLAND

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland.h>

static void listen(struct wl_signal *signal, struct wl_listener *listener, wl_notify_func_t notify)
{
    listener->notify = notify;
    wl_signal_add(signal, listener);
}

static void unlisten(struct wl_listener *listener)
{
    wl_list_remove(&listener->link);
    wl_list_init(&listener->link);
}

// ------------------------------------------------- menu, tooltip, tendine --

// Un menu, un tooltip o una tendina X11: nessun gestore di finestre, la
// posizione la sceglie l'app (in coordinate globali).
struct unmanaged {
    struct vela_server *server;
    struct wlr_xwayland_surface *x11;
    struct vela_tree *tree;
    struct vela_surface_node *surface_node;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener set_geometry;
    struct wl_listener request_configure;
    struct wl_listener destroy;
};

static void handle_unmanaged_map(struct wl_listener *listener, void *data)
{
    struct unmanaged *menu = wl_container_of(listener, menu, map);
    vela_node_set_position(&menu->tree->node, menu->x11->x, menu->x11->y);
    vela_node_raise_to_top(&menu->tree->node);
    vela_node_set_enabled(&menu->tree->node, true);
    // Alcuni menu X11 vogliono la tastiera (per scorrere le voci).
    if (wlr_xwayland_surface_override_redirect_wants_focus(menu->x11)) {
        vela_input_keyboard_enter(menu->server->input, menu->x11->surface);
    }
}

static void handle_unmanaged_unmap(struct wl_listener *listener, void *data)
{
    struct unmanaged *menu = wl_container_of(listener, menu, unmap);
    vela_node_set_enabled(&menu->tree->node, false);
    if (menu->server->seat->keyboard_state.focused_surface == menu->x11->surface) {
        vela_focus_refocus(menu->server);
    }
}

static void handle_unmanaged_associate(struct wl_listener *listener, void *data)
{
    struct unmanaged *menu = wl_container_of(listener, menu, associate);
    menu->surface_node = vela_surface_node_create(menu->tree, menu->x11->surface);
    listen(&menu->x11->surface->events.map, &menu->map, handle_unmanaged_map);
    listen(&menu->x11->surface->events.unmap, &menu->unmap, handle_unmanaged_unmap);
}

static void handle_unmanaged_dissociate(struct wl_listener *listener, void *data)
{
    struct unmanaged *menu = wl_container_of(listener, menu, dissociate);
    unlisten(&menu->map);
    unlisten(&menu->unmap);
    if (menu->surface_node) {
        vela_node_destroy(&menu->surface_node->node);
        menu->surface_node = NULL;
    }
}

static void handle_unmanaged_set_geometry(struct wl_listener *listener, void *data)
{
    struct unmanaged *menu = wl_container_of(listener, menu, set_geometry);
    vela_node_set_position(&menu->tree->node, menu->x11->x, menu->x11->y);
}

static void handle_unmanaged_request_configure(struct wl_listener *listener, void *data)
{
    struct unmanaged *menu = wl_container_of(listener, menu, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    wlr_xwayland_surface_configure(menu->x11, event->x, event->y, event->width, event->height);
}

static void handle_unmanaged_destroy(struct wl_listener *listener, void *data)
{
    struct unmanaged *menu = wl_container_of(listener, menu, destroy);
    unlisten(&menu->associate);
    unlisten(&menu->dissociate);
    unlisten(&menu->map);
    unlisten(&menu->unmap);
    unlisten(&menu->set_geometry);
    unlisten(&menu->request_configure);
    unlisten(&menu->destroy);
    if (menu->surface_node) {
        vela_node_destroy(&menu->surface_node->node);
    }
    vela_node_destroy(&menu->tree->node);
    free(menu);
}

static void unmanaged_create(struct vela_server *server, struct wlr_xwayland_surface *x11)
{
    struct unmanaged *menu = calloc(1, sizeof(*menu));
    menu->server = server;
    menu->x11 = x11;
    menu->tree = vela_tree_create(server->layers.x11_popups);
    vela_node_set_enabled(&menu->tree->node, false);
    wl_list_init(&menu->map.link);
    wl_list_init(&menu->unmap.link);
    listen(&x11->events.associate, &menu->associate, handle_unmanaged_associate);
    listen(&x11->events.dissociate, &menu->dissociate, handle_unmanaged_dissociate);
    listen(&x11->events.set_geometry, &menu->set_geometry, handle_unmanaged_set_geometry);
    listen(&x11->events.request_configure, &menu->request_configure, handle_unmanaged_request_configure);
    listen(&x11->events.destroy, &menu->destroy, handle_unmanaged_destroy);
}

// ---------------------------------------------------------- finestre X11 --

void vela_view_sync_x11(struct vela_view *view)
{
    struct wlr_xwayland_surface *x11 = view->x11;
    if (!x11 || !x11->surface) {
        return;
    }
    const struct wlr_box wanted = {
        .x = (int)lround(view->tree->node.x),
        .y = (int)lround(view->tree->node.y),
        .width = view->x11_width > 0 ? view->x11_width : x11->width,
        .height = view->x11_height > 0 ? view->x11_height : x11->height,
    };
    const struct wlr_box *sent = &view->x11_sent;
    if (wanted.width <= 0 || wanted.height <= 0
        || (wanted.x == sent->x && wanted.y == sent->y && wanted.width == sent->width
            && wanted.height == sent->height)) {
        return;
    }
    wlr_xwayland_surface_configure(x11, (int16_t)wanted.x, (int16_t)wanted.y, (uint16_t)wanted.width,
        (uint16_t)wanted.height);
    view->x11_sent = wanted;
}

static void handle_map(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, map);
    vela_view_update_decoration(view);
    // Le app X11 possono chiedere di partire massimizzate o a schermo
    // intero (giochi).
    if (view->x11->fullscreen) {
        vela_view_set_fullscreen(view, true);
    } else if (view->x11->maximized_horz || view->x11->maximized_vert) {
        vela_view_set_maximized(view, true, true);
    }
    vela_view_mapped(view);
}

static void handle_unmap(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, unmap);
    vela_view_unmapped(view);
}

static void handle_commit(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, commit);
    vela_view_committed(view);
}

// La superficie Wayland arriva dopo (associate) e può andarsene prima della
// finestra X11 (dissociate).
static void handle_associate(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, associate);
    struct wlr_surface *surface = view->x11->surface;
    view->surface_node = vela_surface_node_create(view->tree, surface);
    listen(&surface->events.map, &view->map, handle_map);
    listen(&surface->events.unmap, &view->unmap, handle_unmap);
    listen(&surface->events.commit, &view->commit, handle_commit);
}

static void handle_dissociate(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, dissociate);
    unlisten(&view->map);
    unlisten(&view->unmap);
    unlisten(&view->commit);
    if (view->surface_node) {
        vela_node_destroy(&view->surface_node->node);
        view->surface_node = NULL;
    }
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, destroy);
    vela_view_destroy(view);
}

static void handle_request_configure(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (!view->mapped) {
        // Prima di comparire l'app sceglie dove e quanto (ce lo si ricorda:
        // è la sua dimensione "normale").
        view->x11_initial = (struct wlr_box) { event->x, event->y, event->width, event->height };
        wlr_xwayland_surface_configure(view->x11, event->x, event->y, event->width, event->height);
        return;
    }
    if (view->maximized || view->fullscreen || !vela_snap_is_none(view->snap)) {
        view->x11_sent = (struct wlr_box) { 0 }; // decidiamo noi: si ripete la nostra geometria
        vela_view_sync_x11(view);
        return;
    }
    // Finestra libera: l'app può spostarsi e ridimensionarsi (giochi che
    // cambiano risoluzione, finestre di dialogo che si centrano), ma resta
    // dentro lo schermo dove vuole andare: Steam, per esempio, torna alla
    // posizione e alla dimensione che ricorda, anche se non ci stanno più.
    int bar = vela_view_title_bar_height(view);
    struct wlr_box frame = { event->x, event->y - bar, event->width, event->height + bar };
    struct vela_output *output
        = vela_output_at(view->server, frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
    if (!output) {
        output = vela_view_output(view);
    }
    if (output) {
        frame = vela_output_fit(output, frame);
    }
    vela_node_set_position(&view->tree->node, frame.x, frame.y + bar);
    view->x11_width = frame.width;
    view->x11_height = frame.height - bar;
    vela_view_sync_x11(view);
}

static void handle_request_move(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, request_move);
    vela_interact_begin(view->server, view, VELA_CURSOR_MOVE, 0, false);
}

static void handle_request_resize(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, request_resize);
    struct wlr_xwayland_resize_event *event = data;
    vela_interact_begin(view->server, view, VELA_CURSOR_RESIZE, event->edges, false);
}

static void handle_request_maximize(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, request_maximize);
    vela_view_set_maximized(view, view->x11->maximized_horz || view->x11->maximized_vert, true);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, request_fullscreen);
    vela_view_set_fullscreen(view, view->x11->fullscreen);
}

static void handle_request_minimize(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, request_minimize);
    struct wlr_xwayland_minimize_event *event = data;
    vela_view_set_minimized(view, event->minimize);
}

static void handle_request_activate(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, request_activate);
    vela_focus_new_view(view->server, view);
}

static void handle_set_title(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, set_title);
    vela_view_title_changed(view);
    vela_view_refresh_decoration(view);
}

static void handle_set_decorations(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, set_decorations);
    if (view->mapped) {
        vela_view_update_decoration(view);
    }
}

static void handle_set_class(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, set_app_id);
    vela_view_app_id_changed(view);
}

static void handle_set_parent(struct wl_listener *listener, void *data)
{
    struct vela_view *view = wl_container_of(listener, view, set_parent);
    vela_view_parent_changed(view);
}

static void view_create(struct vela_server *server, struct wlr_xwayland_surface *x11)
{
    struct vela_view *view = calloc(1, sizeof(*view));
    vela_view_init(view, server);
    view->x11 = x11;
    x11->data = view; // per risalire alla finestra genitore
    listen(&x11->events.associate, &view->associate, handle_associate);
    listen(&x11->events.dissociate, &view->dissociate, handle_dissociate);
    listen(&x11->events.destroy, &view->destroy, handle_destroy);
    listen(&x11->events.request_configure, &view->request_configure, handle_request_configure);
    listen(&x11->events.request_move, &view->request_move, handle_request_move);
    listen(&x11->events.request_resize, &view->request_resize, handle_request_resize);
    listen(&x11->events.request_maximize, &view->request_maximize, handle_request_maximize);
    listen(&x11->events.request_fullscreen, &view->request_fullscreen, handle_request_fullscreen);
    listen(&x11->events.request_minimize, &view->request_minimize, handle_request_minimize);
    listen(&x11->events.request_activate, &view->request_activate, handle_request_activate);
    listen(&x11->events.set_title, &view->set_title, handle_set_title);
    listen(&x11->events.set_decorations, &view->set_decorations, handle_set_decorations);
    listen(&x11->events.set_class, &view->set_app_id, handle_set_class);
    listen(&x11->events.set_parent, &view->set_parent, handle_set_parent);
}

// --------------------------------------------------------------- Xwayland --

static void handle_ready(struct wl_listener *listener, void *data)
{
    struct vela_server *server = wl_container_of(listener, server, xwayland_ready);
    wlr_xwayland_set_seat(server->xwayland, server->seat);
    // Il cursore delle finestre X11 finché l'app non ne sceglie uno.
    if (wlr_xcursor_manager_load(server->cursor_manager, 1.0f)) {
        struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(server->cursor_manager, "default", 1.0f);
        if (xcursor) {
            struct wlr_xcursor_image *image = xcursor->images[0];
            wlr_xwayland_set_cursor(server->xwayland, wlr_xcursor_image_get_buffer(image), (int32_t)image->hotspot_x,
                (int32_t)image->hotspot_y);
        }
    }
    wlr_log(WLR_INFO, "Xwayland ready on DISPLAY=%s", server->xwayland->display_name);
}

static void handle_new_surface(struct wl_listener *listener, void *data)
{
    struct vela_server *server = wl_container_of(listener, server, xwayland_new_surface);
    struct wlr_xwayland_surface *surface = data;
    if (surface->override_redirect) {
        unmanaged_create(server, surface);
    } else {
        view_create(server, surface);
    }
}

void vela_xwayland_init(struct vela_server *server)
{
    wl_list_init(&server->xwayland_ready.link);
    wl_list_init(&server->xwayland_new_surface.link);
    // Pigro: il server X parte al primo client, niente costo se non serve.
    server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
    if (!server->xwayland) {
        wlr_log(WLR_ERROR, "Xwayland not available: X11-only apps won't start");
        return;
    }
    listen(&server->xwayland->events.ready, &server->xwayland_ready, handle_ready);
    listen(&server->xwayland->events.new_surface, &server->xwayland_new_surface, handle_new_surface);
    wlr_log(WLR_INFO, "Xwayland on DISPLAY=%s (starts with the first X11 client)", server->xwayland->display_name);
}

void vela_xwayland_finish(struct vela_server *server)
{
    // Chiude le finestre X11 (e le loro viste) prima dei client Wayland.
    if (server->xwayland) {
        unlisten(&server->xwayland_ready);
        unlisten(&server->xwayland_new_surface);
        wlr_xwayland_destroy(server->xwayland);
        server->xwayland = NULL;
    }
}

void vela_xwayland_sync(struct vela_server *server)
{
    if (!server->xwayland) {
        return;
    }
    struct vela_view *view;
    wl_list_for_each (view, &server->views, link) {
        if (view->x11) {
            vela_view_sync_x11(view);
        }
    }
}

#else

void vela_view_sync_x11(struct vela_view *view)
{
}

void vela_xwayland_init(struct vela_server *server)
{
}

void vela_xwayland_finish(struct vela_server *server)
{
}

void vela_xwayland_sync(struct vela_server *server)
{
}

#endif
