// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "output.h"

#include "config.h"
#include "geometry.h"
#include "layer.h"
#include "nested.h"
#include "output_config.h"
#include "output_manager.h"
#include "render/renderer.h"
#include "scene/frame.h"
#include "scene/scene.h"
#include "server.h"
#include "util.h"
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/wayland.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

// --------------------------------------------------------------- creazione --

// Molti monitor ad alta frequenza dichiarano come "preferita" una modalità a
// 60 Hz. Noi prendiamo la risoluzione preferita alla frequenza più alta
// disponibile: sul tuo monitor, 180 Hz senza toccare nulla.
static struct wlr_output_mode *pick_mode(struct wlr_output *output)
{
    struct wlr_output_mode *preferred = wlr_output_preferred_mode(output);
    if (!preferred) {
        return NULL; // es. backend annidato: nessuna lista di modalità
    }
    struct wlr_output_mode *best = preferred;
    struct wlr_output_mode *mode;
    wl_list_for_each (mode, &output->modes, link) {
        if (mode->width == preferred->width && mode->height == preferred->height && mode->refresh > best->refresh) {
            best = mode;
        }
    }
    return best;
}

// La modalità dello schermo più vicina a quella salvata, NULL se non c'è.
static struct wlr_output_mode *find_mode(struct wlr_output *output, int width, int height, int refresh_mhz)
{
    struct wlr_output_mode *best = NULL;
    struct wlr_output_mode *mode;
    wl_list_for_each (mode, &output->modes, link) {
        if (mode->width != width || mode->height != height) {
            continue;
        }
        if (!best || abs(mode->refresh - refresh_mhz) < abs(best->refresh - refresh_mhz)) {
            best = mode;
        }
    }
    return best;
}

void vela_output_key(const struct wlr_output *output, char *out, size_t size)
{
    out[0] = '\0';
    size_t used = 0;
    const char *parts[] = { output->make, output->model, output->serial };
    for (int i = 0; i < 3; ++i) {
        if (parts[i] && *parts[i] && used < size) {
            used += (size_t)snprintf(out + used, size - used, "%s%s", used ? " " : "", parts[i]);
        }
    }
    if (!out[0]) {
        snprintf(out, size, "%s", output->name);
    }
}

// Lo stato iniziale: ciò che l'utente ha scelto l'ultima volta per questo
// monitor (solo nella sessione vera), altrimenti la modalità migliore e la
// scala dai DPI. `saved` è NULL se non c'è niente di salvato.
static void initial_state(struct wlr_output *wlr, const struct vela_saved_output *saved,
    struct wlr_output_state *state)
{
    bool enabled = !saved || saved->enabled;
    wlr_output_state_set_enabled(state, enabled);
    struct wlr_output_mode *saved_mode
        = saved && saved->width > 0 ? find_mode(wlr, saved->width, saved->height, saved->refresh_mhz) : NULL;
    struct wlr_output_mode *best = NULL;
    const char *size = getenv("VELA_OUTPUT_SIZE");
    if (!enabled) {
        // Spento, come l'ha lasciato l'utente.
    } else if (saved_mode) {
        wlr_output_state_set_mode(state, saved_mode);
    } else if ((best = pick_mode(wlr))) {
        wlr_output_state_set_mode(state, best);
    } else if (size) {
        // Schermo senza modalità (finestra annidata, headless): la dimensione
        // la scegliamo noi, es. VELA_OUTPUT_SIZE=1920x1080, con la frequenza
        // facoltativa per le prove: 1920x1080@144.
        int width = 0;
        int height = 0;
        double hz = 0.0;
        int fields = sscanf(size, "%dx%d@%lf", &width, &height, &hz);
        if (fields >= 2 && width > 0 && height > 0) {
            wlr_output_state_set_custom_mode(state, width, height, fields == 3 ? (int32_t)(hz * 1000.0) : 0);
        }
    }
    if (saved && enabled) {
        wlr_output_state_set_transform(state, (enum wl_output_transform)saved->transform);
    }
    float requested = vela_requested_scale(getenv("VELA_SCALE"), wlr->name);
    if (requested > 0.0f) {
        wlr_output_state_set_scale(state, requested);
    } else if (saved && saved->scale > 0.0f) {
        wlr_output_state_set_scale(state, saved->scale);
    } else if (state->committed & WLR_OUTPUT_STATE_MODE) {
        bool fixed = state->mode_type == WLR_OUTPUT_STATE_MODE_FIXED;
        int width = fixed ? state->mode->width : state->custom_mode.width;
        int height = fixed ? state->mode->height : state->custom_mode.height;
        double dpi = 0.0;
        float scale = vela_scale_for_dpi(wlr->phys_width, wlr->phys_height, width, height,
            vela_is_internal_panel(wlr->name), &dpi);
        wlr_output_state_set_scale(state, scale);
        if (dpi > 0.0) {
            wlr_log(WLR_INFO, "%s: %.0f DPI (%d×%d mm), default scale %.0f%%", wlr->name, dpi, wlr->phys_width,
                wlr->phys_height, scale * 100.0);
        }
    }
}

