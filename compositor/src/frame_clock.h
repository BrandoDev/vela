// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_FRAME_CLOCK_H
#define VELA_FRAME_CLOCK_H

// An output's time (docs/renderer.md §4): when the frame we are about to
// draw will be shown, and when to start drawing it.
//
// - Animations are computed for the moment the frame will become light, not
//   for "now": motion is right at every frame at any refresh rate, even when
//   a frame comes late (§4.2).
// - Drawing happens as late as possible (late latching, §4.3): we measure
//   what a frame costs (CPU and GPU, until it's ready) and start that much,
//   plus a margin, before the vblank. Input and animations sampled later
//   mean lower latency. When a frame is late the margin grows by itself,
//   then slowly returns to the minimum.
//
// Everything in nanoseconds on CLOCK_MONOTONIC. The struct lives inside
// vela_output, by value: no allocation.

#include <stdbool.h>
#include <stdint.h>

#define VELA_FRAME_COST_SLOTS 64
#define VELA_FRAME_PENDING_SLOTS 8

struct vela_frame_plan {
    int64_t start; // when to start drawing
    int64_t deadline; // the vblank being drawn for
    int64_t present; // when the frame will become light
};

// Statistics since the last vela_frame_clock_take_stats.
struct vela_frame_stats {
    double fps; // frames shown per second
    double error_mean_ms; // error predicting when frames show
    double error_max_ms;
    double latency_ms; // from the start of drawing to light, on average
    double cost_ms; // cost of a frame (the recent maximum)
    double margin_ms;
    int missed; // missed vblanks
};

struct vela_frame_clock {
    // Vblanks of delay between delivering a frame and its showing. Usually 0
    // on hardware; in a nested window the host compositor adds some (KWin: 1
    // or 2). Learned by itself.
    int latency_frames;
    // Since startup, never reset (for tests: "state").
    uint64_t frames_total;
    uint64_t missed_total;

    // The rest is private to frame_clock.c.
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

// Minimum margin between "frame ready" and vblank (VELA_LATCH_MARGIN).
void vela_frame_clock_set_base_margin(struct vela_frame_clock *clock, int64_t ns);

// The output mode's period (mHz), used until the backend reports the real
// period through presentation feedback.
void vela_frame_clock_set_mode_refresh(struct vela_frame_clock *clock, int32_t refresh_mhz);

int64_t vela_frame_clock_period(const struct vela_frame_clock *clock);

// The first vblank of the grid (last real presentation + multiples of the
// period) after `t`. Without presentations: `t` itself.
int64_t vela_frame_clock_vblank_after(const struct vela_frame_clock *clock, int64_t t);

// A measured frame: from when it should have started to when it was ready
// (commit done and GPU finished).
void vela_frame_clock_add_cost(struct vela_frame_clock *clock, int64_t now, int64_t cost);

// The cost of a frame: the maximum of the last second (at least the last 8
// frames), because a slow frame must not miss the vblank.
int64_t vela_frame_clock_cost(const struct vela_frame_clock *clock, int64_t now);

int64_t vela_frame_clock_margin(const struct vela_frame_clock *clock);

// How long before the vblank to start: cost + margin, but never so much that a
// vblank is skipped every frame (right after a vblank there is always time for
// the next one).
int64_t vela_frame_clock_budget(const struct vela_frame_clock *clock, int64_t now);

// The next frame, if one is needed now. latch: start as late as possible;
// otherwise right away.
struct vela_frame_plan vela_frame_clock_plan(const struct vela_frame_clock *clock, int64_t now, bool latch);

// The frame for `seq` (the output's commit_seq), started at `start`, was due
// to show at `predicted`.
void vela_frame_clock_committed(struct vela_frame_clock *clock, uint32_t seq, int64_t start, int64_t predicted);

// Backend feedback: frame `seq` showed at `when`.
void vela_frame_clock_presented(struct vela_frame_clock *clock, uint32_t seq, int64_t when, int64_t refresh);

// The statistics since the last call (and resets them). false if less than two
// seconds have passed.
bool vela_frame_clock_take_stats(struct vela_frame_clock *clock, int64_t now, struct vela_frame_stats *out);

#endif
