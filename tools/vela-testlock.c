// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// A test lock program (ext-session-lock-v1): locks, draws on every output
// asking for a frame callback on every frame (like vela-lock with its
// clock), and after the given time unlocks by itself and exits. It tests
// locking and unlocking without a password, in test sessions:
//
//   VELA_LOCK="vela-testlock 1500" vela-compositor ...   then the "lock" command
//
// Usage: vela-testlock [milliseconds] [--keep-surfaces]
//   --keep-surfaces  after unlocking exits without destroying the surfaces
//                    (the disconnection destroys them, as in a crash)

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "ext-session-lock-v1-client-protocol.h"

#define MAX_OUTPUTS 8

struct screen {
    struct wl_output* output;
    struct wl_surface* surface;
    struct ext_session_lock_surface_v1* lock_surface;
    struct wl_buffer* buffer;
    uint32_t width, height;
    unsigned frames;
};

static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct ext_session_lock_manager_v1* manager;
static struct screen screens[MAX_OUTPUTS];
static int screen_count;
static bool locked, finished;

static struct wl_buffer* make_buffer(uint32_t width, uint32_t height, uint32_t color)
{
    const size_t stride = width * 4, size = stride * height;
    const int fd = memfd_create("vela-testlock", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
        return NULL;
    }
    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (size_t i = 0; i < size / 4; ++i) {
        pixels[i] = color;
    }
    munmap(pixels, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer* buffer
        = wl_shm_pool_create_buffer(pool, 0, (int32_t)width, (int32_t)height, (int32_t)stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void draw(struct screen* screen);

static void frame_done(void* data, struct wl_callback* callback, uint32_t time)
{
    wl_callback_destroy(callback);
    draw(data);
}
static const struct wl_callback_listener frame_listener = { frame_done };

// Every frame: damage, a new frame callback and commit, like a clock.
static void draw(struct screen* screen)
{
    if (!screen->buffer) {
        return;
    }
    wl_surface_attach(screen->surface, screen->buffer, 0, 0);
    wl_surface_damage_buffer(screen->surface, 0, 0, 64, 64);
    wl_callback_add_listener(wl_surface_frame(screen->surface), &frame_listener, screen);
    wl_surface_commit(screen->surface);
    screen->frames++;
}

static void lock_surface_configure(void* data, struct ext_session_lock_surface_v1* lock_surface, uint32_t serial,
    uint32_t width, uint32_t height)
{
    struct screen* screen = data;
    ext_session_lock_surface_v1_ack_configure(lock_surface, serial);
    if (screen->width != width || screen->height != height) {
        if (screen->buffer) {
            wl_buffer_destroy(screen->buffer);
        }
        screen->width = width;
        screen->height = height;
        screen->buffer = make_buffer(width, height, 0xff102040);
    }
    draw(screen);
}
static const struct ext_session_lock_surface_v1_listener lock_surface_listener = { lock_surface_configure };

static void lock_locked(void* data, struct ext_session_lock_v1* lock)
{
    locked = true;
}
static void lock_finished(void* data, struct ext_session_lock_v1* lock)
{
    finished = true;
}
static const struct ext_session_lock_v1_listener lock_listener = { lock_locked, lock_finished };

static void global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, wl_output_interface.name) && screen_count < MAX_OUTPUTS) {
        screens[screen_count++].output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    } else if (!strcmp(interface, ext_session_lock_manager_v1_interface.name)) {
        manager = wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
    }
}
static void global_remove(void* data, struct wl_registry* registry, uint32_t name) { }
static const struct wl_registry_listener registry_listener = { global, global_remove };

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(int argc, char* argv[])
{
    const int hold = argc > 1 ? atoi(argv[1]) : 1500;
    const bool keep_surfaces = argc > 2 && !strcmp(argv[2], "--keep-surfaces");
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "vela-testlock: no Wayland display\n");
        return 1;
    }
    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !manager) {
        fprintf(stderr, "vela-testlock: ext-session-lock-v1 is missing\n");
        return 1;
    }

    struct ext_session_lock_v1* lock = ext_session_lock_manager_v1_lock(manager);
    ext_session_lock_v1_add_listener(lock, &lock_listener, NULL);
    for (int i = 0; i < screen_count; ++i) {
        struct screen* screen = &screens[i];
        screen->surface = wl_compositor_create_surface(compositor);
        screen->lock_surface = ext_session_lock_v1_get_lock_surface(lock, screen->surface, screen->output);
        ext_session_lock_surface_v1_add_listener(screen->lock_surface, &lock_surface_listener, screen);
    }

    // Locked, then "hold" milliseconds of frames, then the unlock.
    int64_t until = -1;
    while (!finished && wl_display_dispatch(display) != -1) {
        if (locked && until < 0) {
            until = now_ms() + hold;
            fprintf(stderr, "vela-testlock: locked\n");
        }
        if (until >= 0 && now_ms() >= until) {
            break;
        }
    }
    if (finished) {
        fprintf(stderr, "vela-testlock: the compositor refused the lock\n");
        return 1;
    }
    unsigned frames = 0;
    for (int i = 0; i < screen_count; ++i) {
        frames += screens[i].frames;
    }
    ext_session_lock_v1_unlock_and_destroy(lock);
    if (!keep_surfaces) {
        for (int i = 0; i < screen_count; ++i) {
            ext_session_lock_surface_v1_destroy(screens[i].lock_surface);
            wl_surface_destroy(screens[i].surface);
        }
    }
    wl_display_roundtrip(display);
    fprintf(stderr, "vela-testlock: unlocked after %u frames\n", frames);
    wl_display_disconnect(display);
    return 0;
}
