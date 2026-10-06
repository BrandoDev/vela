// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// vela-pattern: client di prova della nitidezza (docs/renderer.md §3.10).
//
// Apre una finestra senza decorazioni e la riempie, alla scala frazionaria
// esatta chiesta dal compositor (fractional-scale-v1 + viewporter), con un
// buffer grande quanto i suoi pixel fisici. Ogni pixel codifica le proprie
// coordinate nel buffer:
//
//     R = x % 256,  G = y % 256,  B = (x / 256) * 16 + (y / 256)
//
// Sullo schermo, se il compositor copia il buffer 1:1, ogni pixel della
// finestra "dice" la stessa origine: il test (scripts/test-sharpness.sh)
// la ritrova e confronta tutto bit per bit. Righe e colonne alternate e
// valori vicini tra loro sono il caso peggiore per qualunque filtro.
//
// Uso: vela-pattern [--scala1] [--app-id ID] [LARGHEZZA ALTEZZA]   (logiche; predefinito 401x301)
//
// --scala1: come un'app vecchia, disegna sempre a scala 1 (il compositor
// deve ingrandire): righe di un pixel e scacchiera, per giudicare il filtro.

#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wmBase;
static struct wp_viewporter* viewporter;
static struct wp_fractional_scale_manager_v1* fractionalManager;

static struct wl_surface* surface;
static struct wp_viewport* viewport;
static struct xdg_surface* xdgSurface;
static struct xdg_toplevel* toplevel;

static int logicalWidth = 401;
static int logicalHeight = 301;
static int pendingWidth;
static int pendingHeight;
static uint32_t scale120 = 120;
static int configured;
static int legacy;
static int running = 1;

static void onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        wmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    } else if (!strcmp(interface, wp_viewporter_interface.name)) {
        viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (!strcmp(interface, wp_fractional_scale_manager_v1_interface.name)) {
        fractionalManager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    }
}

static void onGlobalRemove(void* data, struct wl_registry* registry, uint32_t name) { }

static const struct wl_registry_listener registryListener = { onGlobal, onGlobalRemove };

static void onBufferRelease(void* data, struct wl_buffer* buffer)
{
    wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener bufferListener = { onBufferRelease };

// Dimensione del buffer come vuole fractional-scale-v1: la dimensione
// logica per la scala, arrotondata.
static int physical(int logical)
{
    return (int)((logical * (int64_t)scale120 + 60) / 120);
}

static void draw(void)
{
    const int width = physical(logicalWidth);
    const int height = physical(logicalHeight);
    const int stride = width * 4;
    const size_t size = (size_t)stride * height;

    const int fd = memfd_create("vela-pattern", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) < 0) {
        perror("vela-pattern: memfd");
        exit(1);
    }
    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t color;
            if (legacy) {
                // Quattro quadranti: righe orizzontali e verticali di un
                // pixel, scacchiera, e un bordo nero su bianco.
                const int left = x < width / 2;
                const int top = y < height / 2;
                const int on = top ? (left ? y & 1 : x & 1) : (left ? (x ^ y) & 1 : (x % 16 == 0 || y % 16 == 0));
                color = on ? 0xff000000u : 0xffffffffu;
            } else {
                const uint32_t r = (uint32_t)x & 255;
                const uint32_t g = (uint32_t)y & 255;
                const uint32_t b = ((uint32_t)(x >> 8) & 15) << 4 | ((uint32_t)(y >> 8) & 15);
                color = 0xff000000u | r << 16 | g << 8 | b;
            }
            pixels[y * width + x] = color;
        }
    }
    munmap(pixels, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_buffer_add_listener(buffer, &bufferListener, NULL);
    wl_shm_pool_destroy(pool);
    close(fd);

    wp_viewport_set_destination(viewport, logicalWidth, logicalHeight);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    wl_surface_commit(surface);
    fprintf(stderr, "vela-pattern: %dx%d logici, scala %u/120, buffer %dx%d\n", logicalWidth, logicalHeight,
        scale120, width, height);
}

static void onPreferredScale(void* data, struct wp_fractional_scale_v1* fractional, uint32_t scale)
{
    if (legacy || scale == scale120) {
        return;
    }
    scale120 = scale;
    if (configured) {
        draw();
    }
}

static const struct wp_fractional_scale_v1_listener fractionalListener = { onPreferredScale };

static void onPing(void* data, struct xdg_wm_base* base, uint32_t serial)
{
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wmBaseListener = { onPing };

static void onSurfaceConfigure(void* data, struct xdg_surface* xdg, uint32_t serial)
{
    xdg_surface_ack_configure(xdg, serial);
    if (pendingWidth > 0 && pendingHeight > 0) {
        logicalWidth = pendingWidth;
        logicalHeight = pendingHeight;
    }
    configured = 1;
    draw();
}

static const struct xdg_surface_listener surfaceListener = { onSurfaceConfigure };

static void onToplevelConfigure(void* data, struct xdg_toplevel* t, int32_t width, int32_t height, struct wl_array* states)
{
    pendingWidth = width;
    pendingHeight = height;
}

static void onToplevelClose(void* data, struct xdg_toplevel* t)
{
    running = 0;
}

static void onConfigureBounds(void* data, struct xdg_toplevel* t, int32_t width, int32_t height) { }
static void onWmCapabilities(void* data, struct xdg_toplevel* t, struct wl_array* capabilities) { }

static const struct xdg_toplevel_listener toplevelListener = {
    onToplevelConfigure, onToplevelClose, onConfigureBounds, onWmCapabilities,
};

int main(int argc, char** argv)
{
    int arg = 1;
    const char* appId = "vela.pattern";
    if (arg < argc && !strcmp(argv[arg], "--scala1")) {
        legacy = 1;
        ++arg;
    }
    // --app-id: per le prove che dipendono dall'app (dialoghi di sistema).
    if (arg + 1 < argc && !strcmp(argv[arg], "--app-id")) {
        appId = argv[arg + 1];
        arg += 2;
    }
    if (arg + 1 < argc) {
        logicalWidth = atoi(argv[arg]);
        logicalHeight = atoi(argv[arg + 1]);
    }
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "vela-pattern: nessuna sessione Wayland (WAYLAND_DISPLAY)\n");
        return 1;
    }
    wl_registry_add_listener(wl_display_get_registry(display), &registryListener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !wmBase || !viewporter || !fractionalManager) {
        fprintf(stderr, "vela-pattern: mancano protocolli (viewporter, fractional-scale-v1)\n");
        return 1;
    }
    xdg_wm_base_add_listener(wmBase, &wmBaseListener, NULL);

    surface = wl_compositor_create_surface(compositor);
    viewport = wp_viewporter_get_viewport(viewporter, surface);
    struct wp_fractional_scale_v1* fractional
        = wp_fractional_scale_manager_v1_get_fractional_scale(fractionalManager, surface);
    wp_fractional_scale_v1_add_listener(fractional, &fractionalListener, NULL);
    xdgSurface = xdg_wm_base_get_xdg_surface(wmBase, surface);
    xdg_surface_add_listener(xdgSurface, &surfaceListener, NULL);
    toplevel = xdg_surface_get_toplevel(xdgSurface);
    xdg_toplevel_add_listener(toplevel, &toplevelListener, NULL);
    xdg_toplevel_set_title(toplevel, "vela-pattern");
    xdg_toplevel_set_app_id(toplevel, appId);
    wl_surface_commit(surface);

    while (running && wl_display_dispatch(display) != -1) {
    }
    return 0;
}
