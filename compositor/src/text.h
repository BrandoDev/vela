// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_TEXT_H
#define VELA_TEXT_H

// Il testo disegnato dal compositor (titoli delle finestre; più avanti
// l'overlay di debug). FreeType per i glifi, HarfBuzz per disporli,
// fontconfig per trovare il font scelto in KDE (docs/renderer.md §9.5).
// Il testo si rasterizza sempre alla dimensione fisica esatta: mai
// ingrandito, nitido a ogni scala.

#include "image.h"

#include <stdbool.h>
#include <stdint.h>

struct vela_text;

// Carica il font di KDE. NULL se non c'è nessun font utilizzabile.
struct vela_text *vela_text_create(void);
void vela_text_destroy(struct vela_text *text);

// La dimensione del font di KDE, in pixel logici (10 pt = 13,33 px).
double vela_text_pixel_size(const struct vela_text *text);

// Una riga di testo in `color` (0xAARRGGBB), alta `pixel_size` pixel
// fisici, larga al massimo `max_width` (altrimenti tagliata con "…"; 0:
// senza limite). L'immagine va liberata con vela_image_finish.
void vela_text_render(struct vela_text *text, const char *utf8, double pixel_size, uint32_t color, int max_width,
    struct vela_image *out);

#endif
