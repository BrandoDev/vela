// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "interact.h"

#include "a11y.h"
#include "bindings.h"
#include "decoration.h"
#include "focus.h"
#include "input.h"
#include "layer.h"
#include "lock.h"
#include "scene/scene.h"
#include "server.h"
#include "snap.h"
#include "util.h"
#include "view.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <xkbcommon/xkbcommon.h>

#define BORDER_BAND 8.0 // fuori dalla finestra, come in Windows 11
#define BORDER_INNER 4.0 // in alto anche dentro la barra del titolo
#define BORDER_CORNER 16.0 // gli angoli prendono anche un tratto dei lati
#define DOUBLE_CLICK_MS 400

struct vela_interaction *vela_interaction_create(void)
{
    return calloc(1, sizeof(struct vela_interaction));
}

void vela_interaction_destroy(struct vela_interaction *interaction)
{
    free(interaction);
}

static struct vela_view *view_of(struct vela_owner *owner)
{
    return owner && owner->kind == VELA_OWNER_VIEW ? (struct vela_view *)owner : NULL;
}

static const char *resize_cursor(uint32_t edges)
{
    bool top = edges & WLR_EDGE_TOP;
    bool bottom = edges & WLR_EDGE_BOTTOM;
    bool left = edges & WLR_EDGE_LEFT;
    bool right = edges & WLR_EDGE_RIGHT;
    if (top) {
        return left ? "nw-resize" : right ? "ne-resize" : "n-resize";
    }
    if (bottom) {
        return left ? "sw-resize" : right ? "se-resize" : "s-resize";
    }
    return left ? "w-resize" : "e-resize";
}

// I bordi invisibili per ridimensionare le finestre con la barra di Vela,
// come in Windows 11: la finestra e i bordi (WLR_EDGE_*) sotto il punto, o
// NULL.
static struct vela_view *resize_border_at(struct vela_server *server, double lx, double ly, uint32_t *edges)
{
    *edges = 0;
    // Sopra le finestre (taskbar, menu, pannelli): niente bordi.
    struct vela_hit hit = vela_scene_at(server->scene, lx, ly);
    struct vela_owner *owner = hit.owner;
    if (owner && owner->kind == VELA_OWNER_LAYER) {
        uint32_t layer = ((struct vela_layer_surface *)owner)->layer;
        if (layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP || layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
            return NULL;
        }
    }
    // Dalla finestra più in alto: la prima che copre il punto vince.
    struct vela_node *child;
    wl_list_for_each_reverse (child, &server->layers.windows->children, link) {
        struct vela_view *view = view_of(child->data);
        if (!view || !child->enabled || !view->mapped || view->minimized) {
            continue;
        }
        struct wlr_box f = vela_view_frame_box(view);
        bool resizable = view->decoration && !view->maximized && !view->fullscreen && vela_view_resizable(view);
        bool inside = lx >= f.x && lx < f.x + f.width && ly >= f.y && ly < f.y + f.height;
        if (inside && !(resizable && ly < f.y + BORDER_INNER)) {
            return NULL; // la finestra copre il punto
        }
        if (!resizable || lx < f.x - BORDER_BAND || lx >= f.x + f.width + BORDER_BAND || ly < f.y - BORDER_BAND
            || ly >= f.y + f.height + BORDER_BAND) {
            continue;
        }
        bool left = lx < f.x;
        bool right = lx >= f.x + f.width;
        bool top = ly < f.y + (inside ? BORDER_INNER : 0.0);
        bool bottom = ly >= f.y + f.height;
        if (top || bottom) {
            left = left || lx < f.x + BORDER_CORNER;
            right = right || (!left && lx >= f.x + f.width - BORDER_CORNER);
        }
        if (left || right) {
            top = top || ly < f.y + BORDER_CORNER;
            bottom = bottom || (!top && ly >= f.y + f.height - BORDER_CORNER);
        }
        *edges = (top ? WLR_EDGE_TOP : 0) | (bottom ? WLR_EDGE_BOTTOM : 0) | (left ? WLR_EDGE_LEFT : 0)
            | (right ? WLR_EDGE_RIGHT : 0);
        return *edges ? view : NULL;
    }
    return NULL;
}