static int handle_latch_timer(int fd, uint32_t mask, void *data);
void vela_server_schedule_frames(struct vela_server *server)
{
    struct vela_output *output;
    wl_list_for_each (output, &server->outputs, link) {
        vela_output_schedule_frame(output);
    }
}

void vela_output_load_settings(struct vela_server *server)
{
    struct vela_config settings;
    vela_config_read(&settings);
    const char *vrr = vela_config_get(&settings, "variable-refresh", "games");
    server->vrr_mode = strcmp(vrr, "always") == 0 ? 2 : strcmp(vrr, "no") == 0 ? 0 : 1;
    const char *env = getenv("VELA_VRR");
    if (env && *env) {
        server->vrr_mode = strcmp(env, "0") == 0 ? 0 : strcmp(env, "1") == 0 ? 2 : server->vrr_mode;
    }
    vela_server_schedule_frames(server);
    server->scene->allow_tearing = vela_config_flag(&settings, "tearing", true) && !vela_env_off("VELA_TEARING");
    vela_config_finish(&settings);
}

static void handle_frame_event(struct wl_listener *listener, void *data);
static void handle_present(struct wl_listener *listener, void *data);
static void handle_request_state(struct wl_listener *listener, void *data);
static void handle_destroy(struct wl_listener *listener, void *data);
static void start_virtual_vblank(struct vela_output *output);

