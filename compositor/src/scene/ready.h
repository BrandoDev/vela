// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_READY_H
#define VELA_SCENE_READY_H

// A slow app doesn't hold up the screen (docs/renderer.md §7.3). A commit
// whose buffer the app's GPU hasn't finished drawing waits
// (wlr_surface_lock_pending) and becomes the current state only when its fence
// signals. Meanwhile frames show the previous, ready state: cursor, animations
// and other windows wait for nobody's GPU.
//
// This covers explicit sync (the linux-drm-syncobj-v1 acquire point, used by
// Mesa Vulkan, Firefox and games) and implicit sync (the dmabuf write fence,
// OpenGL). Shared-memory buffers are ready by definition.

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct wlr_compositor;

// One, the server's; each surface has its own state (an addon).
struct vela_ready {
    bool enabled; // VELA_READY_WAIT=0: commits apply at once and the frame waits for the app's GPU
    uint64_t held_total; // commits held since startup
    uint64_t held_now; // and how many are held now
    struct wl_listener new_surface;
};

void vela_ready_init(struct vela_ready *ready, struct wlr_compositor *compositor);
void vela_ready_finish(struct vela_ready *ready);

#endif