static void unhover_decoration(struct vela_interaction *interaction)
{
    struct vela_view *hovered = interaction->hovered_decoration;
    if (hovered && hovered->decoration) {
        vela_decoration_set_hover(hovered->decoration, VELA_DECORATION_NONE);
    }
}

void vela_interact_motion(struct vela_server *server, uint32_t time_msec)
{
    struct vela_interaction *interaction = server->interaction;
    struct wlr_cursor *cursor = server->cursor;
    struct wlr_seat *seat = server->seat;
    struct vela_input *input = server->input;
    vela_input_update_drag_icon(input);
    struct vela_a11y *a11y = server->a11y;
    if (a11y->magnifier || a11y->zoom_animating || a11y->zoom > 1.0) {
        vela_a11y_update_magnifier(a11y); // la zona ingrandita segue il cursore
    }
    struct vela_view *dragged = interaction->pending_title_drag.view;
    if (dragged
        && hypot(cursor->x - interaction->pending_title_drag.x, cursor->y - interaction->pending_title_drag.y) > 4.0) {
        interaction->pending_title_drag.view = NULL;
        vela_interact_begin(server, dragged, VELA_CURSOR_MOVE, 0, true);
    }
    struct vela_view *grabbed = server->grabbed;
    if (server->cursor_mode == VELA_CURSOR_MOVE && grabbed) {
        // Posizione esatta, anche frazionaria: al disegno la finestra si
        // aggancia al pixel fisico più vicino (§3.4). Con posizioni logiche
        // intere, al 125% la finestra avanzerebbe a scatti di 1 e 2 pixel.
        vela_node_set_position(&grabbed->tree->node, cursor->x - interaction->grab_x, cursor->y - interaction->grab_y);
        vela_snap_update_zone(server);
        return;
    }
    if (server->cursor_mode == VELA_CURSOR_RESIZE && grabbed) {
        double border_x = cursor->x - interaction->grab_x;
        double border_y = cursor->y - interaction->grab_y;
        const struct wlr_box *box = &interaction->grab_box;
        int left = box->x;
        int right = box->x + box->width;
        int top = box->y;
        int bottom = box->y + box->height;
        uint32_t edges = interaction->resize_edges;
        if (edges & WLR_EDGE_TOP) {
            top = vela_min((int)border_y, bottom - 1);
        } else if (edges & WLR_EDGE_BOTTOM) {
            bottom = vela_max((int)border_y, top + 1);
        }
        if (edges & WLR_EDGE_LEFT) {
            left = vela_min((int)border_x, right - 1);
        } else if (edges & WLR_EDGE_RIGHT) {
            right = vela_max((int)border_x, left + 1);
        }
        struct wlr_box geometry = vela_view_geometry(grabbed);
        vela_node_set_position(&grabbed->tree->node, left - geometry.x, top - geometry.y);
        vela_view_configure_size(grabbed, right - left, bottom - top);
        return;
    }

    // Un tasto premuto su una superficie: il movimento resta suo finché non
    // lo si rilascia (la selezione a riquadro che esce dallo schermo, una
    // barra di scorrimento trascinata fuori dalla finestra).
    if (input->implicit_grab.surface && !seat->drag) {
        if (seat->pointer_state.button_count > 0 && seat->pointer_state.focused_surface == input->implicit_grab.surface) {
            wlr_seat_pointer_notify_motion(seat, time_msec, cursor->x - input->implicit_grab.origin_x,
                cursor->y - input->implicit_grab.origin_y);
            return;
        }
        input->implicit_grab.surface = NULL;
        input->implicit_grab.origin_x = 0.0;
        input->implicit_grab.origin_y = 0.0;
    }

    // Sul bordo di una finestra con la barra di Vela: le frecce per ridimensionare.
    if (server->cursor_mode == VELA_CURSOR_PASSTHROUGH && seat->pointer_state.button_count == 0 && !seat->drag) {
        uint32_t edges = 0;
        if (resize_border_at(server, cursor->x, cursor->y, &edges)) {
            unhover_decoration(interaction);
            interaction->hovered_decoration = NULL;
            wlr_seat_pointer_clear_focus(seat);
            vela_input_constrain(input, NULL);
            wlr_cursor_set_xcursor(cursor, server->cursor_manager, resize_cursor(edges));
            return;
        }
    }

    struct vela_hit hit = vela_scene_at(server->scene, cursor->x, cursor->y);
    // Sopra la barra del titolo di Vela: i pulsanti si illuminano.
    struct vela_view *decorated = hit.surface ? NULL : view_of(hit.owner);
    if (decorated && !decorated->decoration) {
        decorated = NULL;
    }
    if (interaction->hovered_decoration != decorated) {
        unhover_decoration(interaction);
    }
    interaction->hovered_decoration = decorated;
    if (decorated) {
        enum vela_decoration_part part = vela_decoration_part_at(decorated->decoration, cursor->x, cursor->y);
        vela_decoration_set_hover(decorated->decoration, part);
        vela_snap_hover_maximize(server, part == VELA_DECORATION_MAXIMIZE ? decorated : NULL);
    } else {
        vela_snap_hover_maximize(server, NULL);
    }
    if (!hit.surface) {
        wlr_cursor_set_xcursor(cursor, server->cursor_manager, "default");
        wlr_seat_pointer_clear_focus(seat);
        vela_input_constrain(input, NULL);
        return;
    }
    wlr_seat_pointer_notify_enter(seat, hit.surface, hit.sx, hit.sy);
    wlr_seat_pointer_notify_motion(seat, time_msec, hit.sx, hit.sy);
    vela_input_constrain(input, hit.surface);
}

