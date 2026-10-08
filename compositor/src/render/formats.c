// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/render.h"

#include <drm_fourcc.h>

const struct vela_pixel_format vela_pixel_formats[] = {
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

const int vela_pixel_format_count = sizeof(vela_pixel_formats) / sizeof(vela_pixel_formats[0]);

const struct vela_pixel_format *vela_pixel_format_from_drm(uint32_t drm)
{
    for (int i = 0; i < vela_pixel_format_count; ++i) {
        if (vela_pixel_formats[i].drm == drm) {
            return &vela_pixel_formats[i];
        }
    }
    return NULL;
}
