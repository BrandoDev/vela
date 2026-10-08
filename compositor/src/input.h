// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_INPUT_H
#define VELA_INPUT_H

// Il seat e i dispositivi di input, come Windows 11:
//
// - mouse: velocità, "Migliora precisione puntatore" (accelerazione),
//   pulsante principale, righe per ogni scatto della rotellina;
// - touchpad: acceso o spento (anche solo con un mouse collegato),
//   velocità, tocco per cliccare (due dita: tasto destro), direzione dello
//   scorrimento, niente tocchi accidentali mentre si scrive;
// - gesti a tre e quattro dita: verso l'alto la Visualizzazione attività,
//   verso il basso il desktop, di lato cambia app (Alt+Tab, seguendo le
//   dita) o desktop virtuale. Gli altri gesti (pizzico, scorrimenti non
//   nostri) vanno alle app (pointer-gestures);
// - giochi e app che vogliono il mouse tutto per sé: movimenti relativi
//   (relative-pointer) e puntatore bloccato o confinato nella finestra
//   (pointer-constraints); un'app a fuoco può tenere per sé le scorciatoie
//   (keyboard-shortcuts-inhibit: macchine virtuali, desktop remoto);
// - appunti, trascinamento tra app (con l'icona che segue il cursore),
//   forma del cursore chiesta per nome.
//
// Le scelte in vela.conf; VELA_NATURAL_SCROLL=0 vince sulla direzione. Le
// tastiere sono in keyboard.h. Crea il seat e il cursore del server.

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct vela_server;
struct vela_tree;
struct vela_surface_node;
struct wlr_pointer_constraint_v1;
struct wlr_pointer_constraints_v1;
struct wlr_pointer_gestures_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_keyboard_shortcuts_inhibit_manager_v1;
struct wlr_surface;

enum vela_swipe_action {
    VELA_SWIPE_NONE,
    VELA_SWIPE_APP, // di lato: cambia app (Alt+Tab)
    VELA_SWIPE_DESKTOP, // di lato: cambia desktop virtuale
    VELA_SWIPE_OTHER, // un valore sconosciuto in vela.conf: solo su e giù
};

// Un'icona trascinata tra app: segue il cursore, spostata di quanto
// chiede l'app.
struct vela_drag_icon {
    struct vela_input *input;
    struct wlr_surface *surface;
    struct vela_tree *tree;
    struct vela_surface_node *node;
    double dx;
    double dy;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct vela_input {
    struct vela_server *server;
    struct wl_list pointers; // vela_pointer.link
    struct wl_list keyboards; // vela_keyboard.link

    // Da vela.conf.
    double wheel_factor; // righe per scatto / 3
    enum vela_swipe_action three_fingers;
    enum vela_swipe_action four_fingers;

    // Lo scorrimento a più dita in corso: nostro (un'azione) o dell'app.
    struct {
        bool ours;
        enum vela_swipe_action action;
        int fingers;
        double dx;
        double dy;
        int steps; // passi di "cambia app" già fatti
    } swipe;

    // Super premuto e rilasciato da solo: apre il menu Start.
    bool super_tap;
    // Il layout scritto nel log l'ultima volta (keyboard.c): solo i cambi.
    char keymap_logged[1024];

    // Presa implicita, come vuole Wayland: finché un tasto resta premuto,
    // il puntatore resta alla superficie su cui è stato premuto (anche fuori
    // da lei, anche su un altro schermo), che riceve così anche il rilascio.
    struct {
        struct wlr_surface *surface;
        double origin_x; // dove sta la sua origine nel layout
        double origin_y;
    } implicit_grab;

    struct vela_drag_icon *drag_icon;

    struct wlr_pointer_gestures_v1 *gestures;
    struct wlr_relative_pointer_manager_v1 *relative_pointers;
    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wlr_pointer_constraint_v1 *active_constraint;
    struct wlr_keyboard_shortcuts_inhibit_manager_v1 *shortcuts_inhibit;
    struct wl_event_source *paste_timer;

    struct wl_listener new_input;
    struct wl_listener new_virtual_pointer;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener new_constraint;
    struct wl_listener new_inhibitor;
    struct wl_listener request_set_shape;
    struct wl_listener request_set_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;
    struct wl_listener swipe_begin;
    struct wl_listener swipe_update;
    struct wl_listener swipe_end;
    struct wl_listener pinch_begin;
    struct wl_listener pinch_update;
    struct wl_listener pinch_end;
    struct wl_listener hold_begin;
    struct wl_listener hold_end;
};

// Crea seat, cursore e protocolli di input. VELA_DEBUG_INPUT=1 accende mouse
// e tastiere virtuali (per le prove: permettono a qualunque programma di
// simulare input).
struct vela_input *vela_input_create(struct vela_server *server);
// Prima di distruggere cursore, backend e display.
void vela_input_destroy(struct vela_input *input);

// Rilegge vela.conf ("reload-config"): layout e ripetizione delle
// tastiere, mouse e touchpad.
void vela_input_reload(struct vela_input *input);

bool vela_input_has_touchpad(const struct vela_input *input);
// L'app a fuoco tiene per sé le scorciatoie.
bool vela_input_shortcuts_inhibited(const struct vela_input *input);

// Il puntatore è su `surface` (NULL: su nessuna): il suo vincolo, se ne ha
// uno, diventa attivo.
void vela_input_constrain(struct vela_input *input, struct wlr_surface *surface);
// La tastiera a questa superficie (anche a un menu X11 che la chiede), con
// i tasti premuti e i modificatori di adesso.
void vela_input_keyboard_enter(struct vela_input *input, struct wlr_surface *surface);

// L'icona del trascinamento dove sta il cursore.
void vela_input_update_drag_icon(struct vela_input *input);

// Win+V: incolla nell'app a fuoco ciò che la shell ha appena messo negli
// appunti (Ctrl+V, Ctrl+Maiusc+V nei terminali), un attimo dopo che il
// pannello si è chiuso.
void vela_input_paste(struct vela_input *input);

#endif