static void decoration_press(struct vela_server *server, struct vela_view *view, uint32_t time_msec)
{
    struct vela_interaction *interaction = server->interaction;
    struct wlr_cursor *cursor = server->cursor;
    vela_focus_view(server, view);
    switch (vela_decoration_part_at(view->decoration, cursor->x, cursor->y)) {
    case VELA_DECORATION_CLOSE:
        vela_view_close(view);
        return;
    case VELA_DECORATION_MAXIMIZE:
        vela_view_set_maximized(view, !view->maximized, true);
        return;
    case VELA_DECORATION_MINIMIZE:
        vela_view_set_minimized(view, true);
        return;
    case VELA_DECORATION_ICON: {
        // Come Windows: un clic sull'icona apre il menu della finestra, un
        // doppio clic la chiude.
        bool double_click = interaction->last_icon_click.view == view
            && time_msec - interaction->last_icon_click.time_msec < DOUBLE_CLICK_MS;
        interaction->last_icon_click.view = view;
        interaction->last_icon_click.time_msec = time_msec;
        if (double_click) {
            interaction->last_icon_click.view = NULL;
            interaction->last_icon_click.time_msec = 0;
            vela_view_close(view);
            return;
        }
        struct wlr_box frame = vela_view_frame_box(view);
        vela_window_menu_show(server, view, frame.x + VELA_DECORATION_ICON_X - 4, frame.y + VELA_DECORATION_HEIGHT,
            false);
        return;
    }
    case VELA_DECORATION_TITLE: {
        // Doppio clic: massimizza o ripristina, come su Windows.
        bool double_click = interaction->last_title_click.view == view
            && time_msec - interaction->last_title_click.time_msec < DOUBLE_CLICK_MS;
        interaction->last_title_click.view = view;
        interaction->last_title_click.time_msec = time_msec;
        if (double_click) {
            interaction->last_title_click.view = NULL;
            interaction->last_title_click.time_msec = 0;
            vela_view_set_maximized(view, !view->maximized, true);
            return;
        }
        // Il trascinamento parte solo se il mouse si muove davvero: un clic
        // (o il primo di un doppio clic) non deve ripristinare una finestra
        // massimizzata.
        interaction->pending_title_drag.view = view;
        interaction->pending_title_drag.x = cursor->x;
        interaction->pending_title_drag.y = cursor->y;
        interaction->modifier_grab = true; // il rilascio non va all'app
        return;
    }
    case VELA_DECORATION_NONE:
        return;
    }
}

