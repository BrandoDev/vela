// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Exercise the real, private CPU diff without creating a compositor or GPU.
// Function sections let the linker discard the unrelated rendering paths.
#ifndef VELA_FRAME_SOURCE
#define VELA_FRAME_SOURCE "../src/scene/frame.c"
#endif
#include VELA_FRAME_SOURCE
#include "frame_diff_driver.h"

static unsigned forgotten;

void vela_surface_forget(struct wlr_surface *surface, struct wlr_output *output)
{
    (void)surface;
    (void)output;
    ++forgotten;
}

void vela_test_diff(struct vela_output_frame *frame)
{
    diff_with_last(frame);
}

unsigned vela_test_forgotten(void)
{
    return forgotten;
}

struct wlr_surface *vela_test_surface(void)
{
    static struct wlr_surface surface;
    return &surface;
}
