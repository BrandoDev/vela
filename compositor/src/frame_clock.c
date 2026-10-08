// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frame_clock.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MS INT64_C(1000000)
#define SECOND INT64_C(1000000000)

// Quante misure concordi servono per cambiare la latenza dello schermo.
#define AGREEMENT_NEEDED 8

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

void vela_frame_clock_init(struct vela_frame_clock *clock)
{
    memset(clock, 0, sizeof(*clock));
    clock->warmup_pending = true;
    clock->base_margin = 1 * MS;
}

void vela_frame_clock_set_base_margin(struct vela_frame_clock *clock, int64_t ns)
{
    clock->base_margin = max64(0, ns);
}

void vela_frame_clock_set_mode_refresh(struct vela_frame_clock *clock, int32_t refresh_mhz)
{
    int64_t period = refresh_mhz > 0 ? (int64_t)(1e12 / refresh_mhz) : 0;
    if (period != clock->mode_period) {
        clock->warmup_pending = true;
    }
    clock->mode_period = period;
}

int64_t vela_frame_clock_period(const struct vela_frame_clock *clock)
{
    if (clock->presented_period > 0) {
        return clock->presented_period;
    }
    return clock->mode_period > 0 ? clock->mode_period : 16666667; // sconosciuto: 60 Hz
}

int64_t vela_frame_clock_vblank_after(const struct vela_frame_clock *clock, int64_t t)
{
    int64_t period = vela_frame_clock_period(clock);
    int64_t last = clock->last_present;
    if (last <= 0) {
        return t;
    }
    if (t < last) {
        return last - ((last - t) / period) * period;
    }
    return last + ((t - last) / period + 1) * period;
}

void vela_frame_clock_add_cost(struct vela_frame_clock *clock, int64_t now, int64_t cost)
{
    clock->costs[clock->next_cost].at = now;
    clock->costs[clock->next_cost].ns = max64(0, cost);
    clock->next_cost = (clock->next_cost + 1) % VELA_FRAME_COST_SLOTS;
    if (clock->cost_count < VELA_FRAME_COST_SLOTS) {
        ++clock->cost_count;
    }
}

int64_t vela_frame_clock_cost(const struct vela_frame_clock *clock, int64_t now)
{
    int64_t worst = 0;
    for (int i = 0; i < clock->cost_count; ++i) {
        int slot = (clock->next_cost - 1 - i + VELA_FRAME_COST_SLOTS) % VELA_FRAME_COST_SLOTS;
        if (i >= 8 && now - clock->costs[slot].at > SECOND) {
            break;
        }
        worst = max64(worst, clock->costs[slot].ns);
    }
    return worst;
}

int64_t vela_frame_clock_margin(const struct vela_frame_clock *clock)
{
    return clock->base_margin + clock->extra_margin;
}

static int64_t max_budget(const struct vela_frame_clock *clock)
{
    int64_t period = vela_frame_clock_period(clock);
    return period - min64(500000, period / 4);
}

int64_t vela_frame_clock_budget(const struct vela_frame_clock *clock, int64_t now)
{
    return min64(vela_frame_clock_cost(clock, now) + vela_frame_clock_margin(clock), max_budget(clock));
}

struct vela_frame_plan vela_frame_clock_plan(const struct vela_frame_clock *clock, int64_t now, bool latch)
{
    struct vela_frame_plan plan;
    if (latch) {
        int64_t budget = vela_frame_clock_budget(clock, now);
        plan.deadline = vela_frame_clock_vblank_after(clock, now + budget);
        plan.start = max64(now, plan.deadline - budget);
    } else {
        int64_t cost = min64(vela_frame_clock_cost(clock, now), max_budget(clock));
        plan.deadline = vela_frame_clock_vblank_after(clock, now + cost);
        plan.start = now;
    }
    if (clock->last_present <= 0) {
        plan.deadline = plan.start + vela_frame_clock_period(clock); // nessuna griglia ancora: stima
    }
    plan.present = plan.deadline + (int64_t)clock->latency_frames * vela_frame_clock_period(clock);
    return plan;
}

void vela_frame_clock_committed(struct vela_frame_clock *clock, uint32_t seq, int64_t start, int64_t predicted)
{
    int slot = seq % VELA_FRAME_PENDING_SLOTS;
    clock->pending[slot].seq = seq;
    clock->pending[slot].start = start;
    clock->pending[slot].predicted = predicted;
    clock->pending[slot].valid = true;
}

