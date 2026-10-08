// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_TEXT_H
#define VELA_TEXT_H

// Text drawn by the compositor (window titles). FreeType for the glyphs,
// HarfBuzz to lay them out, fontconfig to find the font chosen in KDE
// (docs/renderer.md §9.5). Text is always rasterized at the exact physical
// size: never scaled, sharp at every scale.

#include "image.h"

#include <stdbool.h>
#include <stdint.h>

struct vela_text;

// Loads KDE's font. NULL if no usable font exists.
struct vela_text *vela_text_create(void);
void vela_text_destroy(struct vela_text *text);

// KDE's font size, in logical pixels (10 pt = 13.33 px).
double vela_text_pixel_size(const struct vela_text *text);

// One line of text in `color` (0xAARRGGBB), `pixel_size` physical pixels high,
// at most `max_width` wide (otherwise cut with "…"; 0: no limit). The image is
// freed with vela_image_finish.
void vela_text_render(struct vela_text *text, const char *utf8, double pixel_size, uint32_t color, int max_width,
    struct vela_image *out);

#endif