void vela_output_create(struct vela_server *server, struct wlr_output *wlr)
{
    struct vela_output *output = calloc(1, sizeof(*output));
    output->server = server;
    output->wlr = wlr;
    output->powered = true;
    output->latch_fd = -1;
    output->vblank_fd = -1;
    wl_list_init(&output->link);
    wlr->data = output;
    // Tutto ciò che si disegna su questo schermo passa dal renderer di Vela,
    // anche ciò che fa wlroots (cursore, catture).
    wlr_output_init_render(wlr, server->allocator, server->wlr_renderer);

    char key[256];
    vela_output_key(wlr, key, sizeof(key));
    struct vela_saved_output saved_output;
    const struct vela_saved_output *saved
        = server->session && vela_output_config_load(key, &saved_output) ? &saved_output : NULL;
    bool enabled = !saved || saved->enabled;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    initial_state(wlr, saved, &state);
    // Il VRR lo accende e spegne il ciclo dei frame (vrr_state).
    wlr_output_commit_state(wlr, &state);
    wlr_output_state_finish(&state);
    if (enabled) {
        wlr_log(WLR_INFO, "Output %s: %dx%d @ %.2f Hz, scale %.2f%s", wlr->name, wlr->width, wlr->height,
            wlr->refresh / 1000.0, wlr->scale, saved ? " (as saved)" : "");
    } else {
        wlr_log(WLR_INFO, "Output %s: off, as saved", wlr->name);
    }

    output->frame = vela_output_frame_create(server->scene, server->renderer, wlr, output);
    vela_frame_clock_init(&output->clock);
    vela_frame_clock_set_mode_refresh(&output->clock, wlr->refresh);
    // Late latching (§4.3): VELA_LATCH=0 lo spegne (si disegna appena il
    // backend dice "frame"); VELA_LATCH_MARGIN: margine minimo in ms.
    output->latching = !vela_env_off("VELA_LATCH");
    const char *margin = getenv("VELA_LATCH_MARGIN");
    if (margin && *margin) {
        vela_frame_clock_set_base_margin(&output->clock, (int64_t)(strtod(margin, NULL) * 1e6));
    }
    output->latch_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (output->latch_fd >= 0) {
        output->latch_source = wl_event_loop_add_fd(server->loop, output->latch_fd, WL_EVENT_READABLE,
            handle_latch_timer, output);
    } else {
        output->latching = false;
    }
    wl_list_init(&output->present.link);
    wl_list_init(&output->frame_event.link);
    if (wlr_output_is_headless(wlr)) {
        start_virtual_vblank(output);
    } else {
        output->present.notify = handle_present;
        wl_signal_add(&wlr->events.present, &output->present);
        output->frame_event.notify = handle_frame_event;
        wl_signal_add(&wlr->events.frame, &output->frame_event);
    }
    output->request_state.notify = handle_request_state;
    wl_signal_add(&wlr->events.request_state, &output->request_state);
    output->destroy.notify = handle_destroy;
    wl_signal_add(&wlr->events.destroy, &output->destroy);

    // Nella lista prima di entrare nel layout: il cambio del layout avvisa i
    // programmi di configurazione degli schermi, e questo deve già esserci.
    wl_list_insert(server->outputs.prev, &output->link);
    if (!enabled) {
        // Fuori dal layout finché qualcuno non lo riaccende.
        vela_output_manager_update(server);
    } else if (saved && saved->has_position) {
        wlr_output_layout_add(server->output_layout, wlr, saved->x, saved->y);
    } else {
        wlr_output_layout_add_auto(server->output_layout, wlr);
    }
    if (wlr_output_is_wl(wlr)) {
        output->nested = vela_nested_create(output);
    }
    output->usable = vela_output_box(output);
    vela_output_schedule_frame(output);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct vela_output *output = wl_container_of(listener, output, destroy);
    struct vela_server *server = output->server;
    // Pannelli chiusi, anteprima dello snap e lente spente, il blocco non
    // aspetta più il nero su questo schermo.
    vela_server_output_destroyed(server, output);
    wl_list_remove(&output->link);
    vela_output_manager_update(server);
    output->wlr->data = NULL;
    wl_list_remove(&output->frame_event.link);
    wl_list_remove(&output->present.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    if (output->vblank_source) {
        wl_event_source_remove(output->vblank_source);
    }
    if (output->vblank_fd >= 0) {
        close(output->vblank_fd);
    }
    if (output->idle_frame) {
        wl_event_source_remove(output->idle_frame);
    }
    if (output->latch_source) {
        wl_event_source_remove(output->latch_source);
    }
    if (output->latch_fd >= 0) {
        close(output->latch_fd);
    }
    vela_nested_destroy(output->nested);
    vela_output_frame_destroy(output->frame);
    free(output);
}

// Nel backend annidato: la finestra ospite è stata ridimensionata.
static void handle_request_state(struct wl_listener *listener, void *data)
{
    struct vela_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    const struct wlr_output_state *requested = event->state;
    if (output->nested && (requested->committed & WLR_OUTPUT_STATE_MODE)
        && requested->mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM) {
        vela_nested_resize(output->nested, requested->custom_mode.width, requested->custom_mode.height);
        return;
    }
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_copy(&state, requested);
    vela_output_commit_mode(output, &state);
    wlr_output_state_finish(&state);
}

bool vela_output_commit_mode(struct vela_output *output, struct wlr_output_state *state)
{
    struct wlr_box area = vela_output_box(output);
    bool ok = vela_output_frame_render(output->frame, area.x, area.y, state);
    if (!ok) {
        // Ripiego (anche per accendere o spegnere lo schermo): wlroots mette
        // un buffer vuoto, che il nostro registro dei danni non conosce.
        ok = wlr_output_commit_state(output->wlr, state);
        vela_output_frame_reset_damage(output->frame);
    }
    vela_frame_clock_set_mode_refresh(&output->clock, output->wlr->refresh);
    vela_output_arrange_layers(output);
    vela_output_schedule_frame(output);
    return ok;
}

void vela_output_set_powered(struct vela_output *output, bool on)
{
    if (on == output->powered) {
        return;
    }
    struct wlr_output *wlr = output->wlr;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!on) {
        output->mode_before_off = wlr->current_mode;
        output->custom_before_off[0] = wlr->width;
        output->custom_before_off[1] = wlr->height;
        output->custom_before_off[2] = wlr->refresh;
        wlr_output_state_set_enabled(&state, false);
        wlr_output_commit_state(wlr, &state);
    } else {
        wlr_output_state_set_enabled(&state, true);
        if (output->mode_before_off) {
            wlr_output_state_set_mode(&state, output->mode_before_off);
        } else if (output->custom_before_off[0] > 0) {
            wlr_output_state_set_custom_mode(&state, output->custom_before_off[0], output->custom_before_off[1],
                output->custom_before_off[2]);
        }
        vela_output_commit_mode(output, &state);
    }
    wlr_output_state_finish(&state);
    output->powered = on;
    wlr_log(WLR_INFO, "%s: screen %s", wlr->name, on ? "back on" : "off because idle");
}

