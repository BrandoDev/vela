// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_RENDER_PIXEL_BUFFER_H
#define VELA_RENDER_PIXEL_BUFFER_H

// An image drawn by the CPU (text, button glyphs) as a wlr_buffer: the
// renderer uploads it to a texture like a wl_shm buffer.

#include "image.h"

struct wlr_buffer;

// Takes over the pixels of `image` (which is left empty): the buffer frees
// them when it goes away. The buffer starts with one reference, let go with
// wlr_buffer_drop().
struct wlr_buffer *vela_pixel_buffer_create(struct vela_image *image);

#endif
