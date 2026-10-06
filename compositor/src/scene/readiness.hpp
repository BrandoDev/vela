// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

#include "wlr.hpp"

#include <cstdint>

namespace vela::scene {

class ReadyCommits {
public:
    // VELA_READY_WAIT=0 la spegne: i commit si applicano subito e il frame
    // aspetta la GPU dell'app (per confrontare).
    ReadyCommits(wlr_compositor* compositor);
    ~ReadyCommits();

    ReadyCommits(const ReadyCommits&) = delete;
    ReadyCommits& operator=(const ReadyCommits&) = delete;

    bool enabled() const { return m_enabled; }
    // Commit trattenuti dall'avvio, e quanti lo sono ora.
    static uint64_t heldTotal();
    static uint64_t heldNow();

private:
    bool m_enabled = false;
    wl_listener m_newSurface {};
};

} // namespace vela::scene