// ------------------------------------------------------ il ciclo dei frame --
//
// Il backend dice "frame" (al vblank dopo una consegna, o subito se lo
// schermo era fermo), si pianifica il disegno il più tardi possibile prima
// del vblank, e al momento giusto si disegna (docs/renderer.md §4.3).

static void on_frame_event(struct vela_output *output);
static void on_frame(struct vela_output *output);
static void arm_virtual_vblank(struct vela_output *output);
static void collect_costs(struct vela_output *output);

static void handle_idle_frame(void *data)
{
    struct vela_output *output = data;
    output->idle_frame = NULL;
    on_frame_event(output);
}

void vela_output_schedule_frame(struct vela_output *output)
{
    output->frame_requested = true;
    // Come fa wlroots con DRM: se nessun frame consegnato aspetta ancora il
    // vblank, il "frame" arriva subito; altrimenti con lo scambio di pagina.
    // Non si usa wlr_output_schedule_frame: segnerebbe lo schermo come
    // bisognoso di un commit anche quando su di lui non cambia nulla (un
    // commit vuoto, che con DRM blocca fino al vblank).
    bool waiting = output->vblank_fd >= 0 ? output->awaiting_present : output->wlr->frame_pending;
    if (!waiting && !output->latch_armed && !output->idle_frame) {
        output->idle_frame = wl_event_loop_add_idle(output->server->loop, handle_idle_frame, output);
    }
}

static void handle_frame_event(struct wl_listener *listener, void *data)
{
    struct vela_output *output = wl_container_of(listener, output, frame_event);
    on_frame_event(output);
}

static void handle_present(struct wl_listener *listener, void *data)
{
    struct vela_output *output = wl_container_of(listener, output, present);
    const struct wlr_output_event_present *event = data;
    if (event->presented) {
        vela_frame_clock_presented(&output->clock, event->commit_seq, vela_timespec_ns(&event->when), event->refresh);
    }
    collect_costs(output);
}

static void arm_latch(struct vela_output *output, int64_t when)
{
    const struct itimerspec spec = { .it_value = vela_ns_timespec(when) };
    timerfd_settime(output->latch_fd, TFD_TIMER_ABSTIME, &spec, NULL);
    output->latch_armed = true;
}

static int handle_latch_timer(int fd, uint32_t mask, void *data)
{
    struct vela_output *output = data;
    uint64_t expirations = 0;
    if (read(fd, &expirations, sizeof(expirations)) < 0) {
        return 0;
    }
    output->latch_armed = false;
    on_frame(output);
    return 0;
}

static void on_frame_event(struct vela_output *output)
{
    if (output->latch_armed || (!output->frame_requested && !output->wlr->needs_frame)) {
        return; // già pianificato, o niente da fare
    }
    collect_costs(output);
    // Tearing (un gioco a schermo intero che lo chiede), o VRR con un gioco
    // in scanout diretto: il suo frame va sullo schermo appena arriva, non al
    // momento migliore prima di un vblank che col VRR non è fisso.
    const struct vela_frame_delivered *last = &output->frame->delivered;
    if (last->tearing || (last->scanout && output->wlr->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED)) {
        output->planned = false;
        on_frame(output);
        return;
    }
    int64_t now = vela_now_ns();
    output->plan = vela_frame_clock_plan(&output->clock, now, output->latching);
    output->planned = true;
    if (output->plan.start - now > 50000) {
        arm_latch(output, output->plan.start);
        return;
    }
    on_frame(output);
}

