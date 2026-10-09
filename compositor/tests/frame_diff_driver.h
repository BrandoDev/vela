// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef VELA_FRAME_DIFF_DRIVER_H
#define VELA_FRAME_DIFF_DRIVER_H

#include "scene/frame.h"

void vela_test_diff(struct vela_output_frame *frame);
unsigned vela_test_forgotten(void);
struct wlr_surface *vela_test_surface(void);

#endif
