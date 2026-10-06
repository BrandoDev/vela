// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Le curve e i tween delle animazioni (motion.hpp): uguali alle curve
// cubic-bezier di CSS e QML, che usa anche la shell.

#include "motion.hpp"

#include <gtest/gtest.h>

using vela::CubicBezier;
using vela::Tween;

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
    const CubicBezier& curve = vela::motion::decelerate;
    EXPECT_EQ(curve(0.0), 0.0);
    EXPECT_EQ(curve(1.0), 1.0);
    EXPECT_EQ(curve(-0.5), 0.0);
    EXPECT_EQ(curve(1.5), 1.0);
}

TEST(Motion, LinearCurveIsIdentity)
{
    const CubicBezier linear(0.0, 0.0, 1.0, 1.0);
    for (double x = 0.0; x <= 1.0; x += 0.05) {
        EXPECT_NEAR(linear(x), x, 1e-6);
    }
}

TEST(Motion, MatchesCubicBezierDefinition)
{
    struct Case {
        double x1, y1, x2, y2;
    };
    for (const Case& c : { Case { 0.0, 0.0, 0.2, 1.0 }, Case { 0.25, 0.1, 0.25, 1.0 }, Case { 0.42, 0.0, 0.58, 1.0 },
             Case { 0.1, 0.9, 0.2, 1.0 } }) {
        const CubicBezier curve(c.x1, c.y1, c.x2, c.y2);
        for (double x = 0.05; x < 1.0; x += 0.05) {
            EXPECT_NEAR(curve(x), reference(c.x1, c.y1, c.x2, c.y2, x), 1e-4) << "x=" << x;
        }
    }
}

TEST(Motion, DecelerateIsMonotonicAndFrontLoaded)
{
    const CubicBezier& curve = vela::motion::decelerate;
    double previous = 0.0;
    for (int i = 1; i <= 1000; ++i) {
        const double value = curve(i / 1000.0);
        EXPECT_GE(value, previous);
        previous = value;
    }
    // Parte decisa: a metà del tempo ha già fatto ben più di metà strada,
    // ma non così tanto da sparire nei primi frame ad alta frequenza.
    EXPECT_GT(curve(0.5), 0.7);
    EXPECT_LT(curve(3.0 / 45.0), 0.35); // 3 frame a 180 Hz di un'animazione da 250 ms
}

TEST(Motion, TweenStartsAtFirstRead)
{
    Tween tween(250.0, &vela::motion::decelerate);
    EXPECT_FALSE(tween.finished(1000.0)); // non ancora letto: non è partito
    EXPECT_EQ(tween.progress(1000.0), 0.0); // il primo frame è lo stato iniziale
    EXPECT_GT(tween.progress(1100.0), 0.0);
    EXPECT_FALSE(tween.finished(1249.0));
    EXPECT_TRUE(tween.finished(1250.0));
    EXPECT_EQ(tween.progress(1300.0), 1.0);
}

TEST(Motion, TweenWithoutCurveIsLinear)
{
    Tween tween(100.0, nullptr);
    EXPECT_EQ(tween.progress(0.0), 0.0);
    EXPECT_DOUBLE_EQ(tween.progress(25.0), 0.25);
    EXPECT_DOUBLE_EQ(tween.progress(100.0), 1.0);
}