// Il VRR secondo la scelta dell'utente (vela_server.vrr_mode): se va
// acceso o spento lo mette in `state` e restituisce true. "games": solo con
// un'app a schermo intero su questo schermo (come l'"Automatico" di KWin: il
// desktop non sfarfalla sui monitor che lo fanno); "always"; "no".
static bool vrr_state(struct vela_output *output, struct wlr_output_state *state)
{
    struct vela_server *server = output->server;
    bool wanted = server->vrr_mode == 2 || (server->vrr_mode == 1 && vela_views_fullscreen_on(server, output));
    bool enabled = output->wlr->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED;
    if (wanted == enabled || (wanted && output->vrr_unsupported)) {
        return false;
    }
    wlr_output_state_set_adaptive_sync_enabled(state, wanted);
    if (!wlr_output_test_state(output->wlr, state)) {
        if (wanted) {
            wlr_log(WLR_INFO, "%s: VRR not supported", output->wlr->name);
            output->vrr_unsupported = true;
        }
        state->committed &= ~WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
        return false;
    }
    wlr_log(WLR_INFO, "%s: VRR %s", output->wlr->name, wanted ? "on" : "off");
    return true;
}

static void log_stats(struct vela_output *output, int64_t now)
{
    // VELA_STATS=1: le statistiche anche senza il log dettagliato.
    bool requested = vela_env_flag("VELA_STATS");
    struct vela_frame_stats stats;
    if (!vela_frame_clock_take_stats(&output->clock, now, &stats) || stats.fps <= 0.0) {
        return;
    }
    wlr_log(requested ? WLR_INFO : WLR_DEBUG,
        "%s: %.2f fps (period %.3f ms, latency %d vblank); cost %.3f ms + margin %.3f ms; "
        "draw to light %.3f ms; prediction error mean %.3f ms, max %.3f ms; missed vblanks %d",
        output->wlr->name, stats.fps, vela_frame_clock_period(&output->clock) / 1e6, output->clock.latency_frames,
        stats.cost_ms, stats.margin_ms, stats.latency_ms, stats.error_mean_ms, stats.error_max_ms, stats.missed);
    if (requested && output->breakdown_count > 0) {
        const double n = output->breakdown_count;
        const double *sum = output->breakdown_sum;
        const double *max = output->breakdown_max;
        wlr_log(WLR_INFO,
            "%s: mean (max) late wakeup %.3f (%.3f) ms, CPU %.3f (%.3f) ms, "
            "GPU wait %.3f (%.3f) ms, GPU work %.3f (%.3f) ms",
            output->wlr->name, sum[0] / n, max[0], sum[1] / n, max[1], sum[2] / n, max[2], sum[3] / n, max[3]);
    }
    memset(output->breakdown_sum, 0, sizeof(output->breakdown_sum));
    memset(output->breakdown_max, 0, sizeof(output->breakdown_max));
    output->breakdown_count = 0;
}