void vela_interact_button(struct vela_server *server, struct wlr_pointer_button_event *event)
{
    struct vela_interaction *interaction = server->interaction;
    struct wlr_cursor *cursor = server->cursor;
    struct wlr_seat *seat = server->seat;
    struct vela_input *input = server->input;
    bool pressed = event->state == WL_POINTER_BUTTON_STATE_PRESSED;
    input->super_tap = false;
    vela_lock_note_activity(server->lock);
    // Bloccato: il clic dà la tastiera alla schermata di blocco sotto il
    // mouse (con più schermi) e arriva solo a lei.
    if (server->locked) {
        wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
        if (pressed) {
            struct vela_hit hit = vela_scene_at(server->scene, cursor->x, cursor->y);
            if (hit.surface) {
                vela_input_keyboard_enter(input, hit.surface);
            }
        }
        return;
    }

    // "Sposta" o "Ridimensiona" da tastiera in corso: un clic conferma.
    if (interaction->keyboard.view && pressed) {
        vela_interact_finish_keyboard(server, true);
        interaction->modifier_grab = true; // nemmeno il rilascio arriva all'app
        return;
    }

    // Sul bordo di una finestra con la barra di Vela: si ridimensiona.
    if (pressed && event->button == BTN_LEFT && server->cursor_mode == VELA_CURSOR_PASSTHROUGH
        && seat->pointer_state.button_count == 0) {
        uint32_t edges = 0;
        struct vela_view *view = resize_border_at(server, cursor->x, cursor->y, &edges);
        if (view) {
            vela_focus_view(server, view);
            vela_interact_begin(server, view, VELA_CURSOR_RESIZE, edges, true);
            if (server->cursor_mode != VELA_CURSOR_PASSTHROUGH) {
                interaction->modifier_grab = true; // nemmeno il rilascio arriva all'app
                return;
            }
        }
    }

    // Super + trascinamento sposta la finestra, Super + tasto destro la
    // ridimensiona (dall'angolo più vicino), come in KDE. Serve anche alle
    // finestre X11 senza barra del titolo propria. Il clic non arriva
    // all'app.
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    bool super = keyboard && (wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_LOGO);
    if (pressed && super && server->cursor_mode == VELA_CURSOR_PASSTHROUGH
        && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
        struct vela_view *view = view_of(vela_scene_at(server->scene, cursor->x, cursor->y).owner);
        if (view) {
            vela_focus_view(server, view);
            uint32_t edges = 0;
            if (event->button == BTN_RIGHT) {
                struct wlr_box frame = vela_view_frame_box(view);
                edges |= cursor->x < frame.x + frame.width / 2.0 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
                edges |= cursor->y < frame.y + frame.height / 2.0 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
            }
            vela_interact_begin(server, view, event->button == BTN_LEFT ? VELA_CURSOR_MOVE : VELA_CURSOR_RESIZE,
                edges, true);
            if (server->cursor_mode != VELA_CURSOR_PASSTHROUGH) {
                interaction->modifier_grab = true;
                return;
            }
        }
    }
    if (interaction->modifier_grab && !pressed) {
        interaction->modifier_grab = false; // la pressione non era arrivata all'app
        interaction->pending_title_drag.view = NULL;
        interaction->pending_title_drag.x = 0.0;
        interaction->pending_title_drag.y = 0.0;
    } else {
        // Il primo tasto premuto su una superficie la "prende" (vedi
        // vela_input.implicit_grab).
        if (pressed && seat->pointer_state.button_count == 0 && seat->pointer_state.focused_surface && !seat->drag) {
            input->implicit_grab.surface = seat->pointer_state.focused_surface;
            input->implicit_grab.origin_x = cursor->x - seat->pointer_state.sx;
            input->implicit_grab.origin_y = cursor->y - seat->pointer_state.sy;
        }
        wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
    }

    if (!pressed) {
        // Rilasciati tutti i tasti: il puntatore torna a ciò che ha sotto.
        if (input->implicit_grab.surface && seat->pointer_state.button_count == 0) {
            input->implicit_grab.surface = NULL;
            input->implicit_grab.origin_x = 0.0;
            input->implicit_grab.origin_y = 0.0;
            if (server->cursor_mode == VELA_CURSOR_PASSTHROUGH) {
                vela_interact_motion(server, event->time_msec);
            }
        }
        if (server->cursor_mode != VELA_CURSOR_PASSTHROUGH) {
            if (server->cursor_mode == VELA_CURSOR_MOVE) {
                vela_snap_end_zone(server, true); // rilasciata su un bordo: si aggancia
            }
            server->cursor_mode = VELA_CURSOR_PASSTHROUGH;
            server->grabbed = NULL;
            vela_interact_motion(server, event->time_msec);
        }
        return;
    }

    struct vela_hit hit = vela_scene_at(server->scene, cursor->x, cursor->y);
    struct vela_owner *owner = hit.owner;
    if (!owner) {
        return;
    }
    // La barra del titolo di Vela: pulsanti, trascinamento, doppio clic;
    // col tasto destro il menu della finestra.
    struct vela_view *view = view_of(owner);
    if (view && !hit.surface && view->decoration) {
        if (event->button == BTN_LEFT) {
            decoration_press(server, view, event->time_msec);
            return;
        }
        if (event->button == BTN_RIGHT
            && vela_decoration_part_at(view->decoration, cursor->x, cursor->y) == VELA_DECORATION_TITLE) {
            vela_focus_view(server, view);
            vela_window_menu_show(server, view, cursor->x, cursor->y, false);
            return;
        }
    }
    if (view) {
        vela_focus_view(server, view);
    } else {
        struct vela_layer_surface *layer = (struct vela_layer_surface *)owner;
        if (vela_layer_surface_wants_keyboard(layer)) {
            vela_focus_layer(server, layer);
        }
    }
}

