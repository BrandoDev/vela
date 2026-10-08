// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Il frame clock (frame_clock.h, docs/renderer.md §4): la griglia dei
// vblank, il piano del late latching, il costo, il margine che cresce e
// cala, la latenza dello schermo imparata.

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

// Un ciclo di frame simulato: si pianifica, si consegna, lo schermo mostra il
// frame al vblank previsto più `hostDelay` vblank (come un compositor ospite).
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
        vela_frame_clock_presented(&clock, 0, now, period); // la prima comparsa: c'è la griglia
    }

    // Un frame; `late`: arriva un vblank dopo il previsto (frame lento).
    vela_frame_plan frame(int64_t cost = 1 * ms, bool late = false)
    {
        const vela_frame_plan plan = vela_frame_clock_plan(&clock, now, true);
        vela_frame_clock_add_cost(&clock, plan.start + cost, cost);
        vela_frame_clock_committed(&clock, seq, plan.start, plan.present);
        const int64_t shown = plan.deadline + (hostDelay + (late ? 1 : 0)) * period;
        vela_frame_clock_presented(&clock, seq, shown, period);
        ++seq;
        now = shown + period / 10; // il prossimo frame si chiede poco dopo
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
    vela_frame_clock_presented(&clock, 0, 1 * second, 6'944'444); // 144 Hz dal feedback vero
    EXPECT_EQ(vela_frame_clock_period(&clock), 6'944'444);
}

TEST(FrameClock, VblankGrid)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    const int64_t period = periodFor(60);
    const int64_t t0 = 5 * second;
    EXPECT_EQ(vela_frame_clock_vblank_after(&clock, t0 + 123), t0 + 123); // nessuna griglia ancora
    vela_frame_clock_presented(&clock, 0, t0, period);
    EXPECT_EQ(vela_frame_clock_vblank_after(&clock, t0 + period / 3), t0 + period);
    EXPECT_EQ(vela_frame_clock_vblank_after(&clock, t0 + period), t0 + 2 * period); // sempre il successivo
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
    EXPECT_EQ(plan.start, t0 + period - (2 * ms + 1 * ms)); // costo + margine di base
    EXPECT_EQ(plan.present, plan.deadline);
    // Senza late latching si comincia subito, per lo stesso vblank.
    const vela_frame_plan eager = vela_frame_clock_plan(&clock, now, false);
    EXPECT_EQ(eager.start, now);
    EXPECT_EQ(eager.deadline, t0 + period);
}

TEST(FrameClock, CostIsWorstOfLastSecondButAtLeastEightFrames)
{
    vela_frame_clock clock;
    vela_frame_clock_init(&clock);
    vela_frame_clock_add_cost(&clock, 1 * second, 9 * ms); // vecchio e lento
    for (int i = 0; i < 10; ++i) {
        vela_frame_clock_add_cost(&clock, 3 * second + i * ms, 1 * ms);
    }
    EXPECT_EQ(vela_frame_clock_cost(&clock, 3 * second + 20 * ms), 1 * ms); // fuori dall'ultimo secondo
    vela_frame_clock few;
    vela_frame_clock_init(&few);
    vela_frame_clock_add_cost(&few, 1 * second, 9 * ms);
    for (int i = 0; i < 5; ++i) {
        vela_frame_clock_add_cost(&few, 3 * second + i * ms, 1 * ms);
    }
    EXPECT_EQ(vela_frame_clock_cost(&few, 3 * second + 20 * ms), 9 * ms); // tra gli ultimi 8: conta
}

// Il budget non supera mai "periodo − min(0,5 ms, periodo/4)": appena dopo
// un vblank si fa sempre in tempo per il successivo, la frequenza non si
// dimezza mai, a nessuna frequenza e con nessun costo.
TEST(FrameClock, BudgetNeverHalvesTheRefreshRate)
{
    for (int hz : { 60, 75, 120, 144, 165, 180, 240, 360 }) {
        vela_frame_clock clock;
        vela_frame_clock_init(&clock);
        const int64_t period = periodFor(hz);
        const int64_t t0 = 5 * second;
        vela_frame_clock_presented(&clock, 0, t0, period);
        vela_frame_clock_add_cost(&clock, t0, 100 * ms); // un frame lentissimo
        EXPECT_EQ(vela_frame_clock_budget(&clock, t0), period - std::min<int64_t>(500'000, period / 4)) << hz << " Hz";
        const vela_frame_plan plan = vela_frame_clock_plan(&clock, t0 + 1000, true); // subito dopo il vblank
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
    sim.frame(1 * ms, true); // tardi nel primo secondo (allocazioni, modeset)
    EXPECT_EQ(vela_frame_clock_margin(&sim.clock), 1 * ms);
}

TEST(FrameClock, MissedVblankGrowsTheMarginThenItDecays)
{
    Simulation sim(60);
    while (sim.now < 12 * second) {
        sim.frame(); // oltre il secondo di warmup
    }
    sim.frame(1 * ms, true);
    const int64_t grown = vela_frame_clock_margin(&sim.clock);
    EXPECT_EQ(grown, 1 * ms + std::max<int64_t>(250'000, sim.period / 10));
    // Puntuale: torna giù di circa 0,25 ms al secondo, fino al minimo.
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

// Annidato in un compositor che aggiunge un vblank (KWin): il margine sale
// fino al massimo, poi la latenza viene imparata, il margine torna al minimo
// e le previsioni tornano esatte (§4.2, scoperto in S0).
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
    EXPECT_FALSE(vela_frame_clock_take_stats(&sim.clock, sim.now, &stats)); // l'inizio
    const int64_t start = sim.now;
    while (sim.now < start + 2 * second + sim.period) {
        sim.frame();
    }
    ASSERT_TRUE(vela_frame_clock_take_stats(&sim.clock, sim.now, &stats));
    EXPECT_NEAR(stats.fps, 120.0, 2.0);
    EXPECT_EQ(stats.missed, 0);
    EXPECT_NEAR(stats.error_mean_ms, 0.0, 1e-9);
}
