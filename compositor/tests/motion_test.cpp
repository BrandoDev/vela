// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Le curve e i tween delle animazioni (motion.h): uguali alle curve
// cubic-bezier di CSS e QML, che usa anche la shell.

extern "C" {
#include "motion.h"
}

#include <gtest/gtest.h>

#include <cmath>

namespace {

// Il valore della curva per x, calcolato in un altro modo: campionando il
// parametro t molto fitto e prendendo il punto con la x più vicina.
double reference(double x1, double y1, double x2, double y2, double x)
{
    auto bezier = [](double a, double b, double t) {
        const double u = 1.0 - t;
        return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
    };
    double best = 0.0;
    double distance = 2.0;
    for (int i = 0; i <= 200000; ++i) {
        const double t = i / 200000.0;
        const double d = std::abs(bezier(x1, x2, t) - x);
        if (d < distance) {
            distance = d;
            best = bezier(y1, y2, t);
        }
    }
    return best;
}

} // namespace

TEST(Motion, EndpointsAndClamping)
{
    const vela_curve* curve = &vela_decelerate;
    EXPECT_EQ(vela_curve_eval(curve, 0.0), 0.0);
    EXPECT_EQ(vela_curve_eval(curve, 1.0), 1.0);
    EXPECT_EQ(vela_curve_eval(curve, -0.5), 0.0);
    EXPECT_EQ(vela_curve_eval(curve, 1.5), 1.0);
}

TEST(Motion, LinearCurveIsIdentity)
{
    const vela_curve linear { 0.0, 0.0, 1.0, 1.0 };
    for (double x = 0.0; x <= 1.0; x += 0.05) {
        EXPECT_NEAR(vela_curve_eval(&linear, x), x, 1e-6);
    }
}

TEST(Motion, MatchesCubicBezierDefinition)
{
    struct Case {
        double x1, y1, x2, y2;
    };
    for (const Case& c : { Case { 0.0, 0.0, 0.2, 1.0 }, Case { 0.25, 0.1, 0.25, 1.0 }, Case { 0.42, 0.0, 0.58, 1.0 },
             Case { 0.1, 0.9, 0.2, 1.0 } }) {
        const vela_curve curve { c.x1, c.y1, c.x2, c.y2 };
        for (double x = 0.05; x < 1.0; x += 0.05) {
            EXPECT_NEAR(vela_curve_eval(&curve, x), reference(c.x1, c.y1, c.x2, c.y2, x), 1e-4) << "x=" << x;
        }
    }
}

TEST(Motion, DecelerateIsMonotonicAndFrontLoaded)
{
    const vela_curve* curve = &vela_decelerate;
    double previous = 0.0;
    for (int i = 1; i <= 1000; ++i) {
        const double value = vela_curve_eval(curve, i / 1000.0);
        EXPECT_GE(value, previous);
        previous = value;
    }
    // Parte decisa: a metà del tempo ha già fatto ben più di metà strada,
    // ma non così tanto da sparire nei primi frame ad alta frequenza.
    EXPECT_GT(vela_curve_eval(curve, 0.5), 0.7);
    EXPECT_LT(vela_curve_eval(curve, 3.0 / 45.0), 0.35); // 3 frame a 180 Hz di un'animazione da 250 ms
}

TEST(Motion, TweenStartsAtFirstRead)
{
    vela_tween tween;
    vela_tween_start(&tween, 250.0, &vela_decelerate);
    EXPECT_FALSE(vela_tween_finished(&tween, 1000.0)); // non ancora letto: non è partito
    EXPECT_EQ(vela_tween_progress(&tween, 1000.0), 0.0); // il primo frame è lo stato iniziale
    EXPECT_GT(vela_tween_progress(&tween, 1100.0), 0.0);
    EXPECT_FALSE(vela_tween_finished(&tween, 1249.0));
    EXPECT_TRUE(vela_tween_finished(&tween, 1250.0));
    EXPECT_EQ(vela_tween_progress(&tween, 1300.0), 1.0);
}

TEST(Motion, TweenWithoutCurveIsLinear)
{
    vela_tween tween;
    vela_tween_start(&tween, 100.0, nullptr);
    EXPECT_EQ(vela_tween_progress(&tween, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(vela_tween_progress(&tween, 25.0), 0.25);
    EXPECT_DOUBLE_EQ(vela_tween_progress(&tween, 100.0), 1.0);
}
