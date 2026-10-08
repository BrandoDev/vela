// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SNAPSHOT_H
#define VELA_SNAPSHOT_H

// L'aspetto di una finestra "congelato": copie dei suoi buffer, che restano
// valide anche se l'app li cambia o si chiude. Le animazioni di chiusura, di
// riduzione a icona e di massimizzazione muovono e scalano l'istantanea,
// non la finestra vera (che è fatta di superfici annidate e non si può
// scalare).

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct vela_node;
struct vela_server;
struct vela_tree;
struct vela_view;

struct vela_snapshot;

// Copia i buffer sotto `source`, anche se è disabilitato (succede alla
// chiusura e con la finestra ridotta a icona). `frame` è il riquadro della
// finestra in coordinate globali: le trasformazioni si fanno sul suo centro.
struct vela_snapshot *vela_snapshot_create(struct vela_tree *parent, struct vela_node *source, struct wlr_box frame);
void vela_snapshot_destroy(struct vela_snapshot *snapshot);
// Disegna l'istantanea col centro del riquadro in (cx, cy), scalata (in
// larghezza e in altezza, per massimizzare e ripristinare) e sfumata.
void vela_snapshot_apply(struct vela_snapshot *snapshot, double cx, double cy, double scale_x, double scale_y,
    float opacity);

// ------------------------------------------------------------ animazioni --

enum vela_snapshot_kind {
    VELA_SNAPSHOT_CLOSE, // rimpicciolisce e sfuma
    VELA_SNAPSHOT_MINIMIZE, // vola verso il suo pulsante nella taskbar
    VELA_SNAPSHOT_RESTORE, // lo stesso volo, al contrario
    VELA_SNAPSHOT_MORPH, // si deforma fino al riquadro nuovo (vela_snapshot_morph)
};

// Le animazioni in corso stanno in vela_server.snapshot_animations.
void vela_snapshots_init(struct vela_server *server);
void vela_snapshots_finish(struct vela_server *server); // le chiude tutte, alla fine

// Parte un'animazione della finestra (la precedente si annulla). false se
// non parte (finestra senza dimensione).
bool vela_snapshot_animate(struct vela_server *server, struct vela_view *view, enum vela_snapshot_kind kind);
// Massimizza e ripristina: il contenuto di adesso si deforma fino a `to`
// (riquadro globale) e sfuma, mentre la finestra vera vi compare.
bool vela_snapshot_morph(struct vela_server *server, struct vela_view *view, struct wlr_box to);
void vela_snapshot_cancel(struct vela_server *server, struct vela_view *view);
// Avanza le animazioni all'istante `now_ms`; true se ne restano.
bool vela_snapshots_tick(struct vela_server *server, double now_ms);
bool vela_snapshots_running(const struct vela_server *server);

#endif
