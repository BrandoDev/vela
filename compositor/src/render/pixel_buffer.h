// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_RENDER_PIXEL_BUFFER_H
#define VELA_RENDER_PIXEL_BUFFER_H

// Un'immagine disegnata dalla CPU (testo, simboli dei pulsanti) come
// wlr_buffer: il renderer la carica in una texture come un buffer wl_shm.

#include "image.h"

struct wlr_buffer;

// Prende in carico i pixel di `image` (che resta vuota): li libera il
// buffer quando sparisce. Il buffer nasce con un riferimento: lo si lascia
// con wlr_buffer_drop().
struct wlr_buffer *vela_pixel_buffer_create(struct vela_image *image);

#endif
