// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scene/ready.h"

#include "util.h"

#include <linux/dma-buf.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/util/addon.h>
#include <wlr/util/log.h>
#include <xf86drm.h>

struct gate;
struct held;

// Implicita: una sync_file per dmabuf, nel ciclo degli eventi.
struct held_file {
    struct held *owner;
    int fd;
    struct wl_event_source *source;
};

// Un commit trattenuto: si applica quando tutte le sue fence sono segnalate.
struct held {
    struct wl_list link; // gate.held, in ordine di commit
    struct gate *gate;
    uint32_t seq;
    bool locked;
    int remaining; // fence non ancora segnalate
    // Esplicita: il punto di acquisizione sulla timeline dell'app.
    struct wlr_drm_syncobj_timeline_waiter waiter;
    bool waiting;
    struct held_file files[WLR_DMABUF_MAX_PLANES];
    int file_count;
};

// Lo stato per superficie, appeso come addon alla wlr_surface.
struct gate {
    struct wlr_addon addon;
    struct vela_ready *ready;
    struct wlr_surface *surface;
    struct wl_listener client_commit;
    struct wl_list held; // struct held.link
};

static void disarm(struct held_file *file)
{
    if (file->source) {
        wl_event_source_remove(file->source);
        file->source = NULL;
    }
    if (file->fd >= 0) {
        close(file->fd);
        file->fd = -1;
    }
}

// Smette di aspettare e libera; il commit, se trattenuto, resta a wlroots.
static void held_destroy(struct held *held)
{
    if (held->waiting) {
        wlr_drm_syncobj_timeline_waiter_finish(&held->waiter);
    }
    for (int i = 0; i < held->file_count; ++i) {
        disarm(&held->files[i]);
    }
    if (held->locked) {
        --held->gate->ready->held_now;
    }
    wl_list_remove(&held->link);
    free(held);
}

// La superficie muore con i suoi stati in coda: si smette di aspettare
// senza sbloccarli (wlroots li butta via).
static void handle_surface_destroy(struct wlr_addon *addon)
{
    struct gate *gate = wl_container_of(addon, gate, addon);
    struct held *held, *tmp;
    wl_list_for_each_safe (held, tmp, &gate->held, link) {
        held_destroy(held);
    }
    wl_list_remove(&gate->client_commit.link);
    wlr_addon_finish(addon);
    free(gate);
}

static const struct wlr_addon_interface gate_addon = {
    .name = "vela-ready-commits",
    .destroy = handle_surface_destroy,
};

// Una fence in meno; all'ultima il commit si applica.
static void signaled(struct held *held)
{
    if (--held->remaining > 0) {
        return;
    }
    struct wlr_surface *surface = held->gate->surface;
    uint32_t seq = held->seq;
    // Prima tolto, poi sbloccato: applicare il commit fa partire gli altri
    // handler (mappatura, danno, frame), e questo held non deve più esserci.
    held_destroy(held);
    wlr_surface_unlock_cached(surface, seq);
}

static void handle_syncobj_ready(struct wlr_drm_syncobj_timeline_waiter *waiter)
{
    struct held *held = wl_container_of(waiter, held, waiter);
    wlr_drm_syncobj_timeline_waiter_finish(waiter); // wlroots lo permette nella sua callback
    held->waiting = false;
    signaled(held);
}

static int handle_file_ready(int fd, uint32_t mask, void *data)
{
    struct held_file *file = data;
    struct held *held = file->owner;
    // Una sync_file segnalata resta leggibile: va staccata subito, o il
    // ciclo degli eventi la conterebbe di nuovo.
    disarm(file);
    signaled(held);
    return 0;
}

// Lo stato linux-drm-syncobj-v1 del commit in arrivo. wlroots espone solo
// quello corrente (wlr_linux_drm_syncobj_v1_get_surface_state); quello in
// arrivo è lo stato dello stesso oggetto sincronizzato per surface->pending.
// L'oggetto si riconosce perché il suo stato corrente è quello che wlroots
// restituisce. La lista degli oggetti sincronizzati è privata in wlroots
// 0.20: se cambia, la compilazione si ferma qui.
static struct wlr_linux_drm_syncobj_surface_v1_state *pending_syncobj(struct wlr_surface *surface)
{
    struct wlr_linux_drm_syncobj_surface_v1_state *current = wlr_linux_drm_syncobj_v1_get_surface_state(surface);
    if (!current) {
        return NULL;
    }
    struct wlr_surface_synced *synced;
    wl_list_for_each (synced, &surface->WLR_PRIVATE.synced, link) {
        if (wlr_surface_synced_get_state(synced, &surface->current) == current) {
            return wlr_surface_synced_get_state(synced, &surface->pending);
        }
    }
    return NULL;
}

