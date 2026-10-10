// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_screen.h"

#include <errno.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <drm.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wlr/util/log.h>

// One line on a dark background, rendered on the CPU. Vulkan, the shell and
// Wayland do not exist yet when this fallback is needed.
static void draw_message(uint32_t *pixels, uint32_t pitch, uint32_t width, uint32_t height)
{
    const char *message = "Vela needs Vulkan 1.4";
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t *row = (uint32_t *)((unsigned char *)pixels + (size_t)y * pitch);
        for (uint32_t x = 0; x < width; ++x) row[x] = 0x111419;
    }

    FT_Library library = NULL;
    FT_Face face = NULL;
    FcPattern *pattern = NULL;
    FcPattern *match = NULL;
    if (!FcInit() || FT_Init_FreeType(&library) != 0) goto finish;
    pattern = FcNameParse((const FcChar8 *)"sans");
    if (!pattern) goto finish;
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    match = FcFontMatch(NULL, pattern, &result);
    FcChar8 *path = NULL;
    int index = 0;
    if (!match || FcPatternGetString(match, FC_FILE, 0, &path) != FcResultMatch) goto finish;
    FcPatternGetInteger(match, FC_INDEX, 0, &index);
    if (FT_New_Face(library, (const char *)path, index, &face) != 0) goto finish;

    unsigned size = height / 25;
    if (size < 18) size = 18;
    if (size > 40) size = 40;
    int text_width;
    do {
        FT_Set_Pixel_Sizes(face, 0, size);
        text_width = 0;
        for (const unsigned char *c = (const unsigned char *)message; *c; ++c) {
            if (FT_Load_Char(face, *c, FT_LOAD_DEFAULT) == 0)
                text_width += (int)(face->glyph->advance.x >> 6);
        }
    } while (text_width > (int)width - 32 && size-- > 12);

    int x = ((int)width - text_width) / 2;
    int baseline = ((int)height + (int)(face->size->metrics.ascender >> 6)
                    + (int)(face->size->metrics.descender >> 6)) / 2;
    for (const unsigned char *c = (const unsigned char *)message; *c; ++c) {
        if (FT_Load_Char(face, *c, FT_LOAD_RENDER) != 0) continue;
        FT_GlyphSlot glyph = face->glyph;
        FT_Bitmap *bitmap = &glyph->bitmap;
        int left = x + glyph->bitmap_left;
        int top = baseline - glyph->bitmap_top;
        if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
            for (unsigned row = 0; row < bitmap->rows; ++row) {
                int py = top + (int)row;
                if (py < 0 || py >= (int)height) continue;
                uint32_t *dst = (uint32_t *)((unsigned char *)pixels + (size_t)py * pitch);
                const unsigned char *src = bitmap->buffer + (bitmap->pitch < 0
                    ? (bitmap->rows - 1 - row) * (unsigned)(-bitmap->pitch)
                    : row * (unsigned)bitmap->pitch);
                for (unsigned col = 0; col < bitmap->width; ++col) {
                    int px = left + (int)col;
                    if (px < 0 || px >= (int)width) continue;
                    uint32_t a = src[col];
                    uint32_t r = (0x11 * (255 - a) + 0xf2 * a + 127) / 255;
                    uint32_t g = (0x14 * (255 - a) + 0xf4 * a + 127) / 255;
                    uint32_t b = (0x19 * (255 - a) + 0xf8 * a + 127) / 255;
                    dst[px] = (r << 16) | (g << 8) | b;
                }
            }
        }
        x += (int)(glyph->advance.x >> 6);
    }
finish:
    if (face) FT_Done_Face(face);
    if (library) FT_Done_FreeType(library);
    if (match) FcPatternDestroy(match);
    if (pattern) FcPatternDestroy(pattern);
}

// Prefer the connector's active CRTC; otherwise find a compatible one.
static uint32_t find_crtc(int fd, drmModeRes *res, drmModeConnector *connector)
{
    drmModeEncoder *encoder = connector->encoder_id ? drmModeGetEncoder(fd, connector->encoder_id) : NULL;
    uint32_t crtc = encoder ? encoder->crtc_id : 0;
    if (encoder) drmModeFreeEncoder(encoder);
    if (crtc) return crtc;

    for (int e = 0; e < connector->count_encoders; ++e) {
        encoder = drmModeGetEncoder(fd, connector->encoders[e]);
        if (!encoder) continue;
        for (int c = 0; c < res->count_crtcs && c < 32; ++c) {
            if (encoder->possible_crtcs & (1u << c)) {
                crtc = res->crtcs[c];
                break;
            }
        }
        drmModeFreeEncoder(encoder);
        if (crtc) break;
    }
    return crtc;
}

void vela_error_screen_no_vulkan(int drm_fd)
{
    if (drm_fd < 0) return; // nested/headless: no physical KMS output
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) return;

    for (int c = 0; c < res->count_connectors; ++c) {
        drmModeConnector *connector = drmModeGetConnector(drm_fd, res->connectors[c]);
        if (!connector) continue;
        if (connector->connection != DRM_MODE_CONNECTED || connector->count_modes == 0) {
            drmModeFreeConnector(connector);
            continue;
        }
        uint32_t crtc = find_crtc(drm_fd, res, connector);
        if (!crtc) {
            drmModeFreeConnector(connector);
            continue;
        }
        drmModeModeInfo *mode = &connector->modes[0];
        for (int m = 0; m < connector->count_modes; ++m) {
            if (connector->modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                mode = &connector->modes[m];
                break;
            }
        }

        struct drm_mode_create_dumb create = {
            .width = mode->hdisplay, .height = mode->vdisplay, .bpp = 32,
        };
        uint32_t fb = 0;
        void *mapped = MAP_FAILED;
        if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) goto next;
        if (drmModeAddFB(drm_fd, create.width, create.height, 24, 32,
                         create.pitch, create.handle, &fb) != 0) goto cleanup;
        struct drm_mode_map_dumb map = { .handle = create.handle };
        if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) goto cleanup;
        mapped = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, map.offset);
        if (mapped == MAP_FAILED) goto cleanup;
        draw_message(mapped, create.pitch, create.width, create.height);
        if (drmModeSetCrtc(drm_fd, crtc, fb, 0, 0, &connector->connector_id, 1, mode) == 0) {
            wlr_log(WLR_INFO, "Displaying Vulkan requirement for six seconds");
            struct timespec delay = { .tv_sec = 6 };
            while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {}
            // SDDM reclaims the output when this process/session exits.
            if (mapped != MAP_FAILED) munmap(mapped, create.size);
            if (fb) drmModeRmFB(drm_fd, fb);
            struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
            ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
            drmModeFreeConnector(connector);
            break;
        }
cleanup:
        if (mapped != MAP_FAILED) munmap(mapped, create.size);
        if (fb) drmModeRmFB(drm_fd, fb);
        {
            struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
            ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
next:
        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(res);
}
