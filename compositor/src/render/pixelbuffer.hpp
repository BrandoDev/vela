// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Un'immagine disegnata dalla CPU (testo, simboli dei pulsanti) come
// wlr_buffer: il renderer la carica in una texture come un buffer wl_shm.

#include "wlr.hpp"

#include <cstdint>
#include <vector>

namespace vela::render {

// ARGB8888 premoltiplicato, `width` × `height`. Il buffer nasce con un
// riferimento: lo si lascia con wlr_buffer_drop().
wlr_buffer* createPixelBuffer(int width, int height, std::vector<uint32_t> pixels);

} // namespace vela::render
