// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_INTERACT_H
#define VELA_INTERACT_H

// Il puntatore sulle finestre, come Windows 11: il focus del puntatore e la
// presa implicita, spostare e ridimensionare (dalla barra di Vela, dai bordi
// invisibili attorno alle finestre, con Super + trascinamento, o quando
// l'app lo chiede), i pulsanti e il doppio clic della barra del titolo; e
// "Sposta"/"Ridimensiona" da tastiera (frecce, con Ctrl di un'unità; Invio
// conferma, Esc annulla; anche il mouse muove).
//
// La finestra presa e la modalità stanno in vela_server (grabbed,
// cursor_mode); il resto qui.

#include <stdbool.h>
#include <stdint.h>
#include <wlr/util/box.h>

struct vela_server;
struct vela_view;
struct wlr_pointer_button_event;

struct vela_interaction {
    // Il punto preso, rispetto all'origine della finestra (spostare) o al
    // bordo che si muove (ridimensionare).
    double grab_x;
    double grab_y;
    struct wlr_box grab_box; // il riquadro all'inizio del ridimensionamento
    uint32_t resize_edges;
    bool modifier_grab; // la pressione non è arrivata all'app: nemmeno il rilascio
    // La barra del titolo di Vela sotto il mouse, e gli ultimi clic sul
    // titolo e sull'icona (per il doppio clic).
    struct vela_view *hovered_decoration;
    struct {
        struct vela_view *view;
        uint32_t time_msec;
    } last_title_click, last_icon_click;
    // Il titolo premuto: il trascinamento parte solo se il mouse si muove.
    struct {
        struct vela_view *view;
        double x;
        double y;
    } pending_title_drag;
    // "Sposta"/"Ridimensiona" da tastiera.
    struct {
        struct vela_view *view;
        int mode; // enum vela_cursor_mode
        bool edge_chosen; // ridimensionare: il primo tasto freccia sceglie il bordo
        double tree_x; // com'era, per Esc
        double tree_y;
        struct wlr_box geometry;
    } keyboard;
};

struct vela_interaction *vela_interaction_create(void);
void vela_interaction_destroy(struct vela_interaction *interaction);

// Il cursore si è mosso (o ciò che ha sotto è cambiato).
void vela_interact_motion(struct vela_server *server, uint32_t time_msec);
void vela_interact_button(struct vela_server *server, struct wlr_pointer_button_event *event);
// Comincia a spostare o ridimensionare (`edges`: WLR_EDGE_*). Le richieste
// delle app valgono solo dalla finestra sotto il puntatore
// (from_modifier: false).
void vela_interact_begin(struct vela_server *server, struct vela_view *view, int mode, uint32_t edges,
    bool from_modifier);

// "Sposta"/"Ridimensiona" da tastiera (mode: enum vela_cursor_mode).
void vela_interact_begin_keyboard(struct vela_server *server, struct vela_view *view, int mode);
void vela_interact_finish_keyboard(struct vela_server *server, bool confirm);
// Se è in corso, i tasti sono suoi (true) e le pressioni muovono la finestra.
bool vela_interact_keyboard(struct vela_server *server, const uint32_t *syms, int count, uint32_t modifiers,
    bool pressed);

// La finestra sparisce: non la si ricorda più.
void vela_interact_forget(struct vela_server *server, struct vela_view *view);

#endif
