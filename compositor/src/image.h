// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_IMAGE_H
#define VELA_IMAGE_H

// An image drawn by the CPU (text, icons, button glyphs): premultiplied
// ARGB8888, row after row with no padding. `pixels` comes from malloc and
// belongs to whoever holds the image; render/pixel_buffer.h can take it over
// as a wlr_buffer.

#include <stdint.h>
#include <stdlib.h>

struct vela_image {
    int width;
    int height;
    uint32_t *pixels; // NULL: empty image
};

static inline void vela_image_finish(struct vela_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

#endif
