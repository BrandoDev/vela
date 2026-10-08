// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_NESTED_H
#define VELA_NESTED_H

// Vela in a window inside another session (such as KDE), for testing.
// The wlroots nested backend can't do two things, so we do them by talking
// to the host compositor directly:
//
// - Sharpness: a host at a fractional scale (KDE at 125%) would enlarge our
//   window and blur it. We ask for its scale (fractional-scale-v1) and draw
//   a buffer as large as its physical pixels, declaring the logical size
//   with viewporter.
// - Shortcuts: we ask the host not to grab Super, Alt+Tab and the like while
//   Vela's window has the keyboard (keyboard-shortcuts-inhibit).
//
// Owned by its vela_output.

struct vela_nested;
struct vela_output;

struct vela_nested *vela_nested_create(struct vela_output *output);
void vela_nested_destroy(struct vela_nested *nested);

// The host asks for a new window size (in its units).
void vela_nested_resize(struct vela_nested *nested, int width, int height);

// Pixels of our buffer per unit of the host window: the nested backend reports
// the pointer in buffer pixels.
double vela_nested_pointer_scale_x(const struct vela_nested *nested);
double vela_nested_pointer_scale_y(const struct vela_nested *nested);

#endif
