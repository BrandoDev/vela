#include "server.hpp"

#include <sys/timerfd.h>

namespace vela {

namespace {

// Molti monitor ad alta frequenza dichiarano come "preferita" una modalità a
// 60 Hz. Noi prendiamo la risoluzione preferita alla frequenza più alta
// disponibile: sul tuo monitor, 180 Hz senza toccare nulla.
wlr_output_mode* pickMode(wlr_output* output)
{
    wlr_output_mode* preferred = wlr_output_preferred_mode(output);
    if (!preferred) {
        return nullptr; // es. backend annidato: nessuna lista di modalità
    }
    wlr_output_mode* best = preferred;
    wlr_output_mode* mode;
    wl_list_for_each(mode, &output->modes, link)
    {
        if (mode->width == preferred->width && mode->height == preferred->height
            && mode->refresh > best->refresh) {
            best = mode;
        }
    }
    return best;
}

bool envFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value && std::strcmp(value, "0") != 0;
}

} // namespace

Output::Output(Server& s, wlr_output* output)
    : server(s)
    , wlr(output)
{
    wlr->data = this;
    // Tutto ciò che si disegna su questo schermo passa dal renderer di Vela,
    // anche ciò che fa wlroots (cursore, catture).
    wlr_output_init_render(wlr, server.allocator, server.renderer);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (wlr_output_mode* mode = pickMode(wlr)) {
        wlr_output_state_set_mode(&state, mode);
    } else if (const char* size = std::getenv("VELA_OUTPUT_SIZE")) {
        // Schermo senza modalità (finestra annidata, headless): la
        // dimensione la scegliamo noi, es. VELA_OUTPUT_SIZE=1920x1080, con
        // la frequenza facoltativa per le prove: 1920x1080@144.
        int width = 0;
        int height = 0;
        double hz = 0.0;
        const int fields = std::sscanf(size, "%dx%d@%lf", &width, &height, &hz);
        if (fields >= 2 && width > 0 && height > 0) {
            wlr_output_state_set_custom_mode(&state, width, height, fields == 3 ? int32_t(hz * 1000.0) : 0);
        }
    }
    if (const char* scale = std::getenv("VELA_SCALE")) {
        wlr_output_state_set_scale(&state, std::strtof(scale, nullptr));
    }
    // VRR: utile nei giochi, ma su alcuni monitor fa sfarfallare il desktop.
    // Spento di default, come su Windows; VELA_VRR=1 per provarlo.
    if (envFlag("VELA_VRR")) {
        wlr_output_state_set_adaptive_sync_enabled(&state, true);
        if (!wlr_output_test_state(wlr, &state)) {
            wlr_log(WLR_INFO, "%s: VRR non supportato", wlr->name);
            wlr_output_state_set_adaptive_sync_enabled(&state, false);
        }
    }
    wlr_output_commit_state(wlr, &state);
    wlr_output_state_finish(&state);

    wlr_log(WLR_INFO, "Schermo %s: %dx%d @ %.2f Hz, scala %.2f", wlr->name, wlr->width, wlr->height,
        wlr->refresh / 1000.0, wlr->scale);

    sceneFrame = std::make_unique<scene::OutputFrame>(*server.sceneGraph, *server.velaRenderer, wlr);
    sceneFrame->scheduleFrame = [this] { scheduleFrame(); };

    clock.setModeRefresh(wlr->refresh);
    if (wlr_output_is_headless(wlr)) {
        startVirtualVblank();
    } else {
        present.connect(&wlr->events.present, [this](void* data) {
            auto* event = static_cast<wlr_output_event_present*>(data);
            if (event->presented) {
                clock.presented(event->commit_seq, render::toNs(event->when), event->refresh);
            }
        });
        frame.connect(&wlr->events.frame, [this](void*) { onFrame(); });
    }
    requestState.connect(&wlr->events.request_state, [this](void* data) {
        // Nel backend annidato: la finestra ospite è stata ridimensionata.
        auto* event = static_cast<wlr_output_event_request_state*>(data);
        wlr_output_commit_state(wlr, event->state);
        clock.setModeRefresh(wlr->refresh);
        arrangeLayers();
        scheduleFrame();
    });
    destroy.connect(&wlr->events.destroy, [this](void*) { delete this; });

    wlr_output_layout_add_auto(server.outputLayout, wlr);

    usable = box();
    server.outputs.push_back(this);
    scheduleFrame();
}

Output::~Output()
{
    // Le superfici della shell legate a questo schermo vanno chiuse.
    std::vector<LayerSurface*> orphans;
    for (LayerSurface* layer : server.layerSurfaces) {
        if (layer->wlr->output == wlr) {
            orphans.push_back(layer);
        }
    }
    for (LayerSurface* layer : orphans) {
        layer->wlr->output = nullptr;
        wlr_layer_surface_v1_destroy(layer->wlr);
    }
    server.endSnapZone(false); // l'anteprima potrebbe essere su questo schermo
    server.outputs.remove(this);
    wlr->data = nullptr;
    if (m_vblankSource) {
        wl_event_source_remove(m_vblankSource);
    }
    if (m_vblankFd >= 0) {
        close(m_vblankFd);
    }
    sceneFrame.reset();
}

void Output::scheduleFrame()
{
    if (m_vblankFd >= 0) {
        m_frameRequested = true;
        armVirtualVblank();
        return;
    }
    wlr_output_schedule_frame(wlr);
}

// ---------------------------------------------------------- vblank virtuale --