// Esplicita: si aspetta che il punto sia segnalato (wlroots aspetta solo che
// si materializzi). false se è già pronto o non si può aspettare.
static bool wait_syncobj(struct held *held, struct wlr_linux_drm_syncobj_surface_v1_state *sync,
    struct wl_event_loop *loop)
{
    bool ready = false;
    if (!wlr_drm_syncobj_timeline_check(sync->acquire_timeline, sync->acquire_point,
            DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, &ready)
        || ready) {
        return false;
    }
    if (!wlr_drm_syncobj_timeline_waiter_init(&held->waiter, sync->acquire_timeline, sync->acquire_point, 0, loop,
            handle_syncobj_ready)) {
        return false; // il frame aspetterà la GPU, come prima
    }
    held->waiting = true;
    held->remaining = 1;
    return true;
}

// Implicita: le fence di scrittura dei dmabuf, quelle che chi legge deve
// aspettare, una per ogni descrittore diverso.
static void wait_dmabuf(struct held *held, struct wlr_buffer *buffer, struct wl_event_loop *loop)
{
    struct wlr_dmabuf_attributes dmabuf;
    if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        return; // memoria condivisa: già pronta
    }
    for (int i = 0; i < dmabuf.n_planes; ++i) {
        int fd = dmabuf.fd[i];
        bool seen = false;
        for (int j = 0; j < i && !seen; ++j) {
            seen = dmabuf.fd[j] == fd;
        }
        if (seen) {
            continue;
        }
        struct dma_buf_export_sync_file request = { .flags = DMA_BUF_SYNC_READ, .fd = -1 };
        if (ioctl(fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &request) != 0 || request.fd < 0) {
            continue;
        }
        struct pollfd poll_fd = { .fd = request.fd, .events = POLLIN };
        if (poll(&poll_fd, 1, 0) > 0) {
            close(request.fd); // già segnalata
            continue;
        }
        struct held_file *file = &held->files[held->file_count];
        *file = (struct held_file) { held, request.fd, NULL };
        file->source = wl_event_loop_add_fd(loop, request.fd, WL_EVENT_READABLE, handle_file_ready, file);
        if (!file->source) {
            close(request.fd);
            continue;
        }
        ++held->file_count;
        ++held->remaining;
    }
}

static void handle_client_commit(struct wl_listener *listener, void *data)
{
    struct gate *gate = wl_container_of(listener, gate, client_commit);
    struct wlr_surface *surface = gate->surface;
    const struct wlr_surface_state *pending = &surface->pending;
    if (!(pending->committed & WLR_SURFACE_STATE_BUFFER) || !pending->buffer) {
        return;
    }
    struct wl_event_loop *loop = wl_display_get_event_loop(wl_client_get_display(wl_resource_get_client(surface->resource)));
    struct held *held = calloc(1, sizeof(*held));
    held->gate = gate;
    wl_list_init(&held->link);

    struct wlr_linux_drm_syncobj_surface_v1_state *sync = pending_syncobj(surface);
    if (sync && sync->acquire_timeline) {
        if (!wait_syncobj(held, sync, loop)) {
            free(held);
            return;
        }
    } else {
        wait_dmabuf(held, pending->buffer, loop);
        if (held->remaining == 0) {
            free(held);
            return;
        }
    }

    held->seq = wlr_surface_lock_pending(surface);
    held->locked = true;
    ++gate->ready->held_total;
    ++gate->ready->held_now;
    wl_list_insert(gate->held.prev, &held->link);
}

static void handle_new_surface(struct wl_listener *listener, void *data)
{
    struct vela_ready *ready = wl_container_of(listener, ready, new_surface);
    struct wlr_surface *surface = data;
    struct gate *gate = calloc(1, sizeof(*gate));
    gate->ready = ready;
    gate->surface = surface;
    wl_list_init(&gate->held);
    wlr_addon_init(&gate->addon, &surface->addons, NULL, &gate_addon);
    gate->client_commit.notify = handle_client_commit;
    wl_signal_add(&surface->events.client_commit, &gate->client_commit);
}

void vela_ready_init(struct vela_ready *ready, struct wlr_compositor *compositor)
{
    ready->held_total = 0;
    ready->held_now = 0;
    ready->enabled = !vela_env_off("VELA_READY_WAIT");
    wl_list_init(&ready->new_surface.link);
    if (!ready->enabled) {
        wlr_log(WLR_INFO, "VELA_READY_WAIT=0: frames wait for the apps' GPU");
        return;
    }
    ready->new_surface.notify = handle_new_surface;
    wl_signal_add(&compositor->events.new_surface, &ready->new_surface);
}

void vela_ready_finish(struct vela_ready *ready)
{
    wl_list_remove(&ready->new_surface.link);
    wl_list_init(&ready->new_surface.link);
}