// Se le ultime misure concordano su uno scarto di un numero intero di
// vblank, quello scarto è la latenza dello schermo: si corregge la
// previsione. Una misura isolata (un frame perso) non basta.
static void learn_latency(struct vela_frame_clock *clock, int64_t error)
{
    int64_t period = vela_frame_clock_period(clock);
    int shift = (int)((error + (error >= 0 ? period / 2 : -period / 2)) / period);
    if (shift == 0 || shift != clock->shift_candidate) {
        clock->shift_candidate = shift;
        clock->shift_count = shift == 0 ? 0 : 1;
        return;
    }
    if (++clock->shift_count >= AGREEMENT_NEEDED) {
        clock->latency_frames += shift;
        if (clock->latency_frames < 0) {
            clock->latency_frames = 0;
        }
        if (shift > 0) {
            // Il ritardo era dello schermo, non nostro: il margine
            // cresciuto per inseguirlo non serve più.
            clock->extra_margin = 0;
        }
        clock->shift_candidate = 0;
        clock->shift_count = 0;
    }
}

void vela_frame_clock_presented(struct vela_frame_clock *clock, uint32_t seq, int64_t when, int64_t refresh)
{
    clock->last_present = when;
    if (refresh > 0) {
        clock->presented_period = refresh;
    }
    ++clock->frames;
    ++clock->frames_total;
    int slot = seq % VELA_FRAME_PENDING_SLOTS;
    if (!clock->pending[slot].valid || clock->pending[slot].seq != seq) {
        return;
    }
    clock->pending[slot].valid = false;
    int64_t period = vela_frame_clock_period(clock);
    int64_t error = when - clock->pending[slot].predicted;
    // Il primo secondo dopo l'avvio o un cambio di modo non insegna nulla:
    // allocazioni, pipeline nuove e modeset fanno arrivare tardi qualche
    // frame una volta sola.
    if (clock->warmup_pending) {
        clock->warmup_until = when + SECOND;
        clock->warmup_pending = false;
    }
    bool warming_up = when < clock->warmup_until;
    if (error > period / 2) {
        // Arrivato tardi. Se il margine può ancora crescere è colpa
        // nostra (frame lento): margine più ampio. Se non può, il ritardo
        // è dello schermo (compositor ospite): lo impara la latenza.
        int missed = (int)((error + period / 2) / period);
        clock->missed += missed;
        clock->missed_total += missed;
        if (warming_up) {
            // niente da imparare
        } else if (vela_frame_clock_budget(clock, when) < max_budget(clock)) {
            clock->extra_margin = min64(clock->extra_margin + max64(250000, period / 10), period);
            clock->shift_candidate = 0;
            clock->shift_count = 0;
        } else {
            learn_latency(clock, error);
        }
    } else {
        if (!warming_up) {
            learn_latency(clock, error);
        }
        // Puntuale: il margine in più torna giù di 0,25 ms al secondo.
        clock->extra_margin = max64(0, clock->extra_margin - 250000 * period / SECOND - 1);
    }
    double error_ms = (double)error / 1e6;
    clock->error_sum_ms += fabs(error_ms);
    clock->error_max_ms = fmax(clock->error_max_ms, fabs(error_ms));
    clock->latency_sum_ms += (double)(when - clock->pending[slot].start) / 1e6;
    ++clock->measured;
}

bool vela_frame_clock_take_stats(struct vela_frame_clock *clock, int64_t now, struct vela_frame_stats *out)
{
    if (clock->stats_start == 0) {
        clock->stats_start = now;
        return false;
    }
    double seconds = (double)(now - clock->stats_start) / 1e9;
    if (seconds < 2.0) {
        return false;
    }
    out->fps = clock->frames / seconds;
    out->error_mean_ms = clock->measured ? clock->error_sum_ms / clock->measured : 0.0;
    out->error_max_ms = clock->error_max_ms;
    out->latency_ms = clock->measured ? clock->latency_sum_ms / clock->measured : 0.0;
    out->cost_ms = (double)vela_frame_clock_cost(clock, now) / 1e6;
    out->margin_ms = (double)vela_frame_clock_margin(clock) / 1e6;
    out->missed = clock->missed;
    clock->stats_start = now;
    clock->frames = 0;
    clock->measured = 0;
    clock->missed = 0;
    clock->error_sum_ms = 0.0;
    clock->error_max_ms = 0.0;
    clock->latency_sum_ms = 0.0;
    return true;
}
