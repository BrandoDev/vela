// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_MOTION_H
#define VELA_MOTION_H

// Vela's motion design: curves and durations in one place, so the whole
// desktop moves the same way. The QML shell uses the same values (see
// shell/qml/Theme.qml).

#include <stdbool.h>

// A cubic Bézier curve like CSS/QML ones: cubic-bezier(x1, y1, x2, y2).
struct vela_curve {
    double x1, y1, x2, y2;
};

// x = normalized time [0, 1] -> progress [0, 1].
double vela_curve_eval(const struct vela_curve *curve, double x);

// A fixed-duration interpolation. Its start time is fixed by the first frame
// that reads it, so the first frame drawn is always exactly the initial state,
// however long passes between the request and the frame.
struct vela_tween {
    double start_ms; // < 0: not read yet
    double duration_ms;
    const struct vela_curve *curve; // NULL: linear
};

void vela_tween_start(struct vela_tween *tween, double duration_ms, const struct vela_curve *curve);
double vela_tween_progress(struct vela_tween *tween, double now_ms);
bool vela_tween_finished(const struct vela_tween *tween, double now_ms);

// Deceleration: starts decisively and settles gently. This curve, more than
// the duration, is what feels "modern". Beware of more extreme curves such as
// (0.1, 0.9, 0.2, 1): at 180 Hz they cover half the movement in the first 3
// frames and the animation is no longer visible.
extern const struct vela_curve vela_decelerate;

// Window open: fade plus rise. Same duration as Theme.slow.
#define VELA_WINDOW_OPEN_MS 250.0
#define VELA_WINDOW_OPEN_RISE 36

// Close: the window shrinks a little and disappears. The deceleration curve
// here too: with one that starts slowly the window would seem to ignore the
// click for half the time.
#define VELA_WINDOW_CLOSE_MS 180.0
#define VELA_WINDOW_CLOSE_SCALE 0.94

// Minimize and restore: the window flies to its taskbar button (and back),
// shrinking and fading.
#define VELA_WINDOW_MINIMIZE_MS 250.0
#define VELA_WINDOW_MINIMIZE_SCALE 0.3

// Maximize and restore: the old content morphs into the new frame and fades
// into the real window.
#define VELA_WINDOW_MAXIMIZE_MS 250.0

// Snap preview: grows from the center of the area and lights up.
#define VELA_SNAP_PREVIEW_MS 150.0

// Desktop switch: the desktop being left slides away fading, the new one comes
// from the other side. The slide is a fraction of the narrowest output: with
// several outputs windows don't intrude on the next one.
#define VELA_WORKSPACE_SWITCH_MS 300.0
#define VELA_WORKSPACE_SLIDE_FRACTION 0.25

// Magnifier: from one zoom level to the next.
#define VELA_MAGNIFIER_MS 150.0

#endif
