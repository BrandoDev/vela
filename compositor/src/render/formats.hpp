// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// I formati dei pixel che il renderer conosce: il codice DRM (quello dei
// buffer di app e schermi) e il formato Vulkan corrispondente.

#include <drm_fourcc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace vela::render {

struct PixelFormat {
    uint32_t drm;
    // Vista "grezza": gli shader leggono i valori così come sono (codificati
    // sRGB e premoltiplicati) e li portano in spazio lineare da sé (§7.5).
    VkFormat unorm;
    // Vista con conversione sRGB in scrittura, per disegnarci sopra: la GPU
    // fonde in lineare e codifica. VK_FORMAT_UNDEFINED se non esiste.
    VkFormat srgb;
    bool alpha; // i formati X... ignorano il quarto canale
    uint32_t bytesPerPixel;
};

inline constexpr PixelFormat pixelFormatTable[] = {
    { DRM_FORMAT_ARGB8888, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, true, 4 },
    { DRM_FORMAT_XRGB8888, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, false, 4 },
    { DRM_FORMAT_ABGR8888, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, true, 4 },
    { DRM_FORMAT_XBGR8888, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, false, 4 },
    { DRM_FORMAT_ARGB2101010, VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_FORMAT_UNDEFINED, true, 4 },
    { DRM_FORMAT_XRGB2101010, VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_FORMAT_UNDEFINED, false, 4 },
    { DRM_FORMAT_ABGR2101010, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_UNDEFINED, true, 4 },
    { DRM_FORMAT_XBGR2101010, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_UNDEFINED, false, 4 },
    { DRM_FORMAT_ABGR16161616F, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_UNDEFINED, true, 8 },
    { DRM_FORMAT_XBGR16161616F, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_UNDEFINED, false, 8 },
    { DRM_FORMAT_RGB565, VK_FORMAT_R5G6B5_UNORM_PACK16, VK_FORMAT_UNDEFINED, false, 2 },
};

inline std::span<const PixelFormat> pixelFormats()
{
    return pixelFormatTable;
}

inline const PixelFormat* pixelFormat(uint32_t drm)
{
    for (const PixelFormat& format : pixelFormatTable) {
        if (format.drm == drm) {
            return &format;
        }
    }
    return nullptr;
}

} // namespace vela::render
