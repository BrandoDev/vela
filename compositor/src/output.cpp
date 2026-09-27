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

// La scala scelta a mano: VELA_SCALE=1.25 per tutti gli schermi, oppure
// per nome: VELA_SCALE=DP-1=1.5,HDMI-A-1=1. 0 se non c'è.
float requestedScale(const char* name)
{
    const char* value = std::getenv("VELA_SCALE");
    if (!value || !*value) {
        return 0.0f;
    }
    if (!std::strchr(value, '=')) {
        return std::strtof(value, nullptr);
    }
    const std::string list = value;
    size_t start = 0;
    while (start < list.size()) {
        const size_t end = std::min(list.find(',', start), list.size());
        const std::string item = list.substr(start, end - start);
        const size_t eq = item.find('=');
        if (eq != std::string::npos && item.substr(0, eq) == name) {
            return std::strtof(item.c_str() + eq + 1, nullptr);
        }
        start = end + 1;
    }
    return 0.0f;
}

// La scala predefinita, come fa Windows (docs/renderer.md §3.8): dai DPI
// dello schermo, a passi del 25%, tra 100% e 300%. I pannelli dei portatili
// si guardano più da vicino: riferimento 105,6 DPI invece di 96.
float defaultScale(const wlr_output* output, int width, int height, double& dpi)
{
    dpi = 0.0;
    const double physWidth = output->phys_width; // mm, dall'EDID
    const double physHeight = output->phys_height;
    if (physWidth <= 0.0 || physHeight <= 0.0 || width <= 0 || height <= 0) {
        return 1.0f;
    }
    // Misure assurde (proiettori, TV, adattatori che inventano l'EDID):
    // diagonale fuori da 8"–100", o proporzioni diverse da quelle dei pixel.
    const double diagonalMm = std::hypot(physWidth, physHeight);
    const double aspectError = std::abs((physWidth / physHeight) / (double(width) / height) - 1.0);
    if (diagonalMm < 8 * 25.4 || diagonalMm > 100 * 25.4 || aspectError > 0.1) {
        return 1.0f;
    }
    dpi = std::hypot(width, height) / (diagonalMm / 25.4);
    const std::string name = output->name;
    const bool internal = name.starts_with("eDP") || name.starts_with("LVDS") || name.starts_with("DSI");
    const double reference = internal ? 105.6 : 96.0;
    return float(std::clamp(std::round(dpi / reference * 4.0) / 4.0, 1.0, 3.0));
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
    if (const float scale = requestedScale(wlr->name); scale > 0.0f) {
        wlr_output_state_set_scale(&state, scale);
    } else if (state.committed & WLR_OUTPUT_STATE_MODE) {
        const int width = state.mode_type == WLR_OUTPUT_STATE_MODE_FIXED ? state.mode->width : state.custom_mode.width;
        const int height = state.mode_type == WLR_OUTPUT_STATE_MODE_FIXED ? state.mode->height : state.custom_mode.height;
        double dpi = 0.0;
        const float scale = defaultScale(wlr, width, height, dpi);
        wlr_output_state_set_scale(&state, scale);
        if (dpi > 0.0) {
            wlr_log(WLR_INFO, "%s: %.0f DPI (%d×%d mm), scala predefinita %.0f%%", wlr->name, dpi, wlr->phys_width,
                wlr->phys_height, scale * 100.0);
        }
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
    // Late latching (§4.3): VELA_LATCH=0 lo spegne (si disegna appena il
    // backend dice "frame"); VELA_LATCH_MARGIN: margine minimo in ms.
    if (const char* latch = std::getenv("VELA_LATCH"); latch && std::strcmp(latch, "0") == 0) {
        m_latching = false;
    }
    if (const char* margin = std::getenv("VELA_LATCH_MARGIN"); margin && *margin) {
        clock.setBaseMargin(int64_t(std::strtod(margin, nullptr) * 1e6));
    }
    m_latchFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (m_latchFd >= 0) {
        m_latchSource = wl_event_loop_add_fd(server.loop, m_latchFd, WL_EVENT_READABLE,
            [](int fd, uint32_t, void* data) {
                uint64_t expirations = 0;
                if (read(fd, &expirations, sizeof(expirations)) < 0) {
                    return 0;
                }
                auto* self = static_cast<Output*>(data);
                self->m_latchArmed = false;
                self->onFrame();
                return 0;
            },
            this);
    } else {
        m_latching = false;
    }
    if (wlr_output_is_headless(wlr)) {
        startVirtualVblank();
    } else {
        present.connect(&wlr->events.present, [this](void* data) {
            auto* event = static_cast<wlr_output_event_present*>(data);
            if (event->presented) {
                clock.presented(event->commit_seq, render::toNs(event->when), event->refresh);
            }
            collectCosts();
        });
        frame.connect(&wlr->events.frame, [this](void*) { onFrameEvent(); });
    }
    requestState.connect(&wlr->events.request_state, [this](void* data) {
        // Nel backend annidato: la finestra ospite è stata ridimensionata.
        auto* event = static_cast<wlr_output_event_request_state*>(data);
        const wlr_output_state* requested = event->state;
        if (nested && (requested->committed & WLR_OUTPUT_STATE_MODE)
            && requested->mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM) {
            nested->resize(requested->custom_mode.width, requested->custom_mode.height);
            return;
        }
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_copy(&state, requested);
        commitMode(state);
        wlr_output_state_finish(&state);
    });
    destroy.connect(&wlr->events.destroy, [this](void*) { delete this; });

    wlr_output_layout_add_auto(server.outputLayout, wlr);
    if (wlr_output_is_wl(wlr)) {
        nested = std::make_unique<NestedWindow>(*this);
    }

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
    if (m_idleFrame) {
        wl_event_source_remove(m_idleFrame);
    }
    if (m_latchSource) {
        wl_event_source_remove(m_latchSource);
    }
    if (m_latchFd >= 0) {
        close(m_latchFd);
    }
    nested.reset();
    sceneFrame.reset();
}

void Output::commitMode(wlr_output_state& state)
{
    const wlr_box area = box();
    if (!sceneFrame->render(area.x, area.y, &state)) {
        // Ripiego: wlroots mette un buffer vuoto, che il nostro registro
        // dei danni non conosce.
        wlr_output_commit_state(wlr, &state);
        sceneFrame->resetDamage();
    }
    clock.setModeRefresh(wlr->refresh);
    arrangeLayers();
    scheduleFrame();
}

void Output::scheduleFrame()
{
    m_frameRequested = true;
    // Come fa wlroots con DRM: se nessun frame consegnato aspetta ancora il
    // vblank, il "frame" arriva subito; altrimenti con lo scambio di pagina.
    // Non si usa wlr_output_schedule_frame: segnerebbe lo schermo come
    // bisognoso di un commit anche quando su di lui non cambia nulla (un
    // commit vuoto, che con DRM blocca fino al vblank).
    const bool waiting = m_vblankFd >= 0 ? m_awaitingPresent : wlr->frame_pending;
    if (!waiting && !m_latchArmed && !m_idleFrame) {
        m_idleFrame = wl_event_loop_add_idle(
            server.loop,
            [](void* data) {
                auto* self = static_cast<Output*>(data);
                self->m_idleFrame = nullptr;
                self->onFrameEvent();
            },
            this);
    }
}

// ------------------------------------------------------ il ciclo dei frame --

void Output::onFrameEvent()
{
    if (m_latchArmed || (!m_frameRequested && !wlr->needs_frame)) {
        return; // già pianificato, o niente da fare
    }
    collectCosts();
    const int64_t now = render::nowNs();
    m_plan = clock.plan(now, m_latching);
    m_planned = true;
    if (m_plan.start - now > 50'000) {
        armLatch(m_plan.start);
        return;
    }
    onFrame();
}

void Output::armLatch(int64_t when)
{
    const itimerspec spec { .it_interval = {}, .it_value = { time_t(when / 1'000'000'000), long(when % 1'000'000'000) } };
    timerfd_settime(m_latchFd, TFD_TIMER_ABSTIME, &spec, nullptr);
    m_latchArmed = true;
}

void Output::onFrame()
{
    // Uno scambio di pagina ancora in corso (es. dopo un cambio di modo):
    // il suo evento "frame" ci richiamerà.
    if (m_vblankFd < 0 && wlr->frame_pending) {
        m_planned = false;
        return;
    }
    const int64_t now = render::nowNs();
    const render::FrameClock::Plan plan = m_planned ? m_plan : clock.plan(now, false);
    m_planned = false;
    m_frameRequested = false;

    // Prima si fa avanzare ogni animazione all'istante in cui questo frame
    // diventerà luce (docs/renderer.md §4.2), poi si disegna.
    server.tickAnimations(plan.present);

    const wlr_box area = box();
    if (sceneFrame->render(area.x, area.y)) {
        const scene::OutputFrame::Delivered& delivered = sceneFrame->delivered();
        const Delivery delivery { wlr->commit_seq, plan.start, now, render::nowNs(), delivered.point,
            delivered.timingSlot };
        clock.committed(wlr->commit_seq, plan.start, plan.present);
        m_deliveries.push_back(delivery);
        if (m_deliveries.size() > 16) {
            m_deliveries.erase(m_deliveries.begin());
        }
        // Col vblank virtuale il frame compare al primo battito in cui è pronto.
        if (m_vblankFd >= 0) {
            m_awaiting = delivery;
            m_awaitingPresent = true;
            armVirtualVblank();
        }
    }

    timespec nowTs {};
    clock_gettime(CLOCK_MONOTONIC, &nowTs);
    sceneFrame->sendFrameDone(nowTs);

    // VELA_STATS=1: le statistiche anche senza il log dettagliato.
    static const bool statsRequested = envFlag("VELA_STATS");
    render::FrameClock::Stats stats {};
    if (clock.takeStats(now, stats) && stats.fps > 0.0) {
        wlr_log(statsRequested ? WLR_INFO : WLR_DEBUG,
            "%s: %.2f fps (periodo %.3f ms, latenza %d vblank); costo %.3f ms + margine %.3f ms; "
            "dal disegno alla luce %.3f ms; errore di previsione medio %.3f ms, massimo %.3f ms; vblank persi %d",
            wlr->name, stats.fps, clock.periodNs() / 1e6, clock.latencyFrames(), stats.costMs, stats.marginMs,
            stats.latencyMs, stats.errorMeanMs, stats.errorMaxMs, stats.missed);
        if (statsRequested && m_breakdown.count > 0) {
            const Breakdown& b = m_breakdown;
            const double n = b.count;
            wlr_log(WLR_INFO,
                "%s: in media (massimo) risveglio in ritardo %.3f (%.3f) ms, CPU %.3f (%.3f) ms, "
                "attesa della GPU %.3f (%.3f) ms, lavoro della GPU %.3f (%.3f) ms",
                wlr->name, b.sum[0] / n, b.max[0], b.sum[1] / n, b.max[1], b.sum[2] / n, b.max[2], b.sum[3] / n,
                b.max[3]);
        }
        m_breakdown = {};
    }
}

bool Output::readyTime(const Delivery& delivery, int64_t& when) const
{
    if (delivery.point == 0) {
        when = delivery.committedAt;
        return true;
    }
    render::Renderer& renderer = *server.velaRenderer;
    render::Renderer::GpuTiming timing;
    if (renderer.readTiming(delivery.timingSlot, delivery.point, timing)) {
        // Senza timestamp calibrati si sa solo quanto ha lavorato la GPU:
        // si suppone che abbia cominciato al commit.
        when = timing.absolute ? std::max(delivery.committedAt, timing.endNs) : delivery.committedAt + timing.endNs;
        return true;
    }
    if (delivery.point <= renderer.completed()) {
        when = delivery.committedAt; // finito, ma senza misura
        return true;
    }
    return false;
}

void Output::collectCosts()
{
    const int64_t now = render::nowNs();
    std::erase_if(m_deliveries, [&](const Delivery& delivery) {
        int64_t ready = 0;
        if (readyTime(delivery, ready)) {
            clock.addCost(now, ready - delivery.start);
            render::Renderer::GpuTiming timing;
            m_breakdown.add(0, double(delivery.wokeAt - delivery.start) / 1e6);
            m_breakdown.add(1, double(delivery.committedAt - delivery.wokeAt) / 1e6);
            if (delivery.point && server.velaRenderer->readTiming(delivery.timingSlot, delivery.point, timing)
                && timing.absolute) {
                m_breakdown.add(2, double(timing.startNs - delivery.committedAt) / 1e6);
                m_breakdown.add(3, double(timing.endNs - timing.startNs) / 1e6);
            }
            ++m_breakdown.count;
            return true;
        }
        return now - delivery.committedAt > 1'000'000'000; // misura persa
    });
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
    // L'ultimo vblank della griglia passato (il timer può svegliarci tardi).
    const int64_t anchor = m_lastVblankNs;
    const int64_t vblank = anchor + std::max<int64_t>(1, (now - anchor) / period) * period;
    m_lastVblankNs = vblank;

    if (m_awaitingPresent) {
        // Il frame compare al primo vblank in cui era pronto: commit fatto e
        // GPU finita. Se non lo è ancora, resta sullo schermo il precedente
        // e si riprova al prossimo battito (vblank perso).
        int64_t ready = 0;
        if (!readyTime(m_awaiting, ready)) {
            armVirtualVblank();
            return;
        }
        const int64_t late = std::max<int64_t>(0, ready - anchor);
        const int64_t shownAt = anchor + std::max<int64_t>(1, (late + period - 1) / period) * period;
        if (shownAt > vblank) {
            armVirtualVblank();
            return;
        }
        clock.presented(m_awaiting.seq, shownAt, period);
        m_awaitingPresent = false;
        collectCosts();
    }
    // Come l'evento "frame" di DRM al vblank dopo una consegna.
    onFrameEvent();
}

wlr_box Output::box() const
{
    wlr_box result {};
    wlr_output_layout_get_box(server.outputLayout, wlr, &result);
    return result;
}

// ------------------------------------------------ aree in pixel fisici --

Area Output::fromPhysical(const wlr_box& physical) const
{
    const wlr_box full = box();
    const double scale = wlr->scale;
    return { full.x + physical.x / scale, full.y + physical.y / scale, physical.width / scale,
        physical.height / scale };
}

Area Output::fullArea() const
{
    int width = 0;
    int height = 0;
    wlr_output_transformed_resolution(wlr, &width, &height);
    return fromPhysical({ 0, 0, width, height });
}

// L'area libera dai pannelli in pixel dello schermo: i bordi che toccano
// quelli dello schermo restano esattamente sui suoi pixel.
wlr_box Output::physicalUsable() const
{
    const wlr_box full = box();
    int width = 0;
    int height = 0;
    wlr_output_transformed_resolution(wlr, &width, &height);
    const double scale = wlr->scale;
    auto edge = [scale](int logical, int fullStart, int fullEnd, int pixels) {
        if (logical <= fullStart) {
            return 0;
        }
        if (logical >= fullEnd) {
            return pixels;
        }
        return int(std::lround((logical - fullStart) * scale));
    };
    const int x1 = edge(usable.x, full.x, full.x + full.width, width);
    const int x2 = edge(usable.x + usable.width, full.x, full.x + full.width, width);
    const int y1 = edge(usable.y, full.y, full.y + full.height, height);
    const int y2 = edge(usable.y + usable.height, full.y, full.y + full.height, height);
    return { x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1) };
}