static void on_frame(struct vela_output *output)
{
    // Uno scambio di pagina ancora in corso (es. dopo un cambio di modo): il
    // suo evento "frame" ci richiamerà.
    if (output->vblank_fd < 0 && output->wlr->frame_pending) {
        output->planned = false;
        return;
    }
    int64_t now = vela_now_ns();
    struct vela_frame_plan plan = output->planned ? output->plan : vela_frame_clock_plan(&output->clock, now, false);
    output->planned = false;
    output->frame_requested = false;

    // Prima si fa avanzare ogni animazione all'istante in cui questo frame
    // diventerà luce (docs/renderer.md §4.2), poi si disegna.
    vela_server_animate(output->server, plan.present);

    struct wlr_box area = vela_output_box(output);
    // Il VRR da accendere o spegnere va nel commit di questo frame.
    struct wlr_output_state vrr;
    wlr_output_state_init(&vrr);
    bool vrr_change = vrr_state(output, &vrr);
    bool rendered = vela_output_frame_render(output->frame, area.x, area.y, vrr_change ? &vrr : NULL);
    wlr_output_state_finish(&vrr);
    if (rendered) {
        vela_server_output_rendered(output->server, output);
        const struct vela_frame_delivered *delivered = &output->frame->delivered;
        struct vela_delivery delivery = {
            output->wlr->commit_seq, plan.start, now, vela_now_ns(), delivered->point, delivered->timing_slot,
        };
        vela_frame_clock_committed(&output->clock, output->wlr->commit_seq, plan.start, plan.present);
        if (output->delivery_count == VELA_OUTPUT_DELIVERIES) {
            memmove(&output->deliveries[0], &output->deliveries[1],
                (VELA_OUTPUT_DELIVERIES - 1) * sizeof(output->deliveries[0]));
            --output->delivery_count;
        }
        output->deliveries[output->delivery_count++] = delivery;
        // Col vblank virtuale il frame compare al primo battito in cui è pronto.
        if (output->vblank_fd >= 0) {
            output->awaiting = delivery;
            output->awaiting_present = true;
            arm_virtual_vblank(output);
        }
    }

    struct timespec when;
    clock_gettime(CLOCK_MONOTONIC, &when);
    vela_output_frame_send_frame_done(output->frame, &when);
    log_stats(output, now);
}

// Quando il frame era pronto: commit fatto e GPU finita. false se non si sa
// ancora.
static bool ready_time(struct vela_output *output, const struct vela_delivery *delivery, int64_t *when)
{
    if (delivery->point == 0) {
        *when = delivery->committed_at;
        return true;
    }
    struct vela_renderer *renderer = output->server->renderer;
    struct vela_gpu_timing timing;
    if (vela_renderer_read_timing(renderer, delivery->timing_slot, delivery->point, &timing)) {
        // Senza timestamp calibrati si sa solo quanto ha lavorato la GPU: si
        // suppone che abbia cominciato al commit.
        *when = timing.absolute ? (delivery->committed_at > timing.end_ns ? delivery->committed_at : timing.end_ns)
                                : delivery->committed_at + timing.end_ns;
        return true;
    }
    if (delivery->point <= vela_renderer_completed(renderer)) {
        *when = delivery->committed_at; // finito, ma senza misura
        return true;
    }
    return false;
}

static void breakdown_add(struct vela_output *output, int i, double ms)
{
    output->breakdown_sum[i] += ms;
    if (ms > output->breakdown_max[i]) {
        output->breakdown_max[i] = ms;
    }
}

// Il costo dei frame già consegnati, appena la GPU li ha finiti.
static void collect_costs(struct vela_output *output)
{
    int64_t now = vela_now_ns();
    int kept = 0;
    for (int i = 0; i < output->delivery_count; ++i) {
        const struct vela_delivery *delivery = &output->deliveries[i];
        int64_t ready = 0;
        if (ready_time(output, delivery, &ready)) {
            vela_frame_clock_add_cost(&output->clock, now, ready - delivery->start);
            breakdown_add(output, 0, (double)(delivery->woke_at - delivery->start) / 1e6);
            breakdown_add(output, 1, (double)(delivery->committed_at - delivery->woke_at) / 1e6);
            struct vela_gpu_timing timing;
            if (delivery->point
                && vela_renderer_read_timing(output->server->renderer, delivery->timing_slot, delivery->point, &timing)
                && timing.absolute) {
                breakdown_add(output, 2, (double)(timing.start_ns - delivery->committed_at) / 1e6);
                breakdown_add(output, 3, (double)(timing.end_ns - timing.start_ns) / 1e6);
            }
            ++output->breakdown_count;
            continue;
        }
        if (now - delivery->committed_at > VELA_NS_PER_SEC) {
            continue; // misura persa
        }
        output->deliveries[kept++] = *delivery;
    }
    output->delivery_count = kept;
}

// ----------------------------------------------------------- vblank virtuale --
//
// Schermo headless: un vblank virtuale esatto al nanosecondo, per provare
// qualunque frequenza (il timer del backend headless di wlroots lavora al
// millisecondo e non ha vblank). Batte solo quando un frame consegnato
// aspetta di comparire; un frame compare al primo vblank in cui è pronto
// (anche la GPU deve aver finito), come su uno schermo vero.

static int handle_virtual_vblank(int fd, uint32_t mask, void *data);

