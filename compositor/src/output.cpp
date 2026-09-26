#include "server.hpp"

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

    frame.connect(&wlr->events.frame, [this](void*) { onFrame(); });
    requestState.connect(&wlr->events.request_state, [this](void* data) {
        // Nel backend annidato: la finestra ospite è stata ridimensionata.
        auto* event = static_cast<wlr_output_event_request_state*>(data);
        wlr_output_commit_state(wlr, event->state);
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
    server.outputs.remove(this);
    wlr->data = nullptr;
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
            if (toplevel->maximized && toplevel->output() == this) {
                toplevel->applyMaximized();
            }
        }
    }
}

} // namespace vela
