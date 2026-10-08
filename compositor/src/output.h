// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_OUTPUT_H
#define VELA_OUTPUT_H

// Uno schermo: dove sta nel layout, l'area libera dai pannelli, il suo
// ciclo dei frame (docs/renderer.md §4.3), il vblank virtuale degli schermi
// headless, il VRR, l'accensione e lo spegnimento.
//
// Nasce quando il backend annuncia un wlr_output (vela_output_create) e
// muore con lui: il destroy di wlroots libera tutto, frame della scena
// compreso.

#include "frame_clock.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct vela_nested;
struct vela_output_frame;
struct vela_server;
struct wlr_output;
struct wlr_output_mode;
struct wlr_output_state;

// Un rettangolo logico i cui bordi cadono esattamente su pixel fisici di uno
// schermo (docs/renderer.md §3.5): la posizione può essere frazionaria.
struct vela_area {
    double x, y, width, height;
};

// Dove mettere una finestra perché copra un'area: posizione logica esatta e
// dimensione intera (quella che il client riceve nel configure).
struct vela_placement {
    double x, y;
    int width, height;
};

#define VELA_OUTPUT_DELIVERIES 16

// Un frame consegnato, in attesa della misura del suo costo.
struct vela_delivery {
    uint32_t seq;
    int64_t start; // quando doveva cominciare il disegno
    int64_t woke_at; // quando è cominciato davvero
    int64_t committed_at;
    uint64_t point; // lavoro della GPU; 0: nessuno (scanout, solo cursore)
    int timing_slot;
};

struct vela_output {
    struct wl_list link; // vela_server.outputs
    struct vela_server *server;
    struct wlr_output *wlr;
    struct wlr_box usable; // area libera da pannelli (coordinate globali)
    bool powered;
    // Dove stava nel layout prima di essere spento (Impostazioni, wlr-randr):
    // lo si annuncia ancora, così chi lo riaccende lo rimette lì e non sopra
    // un altro schermo in (0, 0).
    bool has_last_position;
    int last_x, last_y;
    // Solo se Vela gira in una finestra dentro un'altra sessione.
    struct vela_nested *nested;
    // Dalla scena ai pixel di questo schermo (scene/frame.h).
    struct vela_output_frame *frame;
    struct vela_frame_clock clock;

    // Il resto è il ciclo dei frame, privato di output.c.
    struct wlr_output_mode *mode_before_off;
    int custom_before_off[3]; // larghezza, altezza, mHz (schermi senza modalità)
    bool latching; // VELA_LATCH=0: si disegna subito, come prima di S3
    bool frame_requested;
    struct wl_event_source *idle_frame; // "frame" subito, se lo schermo era fermo
    int latch_fd;
    struct wl_event_source *latch_source;
    bool latch_armed;
    struct vela_frame_plan plan;
    bool planned;
    struct vela_delivery deliveries[VELA_OUTPUT_DELIVERIES];
    int delivery_count;
    // Di cosa è fatto il costo (VELA_STATS): risveglio in ritardo, CPU fino
    // al commit, attesa prima che la GPU cominci, lavoro della GPU.
    double breakdown_sum[4];
    double breakdown_max[4];
    int breakdown_count;
    bool vrr_unsupported;
    // Schermo headless: un vblank virtuale esatto al nanosecondo.
    int vblank_fd;
    struct wl_event_source *vblank_source;
    int64_t last_vblank;
    bool vblank_armed;
    bool awaiting_present; // un frame è stato consegnato e aspetta il vblank
    struct vela_delivery awaiting;

    struct wl_listener frame_event;
    struct wl_listener request_state;
    struct wl_listener present;
    struct wl_listener destroy;
};

// Un wlr_output nuovo dal backend: lo configura (modalità, scala, posizione
// salvate o scelte da Vela) e lo mette nel layout.
void vela_output_create(struct vela_server *server, struct wlr_output *wlr);

// Posizione e dimensioni nel layout globale.
struct wlr_box vela_output_box(const struct vela_output *output);

// Dove il compositor sistema le finestre, calcolato dai pixel fisici
// (§3.5): tutto lo schermo, la parte libera da pannelli, e un'area in pixel
// dello schermo (relativi al suo angolo) vista in logico.
struct vela_area vela_output_full_area(const struct vela_output *output);
struct vela_area vela_output_usable_area(const struct vela_output *output);
struct wlr_box vela_output_physical_usable(const struct vela_output *output);
struct vela_area vela_output_from_physical(const struct vela_output *output, struct wlr_box physical);
// Posizione e dimensione intera il cui buffer, a questa scala, copre
// esattamente `area`. Se i pixel esatti non si possono ottenere (a 150% 2560
// pixel sarebbero 1706,67 unità), il pixel in più finisce fuori dallo
// schermo quando l'area ne tocca un bordo; altrimenti si resta un pixel
// dentro, senza sovrapporsi ad altro.
struct vela_placement vela_output_place(const struct vela_output *output, struct vela_area area);

// Posiziona pannelli e sfondi (wlr-layer-shell) e ricalcola l'area utile; se
// cambia, le finestre massimizzate e agganciate si risistemano.
void vela_output_arrange_layers(struct vela_output *output);

// Chiede un frame: al prossimo vblank lo schermo ridisegna ciò che è
// cambiato (e i frame callback partono).
void vela_output_schedule_frame(struct vela_output *output);

// Le scelte di vela.conf che valgono per tutti gli schermi:
// "variable-refresh" (no, games, always; VELA_VRR=0/1 vince) e "tearing"
// (VELA_TEARING=0 lo spegne). Poi un frame, per applicarle.
void vela_output_load_settings(struct vela_server *server);

// Applica un cambio di modalità o di scala, con il primo frame già
// disegnato alla nuova dimensione.
bool vela_output_commit_mode(struct vela_output *output, struct wlr_output_state *state);

// Spegne e riaccende lo schermo (inattività) restando nel layout: la shell
// non perde i suoi pannelli.
void vela_output_set_powered(struct vela_output *output, bool on);

// Lo schermo in un punto del layout, o NULL.
struct vela_output *vela_output_at(const struct vela_server *server, double lx, double ly);
struct vela_output *vela_output_named(const struct vela_server *server, const char *name);
// Il riquadro di una finestra (globale) rimesso dentro l'area utile dello
// schermo: prima la dimensione, poi la posizione.
struct wlr_box vela_output_fit(const struct vela_output *output, struct wlr_box frame);

// Lo schermo sotto il cursore, o il primo; NULL se non ce n'è nessuno.
struct vela_output *vela_output_under_cursor(const struct vela_server *server);

// Marca, modello e numero di serie (il nome del connettore se mancano): la
// chiave del monitor in outputs.conf.
void vela_output_key(const struct wlr_output *output, char *out, size_t size);

#endif