static void start_virtual_vblank(struct vela_output *output)
{
    output->vblank_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (output->vblank_fd < 0) {
        wlr_log_errno(WLR_ERROR, "%s: timerfd for the virtual vblank", output->wlr->name);
        return;
    }
    output->vblank_source = wl_event_loop_add_fd(output->server->loop, output->vblank_fd, WL_EVENT_READABLE,
        handle_virtual_vblank, output);
    output->last_vblank = vela_now_ns();
    wlr_log(WLR_INFO, "%s: virtual vblank every %.3f ms", output->wlr->name,
        vela_frame_clock_period(&output->clock) / 1e6);
}

// Il prossimo vblank sulla griglia esatta (ultimo vblank + multipli del
// periodo), anche se il timer è rimasto fermo a lungo.
static void arm_virtual_vblank(struct vela_output *output)
{
    if (output->vblank_armed || output->vblank_fd < 0) {
        return;
    }
    int64_t period = vela_frame_clock_period(&output->clock);
    int64_t now = vela_now_ns();
    int64_t next = output->last_vblank + period;
    if (next <= now) {
        next = output->last_vblank + ((now - output->last_vblank) / period + 1) * period;
    }
    const struct itimerspec when = { .it_value = vela_ns_timespec(next) };
    timerfd_settime(output->vblank_fd, TFD_TIMER_ABSTIME, &when, NULL);
    output->vblank_armed = true;
}

static int handle_virtual_vblank(int fd, uint32_t mask, void *data)
{
    struct vela_output *output = data;
    uint64_t expirations = 0;
    if (read(fd, &expirations, sizeof(expirations)) < 0) {
        return 0;
    }
    output->vblank_armed = false;
    int64_t period = vela_frame_clock_period(&output->clock);
    int64_t now = vela_now_ns();
    // L'ultimo vblank della griglia passato (il timer può svegliarci tardi).
    int64_t anchor = output->last_vblank;
    int64_t steps = (now - anchor) / period;
    int64_t vblank = anchor + (steps > 1 ? steps : 1) * period;
    output->last_vblank = vblank;

    if (output->awaiting_present) {
        // Il frame compare al primo vblank in cui era pronto: commit fatto e
        // GPU finita. Se non lo è ancora, resta sullo schermo il precedente e
        // si riprova al prossimo battito (vblank perso).
        int64_t ready = 0;
        if (!ready_time(output, &output->awaiting, &ready)) {
            arm_virtual_vblank(output);
            return 0;
        }
        int64_t late = ready - anchor > 0 ? ready - anchor : 0;
        int64_t periods = (late + period - 1) / period;
        int64_t shown_at = anchor + (periods > 1 ? periods : 1) * period;
        if (shown_at > vblank) {
            arm_virtual_vblank(output);
            return 0;
        }
        vela_frame_clock_presented(&output->clock, output->awaiting.seq, shown_at, period);
        output->awaiting_present = false;
        collect_costs(output);
    }
    // Come l'evento "frame" di DRM al vblank dopo una consegna.
    on_frame_event(output);
    return 0;
}

// ---------------------------------------------------- aree in pixel fisici --

struct wlr_box vela_output_box(const struct vela_output *output)
{
    struct wlr_box box = { 0 };
    wlr_output_layout_get_box(output->server->output_layout, output->wlr, &box);
    return box;
}

struct vela_area vela_output_from_physical(const struct vela_output *output, struct wlr_box physical)
{
    struct wlr_box full = vela_output_box(output);
    double scale = output->wlr->scale;
    return (struct vela_area) { full.x + physical.x / scale, full.y + physical.y / scale, physical.width / scale,
        physical.height / scale };
}

struct vela_area vela_output_full_area(const struct vela_output *output)
{
    int width = 0;
    int height = 0;
    wlr_output_transformed_resolution(output->wlr, &width, &height);
    return vela_output_from_physical(output, (struct wlr_box) { 0, 0, width, height });
}

