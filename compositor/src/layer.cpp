#include "server.hpp"

namespace vela {

void LayerSurface::create(Server& server, wlr_layer_surface_v1* surface)
{
    // Se il client non ha scelto uno schermo, usiamo quello sotto il cursore.
    if (!surface->output) {
        Output* out = server.outputUnderCursor();
        if (!out) {
            wlr_layer_surface_v1_destroy(surface);
            return;
        }
        surface->output = out->wlr;
    }
    new LayerSurface(server, surface);
}

LayerSurface::LayerSurface(Server& s, wlr_layer_surface_v1* surface)
    : SceneOwner(SceneKind::Layer)
    , server(s)
    , wlr(surface)
    , sceneLayer(wlr_scene_layer_surface_v1_create(s.layerTree(surface->pending.layer), surface))
{
    sceneLayer->tree->node.data = static_cast<SceneOwner*>(this);

    map.connect(&wlr->surface->events.map, [this](void*) {
        // Menu Start, launcher & co. ricevono subito la tastiera.
        const auto layer = wlr->current.layer;
        if (wantsKeyboard()
            && (layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP || layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)) {
            server.focusLayer(this);
        }
    });
    unmap.connect(&wlr->surface->events.unmap, [this](void*) {
        if (server.focusedLayerSurface == this) {
            server.refocus();
        }
    });
    commit.connect(&wlr->surface->events.commit, [this](void*) { onCommit(); });
    destroy.connect(&wlr->events.destroy, [this](void*) { delete this; });
    newPopup.connect(&wlr->events.new_popup, [this](void* data) {
        new Popup(static_cast<wlr_xdg_popup*>(data), sceneLayer->tree, [this] {
            Output* out = output();
            wlr_box box = out ? out->box() : wlr_box { 0, 0, 1920, 1080 };
            box.x -= sceneLayer->tree->node.x;
            box.y -= sceneLayer->tree->node.y;
            return box;
        });
    });

    server.layerSurfaces.push_back(this);
}

LayerSurface::~LayerSurface()
{
    server.forget(this);
    if (Output* out = output()) {
        out->arrangeLayers(); // libera lo spazio che occupava
    }
}

Output* LayerSurface::output() const
{
    return wlr->output ? static_cast<Output*>(wlr->output->data) : nullptr;
}

bool LayerSurface::wantsKeyboard() const
{
    return wlr->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
}

void LayerSurface::onCommit()
{
    const uint32_t committed = wlr->current.committed;

    // Il client può spostarsi di strato (es. da "bottom" a "top").
    if (wlr->initialized && (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER)) {
        wlr_scene_node_reparent(&sceneLayer->tree->node, server.layerTree(wlr->current.layer));
    }

    // Ridisponiamo solo quando cambia qualcosa che conta: ogni configure
    // costringe il client a ridisegnare.
    if (wlr->initial_commit || committed || wlr->surface->mapped != mapped) {
        mapped = wlr->surface->mapped;
        if (Output* out = output()) {
            out->arrangeLayers();
        }
    }
}

} // namespace vela
