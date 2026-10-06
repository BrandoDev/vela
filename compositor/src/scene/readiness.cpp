// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scene/readiness.hpp"

#include "listener.hpp"

#include <linux/dma-buf.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drm.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace vela::scene {

namespace {

uint64_t s_heldTotal = 0;
uint64_t s_heldNow = 0;

struct Gate;

// Un commit trattenuto: si applica quando tutte le sue fence sono segnalate.
struct Held {
    Gate* gate = nullptr;
    uint32_t seq = 0;
    bool locked = false;
    int remaining = 0; // fence non ancora segnalate

    // Esplicita: il punto di acquisizione sulla timeline dell'app.
    struct Waiter {
        wlr_drm_syncobj_timeline_waiter waiter; // primo membro: ci si risale con un cast
        Held* owner;
        bool active;
    } syncobj {};

    // Implicita: una sync_file per dmabuf.
    struct File {
        Held* owner;
        int fd;
        wl_event_source* source;
    };
    std::vector<std::unique_ptr<File>> files;

    ~Held()
    {
        if (syncobj.active) {
            wlr_drm_syncobj_timeline_waiter_finish(&syncobj.waiter);
        }
        for (const auto& file : files) {
            disarm(*file);
        }
        if (locked) {
            --s_heldNow;
        }
    }

