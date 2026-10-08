// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_IMAGE_H
#define VELA_IMAGE_H

// Un'immagine disegnata dalla CPU (testo, icone, simboli dei pulsanti):
// ARGB8888 premoltiplicato, riga dopo riga, senza spazi tra le righe.
// `pixels` è allocato con malloc e appartiene a chi tiene l'immagine;
// render/pixel_buffer.h può prenderlo in carico come wlr_buffer.

#include <stdint.h>
#include <stdlib.h>

struct vela_image {
    int width;
    int height;
    uint32_t *pixels; // NULL: immagine vuota
};

static inline void vela_image_finish(struct vela_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

#endif
