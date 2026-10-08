// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// The frame clock (frame_clock.h, docs/renderer.md §4): the vblank grid, the
// late latching plan, the cost, the margin growing and shrinking, the learned
// output latency.

extern "C" {
#include "frame_clock.h"
}

#include <gtest/gtest.h>

#include <algorithm>

namespace {

constexpr int64_t ms = 1'000'000;
constexpr int64_t second = 1'000'000'000;

int64_t periodFor(int hz)
{
    return int64_t(1e12 / (hz * 1000));
}

// A simulated frame cycle: plan, deliver, the output shows the frame at the
// predicted vblank plus `hostDelay` vblanks (like a host compositor).
struct Simulation {
    vela_frame_clock clock;
    int64_t period;
    int64_t now;
    uint32_t seq = 1;
    int hostDelay = 0;

    explicit Simulation(int hz)
        : period(periodFor(hz))
        , now(10 * second)
    {
        vela_frame_clock_init(&clock);
        vela_frame_clock_set_mode_refresh(&clock, hz * 1000);
        vela_frame_clock_presented(&clock, 0, now, period); // the first presentation: there is a grid
    }

    // One frame; `late`: it arrives one vblank after the predicted one (a slow
    // frame).
    vela_frame_plan frame(int64_t cost = 1 * ms, bool late = false)
    {
        const vela_frame_plan plan = vela_frame_clock_plan(&clock, now, true);
        vela_frame_clock_add_cost(&clock, plan.start + cost, cost);
        vela_frame_clock_committed(&clock, seq, plan.start, plan.present);
        const int64_t shown = plan.deadline + (hostDelay + (late ? 1 : 0)) * period;
        vela_frame_clock_presented(&clock, seq, shown, period);
        ++seq;
        now = shown + period / 10; // the next frame is asked for shortly after
        return plan;
    }
};

} // namespace

TEST(FrameClock, UnknownRefreshIs60Hz)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    EXPECT_EQ(vela_frame_clock_period(&clock), 16'666'667);
    vela_frame_clock_set_mode_refresh(&clock, 180'000);
    EXPECT_EQ(vela_frame_clock_period(&clock), periodFor(180));
}

TEST(FrameClock, PresentedPeriodWinsOverMode)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    vela_frame_clock_set_mode_refresh(&clock, 60'000);
    vela_frame_clock_presented(&clock, 0, 1 * second, 6'944'444); // 144 Hz from real feedback
    EXPECT_EQ(vela_frame_clock_period(&clock), 6'944'444);
}

TEST(FrameClock, VblankGrid)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    const int64_t period = periodFor(60);
    const int64_t t0 = 5 * second;
    EXPECT_EQ(vela_frame_clock_vblank_after(&clock, t0 + 123), t0 + 123); // no grid yet
    vela_frame_clock_presented(&clock, 0, t0, period);
    EXPECT_EQ(vela_frame_clock_vblank_after(&clock, t0 + period / 3), t0 + period);
    EXPECT_EQ(vela_frame_clock_vblank_after(&clock, t0 + period), t0 + 2 * period); // always the next one
    EXPECT_EQ(vela_frame_clock_vblank_after(&clock, t0 + 10 * period + 1), t0 + 11 * period);
}

TEST(FrameClock, PlanWithoutGridDrawsNow)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    vela_frame_clock_set_mode_refresh(&clock, 60'000);
    const vela_frame_plan plan = vela_frame_clock_plan(&clock, 2 * second, true);
    EXPECT_EQ(plan.start, 2 * second);
    EXPECT_EQ(plan.deadline, 2 * second + vela_frame_clock_period(&clock));
    EXPECT_EQ(plan.present, plan.deadline);
}

TEST(FrameClock, LateLatchingStartsCostPlusMarginBeforeVblank)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    const int64_t period = periodFor(60);
    const int64_t t0 = 5 * second;
    vela_frame_clock_presented(&clock, 0, t0, period);
    vela_frame_clock_add_cost(&clock, t0, 2 * ms);
    const int64_t now = t0 + period / 10;
    const vela_frame_plan plan = vela_frame_clock_plan(&clock, now, true);
    EXPECT_EQ(plan.deadline, t0 + period);
    EXPECT_EQ(plan.start, t0 + period - (2 * ms + 1 * ms)); // cost + base margin
    EXPECT_EQ(plan.present, plan.deadline);
    // Without late latching drawing starts at once, for the same vblank.
    const vela_frame_plan eager = vela_frame_clock_plan(&clock, now, false);
    EXPECT_EQ(eager.start, now);
    EXPECT_EQ(eager.deadline, t0 + period);
}

