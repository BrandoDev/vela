// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/pixelbuffer.hpp"

#include <drm_fourcc.h>

#include <algorithm>

namespace vela::render {

namespace {

// wlr_buffer come primo membro di una struttura semplice: dal puntatore
// che wlroots ci passa si risale ai pixel.
struct PixelBuffer {
    wlr_buffer base;
    uint32_t* pixels;
};

void destroyBuffer(wlr_buffer* buffer)
{
    auto* self = reinterpret_cast<PixelBuffer*>(buffer);
    delete[] self->pixels;
    delete self;
}

bool beginAccess(wlr_buffer* buffer, uint32_t, void** data, uint32_t* format, size_t* stride)
{
    auto* self = reinterpret_cast<PixelBuffer*>(buffer);
    *data = self->pixels;
    *format = DRM_FORMAT_ARGB8888;
    *stride = size_t(buffer->width) * 4;
    return true;
}

void endAccess(wlr_buffer*) { }

const wlr_buffer_impl pixelBufferImpl {
    .destroy = destroyBuffer,
    .get_dmabuf = nullptr,
    .get_shm = nullptr,
    .begin_data_ptr_access = beginAccess,
    .end_data_ptr_access = endAccess,
};

} // namespace

wlr_buffer* createPixelBuffer(int width, int height, std::vector<uint32_t> pixels)
{
    auto* buffer = new PixelBuffer {};
    const size_t count = size_t(width) * size_t(height);
    buffer->pixels = new uint32_t[count]();
    std::copy_n(pixels.begin(), std::min(count, pixels.size()), buffer->pixels);
    wlr_buffer_init(&buffer->base, &pixelBufferImpl, width, height);
    return &buffer->base;
}

} // namespace vela::render