// L'area libera dai pannelli in pixel dello schermo: i bordi che toccano
// quelli dello schermo restano esattamente sui suoi pixel.
struct wlr_box vela_output_physical_usable(const struct vela_output *output)
{
    struct wlr_box full = vela_output_box(output);
    const struct wlr_box *usable = &output->usable;
    int width = 0;
    int height = 0;
    wlr_output_transformed_resolution(output->wlr, &width, &height);
    double scale = output->wlr->scale;
    int x1 = vela_physical_edge(usable->x, full.x, full.x + full.width, width, scale);
    int x2 = vela_physical_edge(usable->x + usable->width, full.x, full.x + full.width, width, scale);
    int y1 = vela_physical_edge(usable->y, full.y, full.y + full.height, height, scale);
    int y2 = vela_physical_edge(usable->y + usable->height, full.y, full.y + full.height, height, scale);
    return (struct wlr_box) { x1, y1, x2 > x1 ? x2 - x1 : 0, y2 > y1 ? y2 - y1 : 0 };
}

struct vela_area vela_output_usable_area(const struct vela_output *output)
{
    return vela_output_from_physical(output, vela_output_physical_usable(output));
}

// Un bordo dello schermo senza un altro schermo accanto: ciò che sborda lì
// non si vede.
static bool open_at(const struct vela_output *output, double lx, double ly)
{
    return !wlr_output_layout_output_at(output->server->output_layout, lx, ly);
}

// Il client disegna un buffer di round(dimensione × scala) pixel
// (fractional-scale-v1, scala in 120esimi): per ogni asse si cerca la
// dimensione logica che dà esattamente i pixel dell'area.
struct vela_placement vela_output_place(const struct vela_output *output, struct vela_area area)
{
    struct wlr_box full = vela_output_box(output);
    int screen_width = 0;
    int screen_height = 0;
    wlr_output_transformed_resolution(output->wlr, &screen_width, &screen_height);
    double scale = output->wlr->scale;
    double mid_x = full.x + full.width / 2.0;
    double mid_y = full.y + full.height / 2.0;

    int px = (int)lround((area.x - full.x) * scale);
    int py = (int)lround((area.y - full.y) * scale);
    int pw = (int)lround(area.width * scale);
    int ph = (int)lround(area.height * scale);
    struct vela_axis x = vela_place_axis(px, pw, screen_width, scale, open_at(output, full.x - 0.5, mid_y),
        open_at(output, full.x + full.width + 0.5, mid_y));
    struct vela_axis y = vela_place_axis(py, ph, screen_height, scale, open_at(output, mid_x, full.y - 0.5),
        open_at(output, mid_x, full.y + full.height + 0.5));
    return (struct vela_placement) { full.x + x.offset, full.y + y.offset, x.size, y.size };
}

void vela_output_arrange_layers(struct vela_output *output)
{
    struct wlr_box full = vela_output_box(output);
    struct wlr_box area = full;
    vela_layers_configure(output->server, output, &full, &area);
    bool changed = area.x != output->usable.x || area.y != output->usable.y || area.width != output->usable.width
        || area.height != output->usable.height;
    output->usable = area;
    if (changed) {
        vela_views_usable_changed(output->server, output);
    }
}

struct vela_output *vela_output_at(const struct vela_server *server, double lx, double ly)
{
    struct wlr_output *wlr = wlr_output_layout_output_at(server->output_layout, lx, ly);
    return wlr ? wlr->data : NULL;
}

struct vela_output *vela_output_named(const struct vela_server *server, const char *name)
{
    struct vela_output *output;
    wl_list_for_each (output, &server->outputs, link) {
        if (name && strcmp(output->wlr->name, name) == 0) {
            return output;
        }
    }
    return NULL;
}

struct wlr_box vela_output_fit(const struct vela_output *output, struct wlr_box frame)
{
    const struct wlr_box *area = &output->usable;
    frame.width = vela_min(frame.width, area->width);
    frame.height = vela_min(frame.height, area->height);
    frame.x = vela_clamp(frame.x, area->x, area->x + area->width - frame.width);
    frame.y = vela_clamp(frame.y, area->y, area->y + area->height - frame.height);
    return frame;
}

struct vela_output *vela_output_under_cursor(const struct vela_server *server)
{
    struct vela_output *output = vela_output_at(server, server->cursor->x, server->cursor->y);
    if (output || wl_list_empty(&server->outputs)) {
        return output;
    }
    return wl_container_of(server->outputs.next, output, link);
}