    static void disarm(File& file)
    {
        if (file.source) {
            wl_event_source_remove(file.source);
            file.source = nullptr;
        }
        if (file.fd >= 0) {
            close(file.fd);
            file.fd = -1;
        }
    }
};

// Lo stato per superficie, appeso come addon alla wlr_surface.
struct Gate {
    struct Shim {
        wlr_addon addon; // primo membro: ci si risale con un cast
        Gate* owner;
    } shim {};
    wlr_surface* surface = nullptr;
    Listener clientCommit;
    std::vector<std::unique_ptr<Held>> held;
};

void destroyGate(wlr_addon* addon)
{
    // La superficie muore con i suoi stati in coda: si smette di aspettare
    // senza sbloccarli (wlroots li butta via).
    Gate* gate = reinterpret_cast<Gate::Shim*>(addon)->owner;
    wlr_addon_finish(addon);
    delete gate;
}

const wlr_addon_interface gateAddon {
    .name = "vela-ready-commits",
    .destroy = destroyGate,
};

// Una fence in meno; all'ultima il commit si applica.
void signaled(Held* held)
{
    if (--held->remaining > 0) {
        return;
    }
    Gate* gate = held->gate;
    const uint32_t seq = held->seq;
    // Prima tolto, poi sbloccato: applicare il commit fa partire gli altri
    // handler (mappatura, danno, frame), e questo Held non deve più esserci.
    auto it = std::find_if(gate->held.begin(), gate->held.end(),
        [held](const std::unique_ptr<Held>& entry) { return entry.get() == held; });
    gate->held.erase(it);
    wlr_surface_unlock_cached(gate->surface, seq);
}

void onSyncobjReady(wlr_drm_syncobj_timeline_waiter* waiter)
{
    auto* shim = reinterpret_cast<Held::Waiter*>(waiter);
    wlr_drm_syncobj_timeline_waiter_finish(waiter); // wlroots lo permette nella sua callback
    shim->active = false;
    signaled(shim->owner);
}

int onFileReady(int, uint32_t, void* data)
{
    auto* file = static_cast<Held::File*>(data);
    Held* held = file->owner;
    // Una sync_file segnalata resta leggibile: va staccata subito, o il
    // ciclo degli eventi la conterebbe di nuovo.
    Held::disarm(*file);
    signaled(held);
    return 0;
}

// Lo stato linux-drm-syncobj-v1 del commit in arrivo. wlroots espone solo
// quello corrente (wlr_linux_drm_syncobj_v1_get_surface_state); quello in
// arrivo è lo stato dello stesso oggetto sincronizzato per surface->pending.
// L'oggetto si riconosce perché il suo stato corrente è quello che wlroots
// restituisce. La lista degli oggetti sincronizzati è privata in wlroots
// 0.20: se cambia, la compilazione si ferma qui.
wlr_linux_drm_syncobj_surface_v1_state* pendingSyncobj(wlr_surface* surface)
{
    wlr_linux_drm_syncobj_surface_v1_state* current = wlr_linux_drm_syncobj_v1_get_surface_state(surface);
    if (!current) {
        return nullptr;
    }
    wlr_surface_synced* synced;
    wl_list_for_each(synced, &surface->WLR_PRIVATE.synced, link)
    {
        if (wlr_surface_synced_get_state(synced, &surface->current) == current) {
            return static_cast<wlr_linux_drm_syncobj_surface_v1_state*>(
                wlr_surface_synced_get_state(synced, &surface->pending));
        }
    }
    return nullptr;
}

void onClientCommit(Gate* gate)
{
    wlr_surface* surface = gate->surface;
    const wlr_surface_state& pending = surface->pending;
    if (!(pending.committed & WLR_SURFACE_STATE_BUFFER) || !pending.buffer) {
        return;
    }
    wl_event_loop* loop = wl_display_get_event_loop(wl_client_get_display(wl_resource_get_client(surface->resource)));
    auto held = std::make_unique<Held>();
    held->gate = gate;

    if (wlr_linux_drm_syncobj_surface_v1_state* sync = pendingSyncobj(surface); sync && sync->acquire_timeline) {
        // Esplicita. Un punto non ancora materializzato lo aspetta anche
        // wlroots; noi aspettiamo che sia segnalato (flag 0).
        bool ready = false;
        if (!wlr_drm_syncobj_timeline_check(sync->acquire_timeline, sync->acquire_point,
                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, &ready)
            || ready) {
            return;
        }
        held->syncobj.owner = held.get();
        if (!wlr_drm_syncobj_timeline_waiter_init(&held->syncobj.waiter, sync->acquire_timeline,
                sync->acquire_point, 0, loop, onSyncobjReady)) {
            return; // il frame aspetterà la GPU, come prima
        }
        held->syncobj.active = true;
        held->remaining = 1;
    } else {
        // Implicita: le fence di scrittura dei dmabuf, quelle che chi legge
        // deve aspettare.
        wlr_dmabuf_attributes dmabuf {};
        if (!wlr_buffer_get_dmabuf(pending.buffer, &dmabuf)) {
            return; // memoria condivisa: già pronta
        }
        for (int i = 0; i < dmabuf.n_planes; ++i) {
            const int fd = dmabuf.fd[i];
            if (std::find(dmabuf.fd, dmabuf.fd + i, fd) != dmabuf.fd + i) {
                continue;
            }
            dma_buf_export_sync_file request { .flags = DMA_BUF_SYNC_READ, .fd = -1 };
            if (ioctl(fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &request) != 0 || request.fd < 0) {
                continue;
            }
            pollfd poll { .fd = request.fd, .events = POLLIN, .revents = 0 };
            if (::poll(&poll, 1, 0) > 0) {
                close(request.fd); // già segnalata
                continue;
            }
            auto file = std::make_unique<Held::File>(Held::File { held.get(), request.fd, nullptr });
            file->source = wl_event_loop_add_fd(loop, request.fd, WL_EVENT_READABLE, onFileReady, file.get());
            if (!file->source) {
                close(request.fd);
                continue;
            }
            held->files.push_back(std::move(file));
            ++held->remaining;
        }
        if (held->remaining == 0) {
            return;
        }
    }

    held->seq = wlr_surface_lock_pending(surface);
    held->locked = true;
    ++s_heldTotal;
    ++s_heldNow;
    gate->held.push_back(std::move(held));
}

} // namespace

ReadyCommits::ReadyCommits(wlr_compositor* compositor)
{
    const char* wait = std::getenv("VELA_READY_WAIT");
    m_enabled = !(wait && std::strcmp(wait, "0") == 0);
    if (!m_enabled) {
        wlr_log(WLR_INFO, "VELA_READY_WAIT=0: frames wait for the apps' GPU");
        return;
    }
    m_newSurface.notify = [](wl_listener*, void* data) {
        auto* surface = static_cast<wlr_surface*>(data);
        auto* gate = new Gate;
        gate->surface = surface;
        gate->shim.owner = gate;
        wlr_addon_init(&gate->shim.addon, &surface->addons, nullptr, &gateAddon);
        gate->clientCommit.connect(&surface->events.client_commit, [gate](void*) { onClientCommit(gate); });
    };
    wl_signal_add(&compositor->events.new_surface, &m_newSurface);
}

ReadyCommits::~ReadyCommits()
{
    if (m_newSurface.link.next) {
        wl_list_remove(&m_newSurface.link);
    }
}

uint64_t ReadyCommits::heldTotal()
{
    return s_heldTotal;
}

uint64_t ReadyCommits::heldNow()
{
    return s_heldNow;
}

} // namespace vela::scene
