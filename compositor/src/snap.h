// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SNAP_H
#define VELA_SNAP_H

// Snap delle finestre, come Windows 11. Una finestra agganciata occupa un
// rettangolo dell'area utile in dodicesimi: metà e quarti col trascinamento
// contro i bordi e gli angoli (in alto la massimizza) e con Win+frecce;
// metà, terzi e quarti dai layout di snap che la shell mostra sotto il
// pulsante Ingrandisci (Win+Z).

#include <stdbool.h>
#include <stdint.h>

#include "motion.h"

struct vela_area;
struct vela_output;
struct vela_view;

// Un rettangolo dell'area utile in dodicesimi; tutto zero: non agganciata.
struct vela_snap {
    int x0;
    int y0;
    int x1;
    int y1;
};

static const struct vela_snap vela_snap_none = { 0, 0, 0, 0 };
static const struct vela_snap vela_snap_left = { 0, 0, 6, 12 };
static const struct vela_snap vela_snap_right = { 6, 0, 12, 12 };
static const struct vela_snap vela_snap_top_left = { 0, 0, 6, 6 };
static const struct vela_snap vela_snap_top_right = { 6, 0, 12, 6 };
static const struct vela_snap vela_snap_bottom_left = { 0, 6, 6, 12 };
static const struct vela_snap vela_snap_bottom_right = { 6, 6, 12, 12 };

static inline bool vela_snap_equal(struct vela_snap a, struct vela_snap b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

static inline bool vela_snap_is_none(struct vela_snap s)
{
    return vela_snap_equal(s, vela_snap_none);
}

static inline bool vela_snap_valid(struct vela_snap s)
{
    return s.x0 >= 0 && s.y0 >= 0 && s.x1 <= 12 && s.y1 <= 12 && s.x1 > s.x0 && s.y1 > s.y0;
}

// L'area di uno snap sullo schermo. I bordi si prendono in pixel fisici:
// le zone vicine si toccano senza fessure né sovrapposizioni a qualunque
// scala.
struct vela_area vela_snap_area(const struct vela_output *output, struct vela_snap snap);

// Aggancia (o sgancia, con vela_snap_none) la finestra; `output`: lo
// schermo, se non quello su cui sta.
void vela_view_set_snap(struct vela_view *view, struct vela_snap side, struct vela_output *output);
// La riallinea al suo snap su `output`.
void vela_view_apply_snap(struct vela_view *view, struct vela_output *output);
// Spostata altrove: non sta più col suo gruppo (un gruppo di una finestra
// sola non è più un gruppo).
void vela_view_leave_snap_group(struct vela_view *view);

// ------------------------------------------------- trascinare e la shell --
//
// Mentre si trascina una finestra contro un bordo, un'anteprima mostra dove
// finirà; dopo uno snap, Snap Assist della shell propone le altre finestre
// per gli spazi rimasti liberi; le finestre sistemate insieme fanno un
// gruppo che la taskbar riporta davanti insieme. I layout di snap si aprono
// con Win+Z o col mouse fermo sul pulsante Ingrandisci.

struct vela_rect_node;
struct vela_server;
struct wl_event_source;

enum vela_snap_zone {
    VELA_SNAP_ZONE_NONE,
    VELA_SNAP_ZONE_TILE, // con `tile`
    VELA_SNAP_ZONE_MAXIMIZE,
};

struct vela_snapping {
    // L'anteprima mentre si trascina: dove si aggancerà la finestra se la si
    // rilascia ora.
    enum vela_snap_zone zone;
    struct vela_snap tile;
    struct vela_output *output;
    struct vela_rect_node *rect; // sotto la finestra trascinata, o NULL
    double target_x, target_y, target_width, target_height; // l'area di arrivo
    struct vela_tween tween;
    // I layout di snap col mouse fermo su Ingrandisci.
    struct vela_view *layouts_hover;
    struct wl_event_source *layouts_timer;
    uint32_t next_group;
};

struct vela_snapping *vela_snapping_create(void);
void vela_snapping_destroy(struct vela_snapping *snapping);

// Win+frecce, come Windows 11.
void vela_snap_keyboard(struct vela_server *server, struct vela_view *view, uint32_t sym);
// Durante il trascinamento: l'anteprima secondo dove sta il cursore.
void vela_snap_update_zone(struct vela_server *server);
// L'anteprima che si accende (true finché si anima) e se c'è.
bool vela_snap_tick_preview(struct vela_server *server, double now_ms);
bool vela_snap_preview_shown(const struct vela_server *server);
// Fine del trascinamento; apply: la finestra si aggancia dove indica.
void vela_snap_end_zone(struct vela_server *server, bool apply);
// Dopo uno snap: Snap Assist propone le altre finestre negli spazi liberi.
void vela_snap_offer_assist(struct vela_server *server, struct vela_view *view);
// Snap Assist ha messo `view` accanto a `origin`: stesso gruppo.
void vela_snap_join_group(struct vela_server *server, struct vela_view *view, struct vela_view *origin);
// Il clic sul gruppo nella taskbar: tutte davanti, `view` a fuoco.
void vela_snap_activate_group(struct vela_server *server, struct vela_view *view);
// I layout di snap della finestra (keyboard: Win+Z).
void vela_snap_show_layouts(struct vela_server *server, struct vela_view *view, bool keyboard);
// Il mouse su Ingrandisci di `view` (NULL: altrove).
void vela_snap_hover_maximize(struct vela_server *server, struct vela_view *view);
void vela_snap_forget(struct vela_server *server, struct vela_view *view);

#endif