void vela_interact_begin(struct vela_server *server, struct vela_view *view, int mode, uint32_t edges,
    bool from_modifier)
{
    struct vela_interaction *interaction = server->interaction;
    struct wlr_cursor *cursor = server->cursor;
    // Accetta la richiesta di un'app solo dalla finestra su cui si trova il
    // puntatore.
    struct wlr_surface *focused = server->seat->pointer_state.focused_surface;
    if (!from_modifier && (!focused || wlr_surface_get_root_surface(focused) != vela_view_surface(view))) {
        return;
    }
    if (view->fullscreen) {
        return;
    }
    vela_view_finish_open_animation(view);

    // Trascinare una finestra massimizzata o agganciata la ripristina sotto
    // il cursore, mantenendo il punto afferrato alla stessa proporzione e la
    // barra del titolo sotto il cursore (come Windows).
    if ((view->maximized || !vela_snap_is_none(view->snap)) && mode == VELA_CURSOR_MOVE) {
        struct wlr_box frame = vela_view_frame_box(view);
        double fraction = frame.width > 0 ? (cursor->x - frame.x) / frame.width : 0.5;
        int restored_width = vela_view_restore_box(view).width;
        if (view->maximized) {
            vela_view_set_maximized(view, false, false);
        } else {
            vela_view_set_snap(view, vela_snap_none, NULL);
        }
        struct wlr_box geometry = vela_view_geometry(view);
        vela_node_set_position(&view->tree->node, (int)(cursor->x - fraction * restored_width) - geometry.x,
            frame.y - geometry.y);
    }
    // Ridimensionare una finestra agganciata la sgancia, lasciandola dov'è.
    if (!vela_snap_is_none(view->snap) && mode == VELA_CURSOR_RESIZE) {
        view->snap = vela_snap_none;
        vela_view_send_tiled(view, WLR_EDGE_NONE);
    }

    server->grabbed = view;
    server->cursor_mode = mode;
    if (mode == VELA_CURSOR_MOVE) {
        interaction->grab_x = cursor->x - view->tree->node.x;
        interaction->grab_y = cursor->y - view->tree->node.y;
        return;
    }
    struct wlr_box frame = vela_view_frame_box(view);
    double border_x = frame.x + ((edges & WLR_EDGE_RIGHT) ? frame.width : 0);
    double border_y = frame.y + ((edges & WLR_EDGE_BOTTOM) ? frame.height : 0);
    interaction->grab_x = cursor->x - border_x;
    interaction->grab_y = cursor->y - border_y;
    interaction->grab_box = frame;
    interaction->resize_edges = edges;
}