Area Output::usableArea() const
{
    return fromPhysical(physicalUsable());
}

// Il client disegna un buffer di round(dimensione × scala) pixel
// (fractional-scale-v1, scala in 120esimi): per ogni asse si cerca la
// dimensione logica che dà esattamente i pixel dell'area.
Placement Output::place(const Area& area) const
{
    const wlr_box full = box();
    int screenWidth = 0;
    int screenHeight = 0;
    wlr_output_transformed_resolution(wlr, &screenWidth, &screenHeight);
    const double scale = wlr->scale;
    const int64_t scale120 = std::lround(scale * 120.0);
    auto buffer = [scale120](int64_t logical) { return (logical * scale120 + 60) / 120; };

    // Un bordo dello schermo senza un altro schermo accanto: ciò che sborda
    // lì non si vede.
    auto open = [&](double lx, double ly) {
        return !wlr_output_layout_output_at(server.outputLayout, lx, ly);
    };
    const double midX = full.x + full.width / 2.0;
    const double midY = full.y + full.height / 2.0;

    struct Axis {
        double position;
        int size;
    };
    auto axis = [&](double origin, int start, int size, int screen, bool openBefore, bool openAfter) -> Axis {
        const bool touchesStart = start <= 0 && openBefore;
        const bool touchesEnd = start + size >= screen && openAfter;
        int exact = 0;
        int above = 0; // il più piccolo che sfora
        int below = 1; // il più grande che resta dentro
        for (int64_t w = int64_t(std::floor(size / scale)) - 1; w <= int64_t(std::ceil(size / scale)) + 1; ++w) {
            if (w <= 0) {
                continue;
            }
            const int64_t pixels = buffer(w);
            if (pixels == size) {
                exact = int(w);
            } else if (pixels > size && !above) {
                above = int(w);
            } else if (pixels < size) {
                below = int(w);
            }
        }
        if (exact) {
            return { origin + start / scale, exact };
        }
        if ((touchesStart || touchesEnd) && above) {
            // Si sfora dal lato dello schermo: oltre la fine, o prima
            // dell'inizio se è lì il bordo libero.
            const int64_t excess = buffer(above) - size;
            const double first = touchesEnd ? start : double(start - excess);
            return { origin + first / scale, above };
        }
        return { origin + start / scale, below };
    };

    const int px = int(std::lround((area.x - full.x) * scale));
    const int py = int(std::lround((area.y - full.y) * scale));
    const int pw = int(std::lround(area.width * scale));
    const int ph = int(std::lround(area.height * scale));
    const Axis x = axis(full.x, px, pw, screenWidth, open(full.x - 0.5, midY), open(full.x + full.width + 0.5, midY));
    const Axis y = axis(full.y, py, ph, screenHeight, open(midX, full.y - 0.5), open(midX, full.y + full.height + 0.5));
    return { x.position, y.position, x.size, y.size };
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
