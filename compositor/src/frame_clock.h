// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_FRAME_CLOCK_H
#define VELA_FRAME_CLOCK_H

// Il tempo di uno schermo (docs/renderer.md §4): quando verrà mostrato il
// frame che stiamo per disegnare, e quando cominciare a disegnarlo.
//
// - Le animazioni si calcolano per l'istante in cui il frame diventerà
//   luce, non per "adesso": il movimento è giusto al frame a qualunque
//   frequenza, anche se un frame arriva in ritardo (§4.2).
// - Si disegna il più tardi possibile (late latching, §4.3): si misura
//   quanto costa un frame (CPU e GPU, fino a quando è pronto) e si comincia
//   quel tanto, più un margine, prima del vblank. Input e animazioni
//   campionati più tardi = latenza minore. Se un frame arriva tardi il
//   margine cresce da solo, poi torna piano piano al minimo.
//
// Tutto in nanosecondi su CLOCK_MONOTONIC. La struttura vive dentro
// vela_output, per valore: nessuna allocazione.

#include <stdbool.h>
#include <stdint.h>

#define VELA_FRAME_COST_SLOTS 64
#define VELA_FRAME_PENDING_SLOTS 8

struct vela_frame_plan {
    int64_t start; // quando cominciare a disegnare
    int64_t deadline; // il vblank per cui si disegna
    int64_t present; // quando il frame diventerà luce
};

// Statistiche dall'ultima vela_frame_clock_take_stats.
struct vela_frame_stats {
    double fps; // frame mostrati al secondo
    double error_mean_ms; // errore della previsione del momento di comparsa
    double error_max_ms;
    double latency_ms; // dall'inizio del disegno alla luce, in media
    double cost_ms; // costo di un frame (il massimo recente)
    double margin_ms;
    int missed; // vblank persi
};

struct vela_frame_clock {
    // Vblank di ritardo tra la consegna di un frame e la sua comparsa.
    // Sull'hardware di solito 0; in una finestra annidata il compositor
    // ospite ne aggiunge (KWin: 1 o 2). Si impara da sé.
    int latency_frames;
    // Dall'avvio, senza azzerare (per le prove: "state").
    uint64_t frames_total;
    uint64_t missed_total;

    // Il resto è privato di frame_clock.c.
    int shift_candidate;
    int shift_count;
    bool warmup_pending;
    int64_t warmup_until;
    int64_t base_margin;
    int64_t extra_margin;

    struct {
        int64_t at;
        int64_t ns;
    } costs[VELA_FRAME_COST_SLOTS];
    int next_cost;
    int cost_count;

    int64_t mode_period;
    int64_t presented_period;
    int64_t last_present;
    struct {
        uint32_t seq;
        int64_t start;
        int64_t predicted;
        bool valid;
    } pending[VELA_FRAME_PENDING_SLOTS];

    int64_t stats_start;
    int frames;
    int measured;
    int missed;
    double error_sum_ms;
    double error_max_ms;
    double latency_sum_ms;
};

void vela_frame_clock_init(struct vela_frame_clock *clock);

// Margine minimo tra "frame pronto" e vblank (VELA_LATCH_MARGIN).
void vela_frame_clock_set_base_margin(struct vela_frame_clock *clock, int64_t ns);

// Periodo della modalità dello schermo (mHz), usato finché il backend
// non ci dice il periodo vero con il feedback di presentazione.
void vela_frame_clock_set_mode_refresh(struct vela_frame_clock *clock, int32_t refresh_mhz);

int64_t vela_frame_clock_period(const struct vela_frame_clock *clock);

// Il primo vblank della griglia (ultima presentazione vera + multipli del
// periodo) dopo `t`. Senza presentazioni: `t` stesso.
int64_t vela_frame_clock_vblank_after(const struct vela_frame_clock *clock, int64_t t);

// Un frame misurato: dall'istante in cui doveva cominciare a quello in cui
// era pronto (commit fatto e GPU finita).
void vela_frame_clock_add_cost(struct vela_frame_clock *clock, int64_t now, int64_t cost);

// Il costo di un frame: il massimo dell'ultimo secondo (almeno gli ultimi
// 8 frame), perché un frame lento non deve far perdere il vblank.
int64_t vela_frame_clock_cost(const struct vela_frame_clock *clock, int64_t now);

int64_t vela_frame_clock_margin(const struct vela_frame_clock *clock);

// Quanto prima del vblank si comincia: costo + margine, ma mai tanto da
// saltare un vblank a ogni frame (subito dopo un vblank si fa sempre in
// tempo per il successivo).
int64_t vela_frame_clock_budget(const struct vela_frame_clock *clock, int64_t now);

// Il prossimo frame, se ne serve uno adesso. latch: si comincia il più
// tardi possibile; altrimenti subito.
struct vela_frame_plan vela_frame_clock_plan(const struct vela_frame_clock *clock, int64_t now, bool latch);

// Il frame per `seq` (commit_seq dello schermo), cominciato a `start`,
// doveva comparire a `predicted`.
void vela_frame_clock_committed(struct vela_frame_clock *clock, uint32_t seq, int64_t start, int64_t predicted);

// Feedback del backend: il frame `seq` è comparso a `when`.
void vela_frame_clock_presented(struct vela_frame_clock *clock, uint32_t seq, int64_t when, int64_t refresh);

// Le statistiche dall'ultima chiamata (e azzera). false se sono passati
// meno di due secondi.
bool vela_frame_clock_take_stats(struct vela_frame_clock *clock, int64_t now, struct vela_frame_stats *out);

#endif