// ------------------------------------------------------------ da tastiera --

void vela_interact_begin_keyboard(struct vela_server *server, struct vela_view *view, int mode)
{
    struct vela_interaction *interaction = server->interaction;
    struct wlr_cursor *cursor = server->cursor;
    if (view->minimized || view->maximized || view->fullscreen || server->cursor_mode != VELA_CURSOR_PASSTHROUGH) {
        return;
    }
    if (!vela_snap_is_none(view->snap)) {
        vela_view_set_snap(view, vela_snap_none, NULL);
    }
    vela_focus_view(server, view);
    vela_view_finish_open_animation(view);
    struct wlr_box frame = vela_view_frame_box(view);
    interaction->keyboard.view = view;
    interaction->keyboard.mode = mode;
    interaction->keyboard.edge_chosen = false;
    interaction->keyboard.tree_x = view->tree->node.x;
    interaction->keyboard.tree_y = view->tree->node.y;
    interaction->keyboard.geometry = vela_view_geometry(view);
    // Come Windows: il puntatore va sulla barra del titolo (spostare) o al
    // centro della finestra (ridimensionare), e da lì la segue.
    if (mode == VELA_CURSOR_MOVE) {
        wlr_cursor_warp(cursor, NULL, frame.x + frame.width / 2.0, frame.y + vela_min(16, frame.height / 2));
        vela_interact_begin(server, view, VELA_CURSOR_MOVE, 0, true);
    } else {
        wlr_cursor_warp(cursor, NULL, frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
    }
    wlr_seat_pointer_clear_focus(server->seat);
    wlr_cursor_set_xcursor(cursor, server->cursor_manager, mode == VELA_CURSOR_MOVE ? "move" : "all-scroll");
}

static void keyboard_key(struct vela_server *server, xkb_keysym_t sym, uint32_t modifiers)
{
    struct vela_interaction *interaction = server->interaction;
    struct vela_view *view = interaction->keyboard.view;
    double step = (modifiers & WLR_MODIFIER_CTRL) ? 1.0 : 10.0;
    double dx = 0.0;
    double dy = 0.0;
    uint32_t edge = 0;
    switch (sym) {
    case XKB_KEY_Left:
        dx = -step;
        edge = WLR_EDGE_LEFT;
        break;
    case XKB_KEY_Right:
        dx = step;
        edge = WLR_EDGE_RIGHT;
        break;
    case XKB_KEY_Up:
        dy = -step;
        edge = WLR_EDGE_TOP;
        break;
    case XKB_KEY_Down:
        dy = step;
        edge = WLR_EDGE_BOTTOM;
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        vela_interact_finish_keyboard(server, true);
        return;
    case XKB_KEY_Escape:
        vela_interact_finish_keyboard(server, false);
        return;
    default:
        return;
    }
    if (interaction->keyboard.mode == VELA_CURSOR_RESIZE && !interaction->keyboard.edge_chosen) {
        // Il primo tasto freccia sceglie il bordo da muovere.
        struct wlr_box frame = vela_view_frame_box(view);
        double x = edge == WLR_EDGE_LEFT ? frame.x
            : edge == WLR_EDGE_RIGHT     ? frame.x + frame.width
                                         : frame.x + frame.width / 2.0;
        double y = edge == WLR_EDGE_TOP ? frame.y
            : edge == WLR_EDGE_BOTTOM   ? frame.y + frame.height
                                        : frame.y + frame.height / 2.0;
        wlr_cursor_warp(server->cursor, NULL, x, y);
        vela_interact_begin(server, view, VELA_CURSOR_RESIZE, edge, true);
        interaction->keyboard.edge_chosen = true;
        return;
    }
    wlr_cursor_move(server->cursor, NULL, dx, dy);
    vela_interact_motion(server, 0);
}

bool vela_interact_keyboard(struct vela_server *server, const uint32_t *syms, int count, uint32_t modifiers,
    bool pressed)
{
    if (!server->interaction->keyboard.view) {
        return false;
    }
    for (int i = 0; pressed && i < count && server->interaction->keyboard.view; ++i) {
        keyboard_key(server, syms[i], modifiers);
    }
    return true;
}

void vela_interact_finish_keyboard(struct vela_server *server, bool confirm)
{
    struct vela_interaction *interaction = server->interaction;
    struct vela_view *view = interaction->keyboard.view;
    if (!view) {
        return;
    }
    if (!confirm) {
        // Esc: torna com'era.
        vela_node_set_position(&view->tree->node, interaction->keyboard.tree_x, interaction->keyboard.tree_y);
        if (interaction->keyboard.mode == VELA_CURSOR_RESIZE && interaction->keyboard.edge_chosen) {
            vela_view_configure_size(view, interaction->keyboard.geometry.width,
                interaction->keyboard.geometry.height);
        }
    }
    vela_snap_end_zone(server, false);
    interaction->keyboard.view = NULL;
    interaction->keyboard.mode = VELA_CURSOR_PASSTHROUGH;
    interaction->keyboard.edge_chosen = false;
    interaction->keyboard.tree_x = 0.0;
    interaction->keyboard.tree_y = 0.0;
    interaction->keyboard.geometry = (struct wlr_box) { 0 };
    server->cursor_mode = VELA_CURSOR_PASSTHROUGH;
    server->grabbed = NULL;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
    vela_interact_motion(server, 0);
}

void vela_interact_forget(struct vela_server *server, struct vela_view *view)
{
    struct vela_interaction *interaction = server->interaction;
    if (interaction->hovered_decoration == view) {
        interaction->hovered_decoration = NULL;
    }
    if (interaction->last_title_click.view == view) {
        interaction->last_title_click.view = NULL;
        interaction->last_title_click.time_msec = 0;
    }
    if (interaction->last_icon_click.view == view) {
        interaction->last_icon_click.view = NULL;
        interaction->last_icon_click.time_msec = 0;
    }
    if (interaction->pending_title_drag.view == view) {
        interaction->pending_title_drag.view = NULL;
        interaction->pending_title_drag.x = 0.0;
        interaction->pending_title_drag.y = 0.0;
    }
    if (interaction->keyboard.view == view) {
        struct wlr_box none = { 0 };
        interaction->keyboard.view = NULL;
        interaction->keyboard.mode = VELA_CURSOR_PASSTHROUGH;
        interaction->keyboard.edge_chosen = false;
        interaction->keyboard.tree_x = 0.0;
        interaction->keyboard.tree_y = 0.0;
        interaction->keyboard.geometry = none;
    }
}
