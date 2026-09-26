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

    wlr_log(WLR_INFO, "Schermo %s: %dx%d @ %.2f Hz", wlr->name, wlr->width, wlr->height,
        wlr->refresh / 1000.0);

    if (server.vulkan) {
        renderer = render::OutputRenderer::create(*server.vulkan, server.velaAllocator, wlr);
        if (!renderer) {
            wlr_log(WLR_ERROR, "%s: renderer di Vela non disponibile, uso quello di wlroots", wlr->name);
        }
    }
    clock.setModeRefresh(wlr->refresh);
    if (renderer && wlr_output_is_headless(wlr)) {
        startVirtualVblank();
    } else {
        present.connect(&wlr->events.present, [this](void* data) {
            auto* event = static_cast<wlr_output_event_present*>(data);
            if (event->presented) {
                clock.presented(event->commit_seq, render::toNs(event->when), event->refresh);
            }
        });
        frame.connect(&wlr->events.frame, [this](void*) { renderer ? onFrameVela() : onFrame(); });
    }
    requestState.connect(&wlr->events.request_state, [this](void* data) {
        // Nel backend annidato: la finestra ospite è stata ridimensionata.
        auto* event = static_cast<wlr_output_event_request_state*>(data);
        wlr_output_commit_state(wlr, event->state);
        clock.setModeRefresh(wlr->refresh);
        arrangeLayers();
    });
    destroy.connect(&wlr->events.destroy, [this](void*) { delete this; });

    wlr_output_layout_output* layoutOutput = wlr_output_layout_add_auto(server.outputLayout, wlr);
    sceneOutput = wlr_scene_output_create(server.scene, wlr);
    wlr_scene_output_layout_add_output(server.sceneLayout, layoutOutput, sceneOutput);

    usable = box();
    server.outputs.push_back(this);
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
    renderer.reset(); // prima che l'allocatore e il device spariscano
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
    m_nextVblankNs = render::nowNs() + clock.periodNs();
    const itimerspec when { .it_interval = {}, .it_value = { time_t(m_nextVblankNs / 1'000'000'000), long(m_nextVblankNs % 1'000'000'000) } };
    timerfd_settime(m_vblankFd, TFD_TIMER_ABSTIME, &when, nullptr);
    wlr_log(WLR_INFO, "%s: vblank virtuale ogni %.3f ms", wlr->name, clock.periodNs() / 1e6);
}

void Output::onVirtualVblank()
{
    uint64_t expirations = 0;
    if (read(m_vblankFd, &expirations, sizeof(expirations)) < 0) {
        return;
    }
    const int64_t period = clock.periodNs();
    const int64_t vblank = m_nextVblankNs;

    // Il frame consegnato prima di questo vblank diventa luce adesso.
    if (m_awaitingPresent) {
        clock.presented(m_awaitingSeq, vblank, period);
        m_awaitingPresent = false;
    }

    // Se siamo in ritardo di uno o più periodi, quei vblank sono persi.
    int64_t next = vblank + period;
    const int64_t now = render::nowNs();
    if (now >= next) {
        const int64_t lost = (now - vblank) / period;
        clock.missed(int(lost));
        next = vblank + (lost + 1) * period;
    }
    m_nextVblankNs = next;
    const itimerspec when { .it_interval = {}, .it_value = { time_t(next / 1'000'000'000), long(next % 1'000'000'000) } };
    timerfd_settime(m_vblankFd, TFD_TIMER_ABSTIME, &when, nullptr);

    onFrameVela();
}

wlr_box Output::box() const
{
    wlr_box result {};
    wlr_output_layout_get_box(server.outputLayout, wlr, &result);
    return result;
}

void Output::onFrame()
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);

    // Prima si fa avanzare ogni animazione al tempo di questo frame, poi si
    // disegna. Le animazioni sono legate ai frame reali dello schermo, non a
    // un timer: a 180 Hz ottieni 180 passi al secondo.
    server.tickAnimations(now);

    wlr_scene_output_commit(sceneOutput, nullptr);
    wlr_scene_output_send_frame_done(sceneOutput, &now);
}

// Tappa S0 del renderer di Vela: la scena di prova, disegnata per l'istante
// in cui il frame verrà mostrato. Le finestre non si vedono ancora.
void Output::onFrameVela()
{
    const int64_t now = render::nowNs();
    const int64_t presentAt = clock.predict(now);

    wlr_output_state state;
    wlr_output_state_init(&state);
    if (renderer->render(&state, presentAt) && wlr_output_commit_state(wlr, &state)) {
        clock.committed(wlr->commit_seq, presentAt);
        m_awaitingPresent = true;
        m_awaitingSeq = wlr->commit_seq;
    }
    wlr_output_state_finish(&state);

    // Le app devono continuare a ricevere i frame callback.
    timespec nowTs {};
    clock_gettime(CLOCK_MONOTONIC, &nowTs);
    wlr_scene_output_send_frame_done(sceneOutput, &nowTs);

    // La scena di prova si muove sempre: un frame dopo l'altro (col vblank
    // virtuale è il suo timer a scandirli).
    if (!m_vblankSource) {
        wlr_output_schedule_frame(wlr);
    }

    double fps = 0.0;
    double errorMean = 0.0;
    double errorMax = 0.0;
    int missed = 0;
    if (clock.takeStats(now, fps, errorMean, errorMax, missed)) {
        wlr_log(WLR_INFO, "%s: %.2f fps (periodo %.3f ms, latenza %d vblank), errore di previsione "
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
    wlr_scene_tree* order[] = {
        server.layers.overlay,
        server.layers.top,
        server.layers.bottom,
        server.layers.background,
    };
    for (bool exclusive : { true, false }) {
        for (wlr_scene_tree* tree : order) {
            for (LayerSurface* layer : server.layerSurfaces) {
                if (layer->wlr->output != wlr || layer->sceneLayer->tree->node.parent != tree) {
                    continue;
                }
                if (!layer->wlr->initialized) {
                    continue;
                }
                if ((layer->wlr->current.exclusive_zone > 0) != exclusive) {
                    continue;
                }
                wlr_scene_layer_surface_v1_configure(layer->sceneLayer, &full, &area);
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
