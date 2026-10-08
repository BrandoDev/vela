// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "motion.h"

#include "util.h"

#include <math.h>

const struct vela_curve vela_decelerate = { 0.0, 0.0, 0.2, 1.0 };

// The coefficients of one axis' polynomial: ((a t + b) t + c) t.
struct axis {
    double a, b, c;
};

static struct axis axis_of(double p1, double p2)
{
    struct axis axis;
    axis.c = 3.0 * p1;
    axis.b = 3.0 * (p2 - p1) - axis.c;
    axis.a = 1.0 - axis.c - axis.b;
    return axis;
}

static double sample(struct axis axis, double t)
{
    return ((axis.a * t + axis.b) * t + axis.c) * t;
}

static double sample_derivative(struct axis axis, double t)
{
    return (3.0 * axis.a * t + 2.0 * axis.b) * t + axis.c;
}

// Finds t such that x(t) == x: Newton, with bisection as a fallback.
static double solve_t(struct axis ax, double x)
{
    double t = x;
    for (int i = 0; i < 8; ++i) {
        double err = sample(ax, t) - x;
        if (fabs(err) < 1e-6) {
            return t;
        }
        double d = sample_derivative(ax, t);
        if (fabs(d) < 1e-6) {
            break;
        }
        t -= err / d;
    }
    double lo = 0.0;
    double hi = 1.0;
    t = x;
    for (int i = 0; i < 32; ++i) {
        double v = sample(ax, t);
        if (fabs(v - x) < 1e-6) {
            break;
        }
        if (x > v) {
            lo = t;
        } else {
            hi = t;
        }
        t = (lo + hi) / 2.0;
    }
    return t;
}

double vela_curve_eval(const struct vela_curve *curve, double x)
{
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }
    struct axis ax = axis_of(curve->x1, curve->x2);
    struct axis ay = axis_of(curve->y1, curve->y2);
    return sample(ay, solve_t(ax, x));
}

void vela_tween_start(struct vela_tween *tween, double duration_ms, const struct vela_curve *curve)
{
    tween->start_ms = -1.0;
    tween->duration_ms = duration_ms;
    tween->curve = curve;
}

double vela_tween_progress(struct vela_tween *tween, double now_ms)
{
    if (tween->start_ms < 0.0) {
        tween->start_ms = now_ms;
    }
    double x = vela_clampd((now_ms - tween->start_ms) / tween->duration_ms, 0.0, 1.0);
    return tween->curve ? vela_curve_eval(tween->curve, x) : x;
}

bool vela_tween_finished(const struct vela_tween *tween, double now_ms)
{
    return tween->start_ms >= 0.0 && now_ms - tween->start_ms >= tween->duration_ms;
}