void Output::startVirtualVblank()
{
    m_vblankFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (m_vblankFd < 0) {
        wlr_log_errno(WLR_ERROR, "%s: timerfd per il vblank virtuale", wlr->name);
        return;
    }
    m_vblankSource = wl_event_loop_add_fd(server.loop, m_vblankFd, WL_EVENT_READABLE,
        [](int, uint32_t, void* data) {
            static_cast<Output*>(data)->onVirtualVblank();
            return 0;
        },
        this);
    m_lastVblankNs = render::nowNs();
    wlr_log(WLR_INFO, "%s: vblank virtuale ogni %.3f ms", wlr->name, clock.periodNs() / 1e6);
}

// Il prossimo vblank sulla griglia esatta (ultimo vblank + multipli del
// periodo), anche se il timer è rimasto fermo a lungo.
void Output::armVirtualVblank()
{
    if (m_vblankArmed || m_vblankFd < 0) {
        return;
    }
    const int64_t period = clock.periodNs();
    const int64_t now = render::nowNs();
    int64_t next = m_lastVblankNs + period;
    if (next <= now) {
        next = m_lastVblankNs + ((now - m_lastVblankNs) / period + 1) * period;
    }
    const itimerspec when { .it_interval = {}, .it_value = { time_t(next / 1'000'000'000), long(next % 1'000'000'000) } };
    timerfd_settime(m_vblankFd, TFD_TIMER_ABSTIME, &when, nullptr);
    m_vblankArmed = true;
}

void Output::onVirtualVblank()
{
    uint64_t expirations = 0;
    if (read(m_vblankFd, &expirations, sizeof(expirations)) < 0) {
        return;
    }
    m_vblankArmed = false;
    const int64_t period = clock.periodNs();
    const int64_t now = render::nowNs();
    // Il vblank che ha fatto scattare il timer, sulla griglia.
    const int64_t vblank = m_lastVblankNs + std::max<int64_t>(1, (now - m_lastVblankNs) / period) * period;

    // Il frame consegnato prima di questo vblank diventa luce adesso. Se
    // doveva comparire a un vblank precedente, quelli nel mezzo sono persi.
    if (m_awaitingPresent) {
        if (vblank > m_targetVblankNs) {
            clock.missed(int((vblank - m_targetVblankNs) / period));
        }
        clock.presented(m_awaitingSeq, vblank, period);
        m_awaitingPresent = false;
    }
    m_lastVblankNs = vblank;

    if (m_frameRequested) {
        m_frameRequested = false;
        onFrame();
    }
}

wlr_box Output::box() const
{
    wlr_box result {};
    wlr_output_layout_get_box(server.outputLayout, wlr, &result);
    return result;
}

void Output::onFrame()
{
    const int64_t now = render::nowNs();
    const int64_t presentAt = clock.predict(now);

    // Prima si fa avanzare ogni animazione all'istante in cui questo frame
    // diventerà luce (docs/renderer.md §4.2), poi si disegna.
    server.tickAnimations(presentAt);

    const wlr_box area = box();
    if (sceneFrame->render(area.x, area.y)) {
        clock.committed(wlr->commit_seq, presentAt);
        m_awaitingPresent = true;
        m_awaitingSeq = wlr->commit_seq;
        // Col vblank virtuale il frame va "mostrato" al prossimo battito.
        if (m_vblankFd >= 0) {
            const int64_t period = clock.periodNs();
            m_targetVblankNs = m_lastVblankNs + ((now - m_lastVblankNs) / period + 1) * period;
            armVirtualVblank();
        }
    }

    timespec nowTs {};
    clock_gettime(CLOCK_MONOTONIC, &nowTs);
    sceneFrame->sendFrameDone(nowTs);

    double fps = 0.0;
    double errorMean = 0.0;
    double errorMax = 0.0;
    int missed = 0;
    if (clock.takeStats(now, fps, errorMean, errorMax, missed) && fps > 0.0) {
        wlr_log(WLR_DEBUG, "%s: %.2f fps (periodo %.3f ms, latenza %d vblank), errore di previsione "
                           "medio %.3f ms, massimo %.3f ms, vblank persi %d",
            wlr->name, fps, clock.periodNs() / 1e6, clock.latencyFrames(), errorMean, errorMax, missed);
    }
}

void Output::arrangeLayers()
{
    const wlr_box full = box();
    wlr_box area = full;

    // Prima le superfici che riservano spazio (la taskbar), poi le altre,
    // dall'alto verso il basso: è l'ordine usato da sway.
    const zwlr_layer_shell_v1_layer order[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };
    for (bool exclusive : { true, false }) {
        for (zwlr_layer_shell_v1_layer layer : order) {
            for (LayerSurface* surface : server.layerSurfaces) {
                if (surface->wlr->output != wlr || surface->layer != layer || !surface->wlr->initialized) {
                    continue;
                }
                if ((surface->wlr->current.exclusive_zone > 0) != exclusive) {
                    continue;
                }
                surface->configure(full, area);
            }
        }
    }

    const bool changed = area.x != usable.x || area.y != usable.y
        || area.width != usable.width || area.height != usable.height;
    usable = area;
    if (changed) {
        for (Toplevel* toplevel : server.toplevels) {
            if (toplevel->fullscreen || toplevel->output() != this) {
                continue;
            }
            if (toplevel->maximized) {
                toplevel->applyMaximized();
            } else if (toplevel->snap != Snap::None) {
                toplevel->applySnap(this);
            }
        }
    }
}

} // namespace vela