TEST(FrameClock, CostIsWorstOfLastSecondButAtLeastEightFrames)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    vela_frame_clock_add_cost(&clock, 1 * second, 9 * ms); // old and slow
    for (int i = 0; i < 10; ++i) {
        vela_frame_clock_add_cost(&clock, 3 * second + i * ms, 1 * ms);
    }
    EXPECT_EQ(vela_frame_clock_cost(&clock, 3 * second + 20 * ms), 1 * ms); // outside the last second
    vela_frame_clock few;
    vela_frame_clock_init(&few);
    vela_frame_clock_add_cost(&few, 1 * second, 9 * ms);
    for (int i = 0; i < 5; ++i) {
        vela_frame_clock_add_cost(&few, 3 * second + i * ms, 1 * ms);
    }
    EXPECT_EQ(vela_frame_clock_cost(&few, 3 * second + 20 * ms), 9 * ms); // among the last 8: it counts
}

// The budget never exceeds "period − min(0.5 ms, period/4)": right after a
// vblank there is always time for the next one, the rate never halves, at any
// refresh rate and any cost.
TEST(FrameClock, BudgetNeverHalvesTheRefreshRate)
{
    for (int hz : { 60, 75, 120, 144, 165, 180, 240, 360 }) {
        vela_frame_clock clock;
        vela_frame_clock_init(&clock);
        const int64_t period = periodFor(hz);
        const int64_t t0 = 5 * second;
        vela_frame_clock_presented(&clock, 0, t0, period);
        vela_frame_clock_add_cost(&clock, t0, 100 * ms); // a very slow frame
        EXPECT_EQ(vela_frame_clock_budget(&clock, t0), period - std::min<int64_t>(500'000, period / 4)) << hz << " Hz";
        const vela_frame_plan plan = vela_frame_clock_plan(&clock, t0 + 1000, true); // right after the vblank
        EXPECT_EQ(plan.deadline, t0 + period) << hz << " Hz";
        EXPECT_GE(plan.start, t0 + 1000) << hz << " Hz";
    }
}

TEST(FrameClock, OnTimeFramesPredictExactly)
{
    Simulation sim(180);
    for (int i = 0; i < 400; ++i) {
        const vela_frame_plan plan = sim.frame();
        EXPECT_EQ(plan.present, plan.deadline);
    }
    EXPECT_EQ(sim.clock.latency_frames, 0);
    EXPECT_EQ(vela_frame_clock_margin(&sim.clock), 1 * ms);
}

TEST(FrameClock, WarmupDoesNotGrowTheMargin)
{
    Simulation sim(60);
    sim.frame(1 * ms, true); // late in the first second (allocations, modeset)
    EXPECT_EQ(vela_frame_clock_margin(&sim.clock), 1 * ms);
}

TEST(FrameClock, MissedVblankGrowsTheMarginThenItDecays)
{
    Simulation sim(60);
    while (sim.now < 12 * second) {
        sim.frame(); // past the warmup second
    }
    sim.frame(1 * ms, true);
    const int64_t grown = vela_frame_clock_margin(&sim.clock);
    EXPECT_EQ(grown, 1 * ms + std::max<int64_t>(250'000, sim.period / 10));
    // On time: it goes down by about 0.25 ms per second, to the minimum.
    for (int i = 0; i < 60; ++i) {
        sim.frame();
    }
    EXPECT_LT(vela_frame_clock_margin(&sim.clock), grown);
    EXPECT_NEAR(double(grown - vela_frame_clock_margin(&sim.clock)), 250'000.0, 2'000.0);
    while (vela_frame_clock_margin(&sim.clock) > 1 * ms) {
        sim.frame();
    }
    EXPECT_EQ(vela_frame_clock_margin(&sim.clock), 1 * ms);
}

// Nested in a compositor adding one vblank (KWin): the margin rises to the
// maximum, then the latency is learned, the margin returns to the minimum and
// predictions become exact again (§4.2, found in S0).
TEST(FrameClock, LearnsTheLatencyOfAHostCompositor)
{
    Simulation sim(60);
    sim.hostDelay = 1;
    for (int i = 0; i < 300; ++i) {
        sim.frame();
    }
    EXPECT_EQ(sim.clock.latency_frames, 1);
    EXPECT_EQ(vela_frame_clock_margin(&sim.clock), 1 * ms);
    const vela_frame_plan plan = sim.frame();
    EXPECT_EQ(plan.present, plan.deadline + sim.period);
}

TEST(FrameClock, StatsAfterTwoSeconds)
{
    Simulation sim(120);
    vela_frame_stats stats {};
    EXPECT_FALSE(vela_frame_clock_take_stats(&sim.clock, sim.now, &stats)); // the start
    const int64_t start = sim.now;
    while (sim.now < start + 2 * second + sim.period) {
        sim.frame();
    }
    ASSERT_TRUE(vela_frame_clock_take_stats(&sim.clock, sim.now, &stats));
    EXPECT_NEAR(stats.fps, 120.0, 2.0);
    EXPECT_EQ(stats.missed, 0);
    EXPECT_NEAR(stats.error_mean_ms, 0.0, 1e-9);
}
