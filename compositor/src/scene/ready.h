// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_READY_H
#define VELA_SCENE_READY_H

// Un'app lenta non ferma lo schermo (docs/renderer.md §7.3). Un commit il
// cui buffer la GPU dell'app non ha ancora finito di disegnare resta in
// attesa (wlr_surface_lock_pending) e diventa lo stato corrente solo quando
// la sua fence è segnalata. Nel frattempo i frame mostrano lo stato
// precedente, già pronto: cursore, animazioni e le altre finestre non
// aspettano la GPU di nessuno.
//
// Vale per la sincronizzazione esplicita (il punto di acquisizione di
// linux-drm-syncobj-v1, quella di Mesa Vulkan, Firefox e dei giochi) e per
// quella implicita (la fence di scrittura del dmabuf, OpenGL). I buffer in
// memoria condivisa sono pronti per definizione.

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct wlr_compositor;

// Una, del server; ogni superficie ha il suo stato (un addon).
struct vela_ready {
    bool enabled; // VELA_READY_WAIT=0: i commit si applicano subito e il frame aspetta la GPU dell'app
    uint64_t held_total; // commit trattenuti dall'avvio
    uint64_t held_now; // e quanti lo sono ora
    struct wl_listener new_surface;
};

void vela_ready_init(struct vela_ready *ready, struct wlr_compositor *compositor);
void vela_ready_finish(struct vela_ready *ready);

#endif
