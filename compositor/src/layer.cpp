// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server.hpp"

#include <algorithm>

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
    , tree(std::make_unique<scene::Tree>(s.layerTree(surface->pending.layer,
          surface->pending.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)))
    , surfaceNode(std::make_unique<scene::SurfaceNode>(tree.get(), surface->surface))
    , layer(surface->pending.layer)
{
    tree->data = static_cast<SceneOwner*>(this);

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
        new Popup(static_cast<wlr_xdg_popup*>(data), tree.get(), [this] {
            Output* out = output();
            wlr_box box = out ? out->box() : wlr_box { 0, 0, 1920, 1080 };
            box.x -= int(std::lround(tree->x()));
            box.y -= int(std::lround(tree->y()));
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

    // Il client può spostarsi di strato (es. da "bottom" a "top"), o
    // chiedere la tastiera (e salire sopra lo schermo intero).
    if (wlr->initialized
        && (committed & (WLR_LAYER_SURFACE_V1_STATE_LAYER | WLR_LAYER_SURFACE_V1_STATE_KEYBOARD_INTERACTIVITY))) {
        layer = wlr->current.layer;
        scene::Tree* target = server.layerTree(layer, wantsKeyboard());
        if (tree->parent() != target) {
            tree->reparent(target);
        }
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

namespace {

// Lo spazio che una superficie "esclusiva" (la taskbar) toglie all'area utile.
void applyExclusiveZone(const wlr_layer_surface_v1_state& state, wlr_edges edge, wlr_box& usable)
{
    switch (edge) {
    case WLR_EDGE_NONE:
        return;
    case WLR_EDGE_TOP:
        usable.y += state.exclusive_zone + state.margin.top;
        usable.height -= state.exclusive_zone + state.margin.top;
        break;
    case WLR_EDGE_BOTTOM:
        usable.height -= state.exclusive_zone + state.margin.bottom;
        break;
    case WLR_EDGE_LEFT:
        usable.x += state.exclusive_zone + state.margin.left;
        usable.width -= state.exclusive_zone + state.margin.left;
        break;
    case WLR_EDGE_RIGHT:
        usable.width -= state.exclusive_zone + state.margin.right;
        break;
    }
    usable.width = std::max(usable.width, 0);
    usable.height = std::max(usable.height, 0);
}

} // namespace

// Come wlr_scene_layer_surface_v1_configure di wlroots: dimensione e
// posizione secondo ancore e margini, dentro l'area utile (o tutto lo
// schermo se la superficie lo chiede con exclusive_zone = -1).
void LayerSurface::configure(const wlr_box& full, wlr_box& usable)
{
    const wlr_layer_surface_v1_state& state = wlr->current;
    const wlr_box bounds = state.exclusive_zone == -1 ? full : usable;
    const uint32_t anchor = state.anchor;
    const bool left = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    const bool right = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const bool top = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    const bool bottom = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    wlr_box box { 0, 0, int(state.desired_width), int(state.desired_height) };
    if (box.width == 0) {
        box.x = bounds.x + state.margin.left;
        box.width = bounds.width - (state.margin.left + state.margin.right);
    } else if (left && right) {
        box.x = bounds.x + bounds.width / 2 - box.width / 2;
    } else if (left) {
        box.x = bounds.x + state.margin.left;
    } else if (right) {
        box.x = bounds.x + bounds.width - box.width - state.margin.right;
    } else {
        box.x = bounds.x + bounds.width / 2 - box.width / 2;
    }
    if (box.height == 0) {
        box.y = bounds.y + state.margin.top;
        box.height = bounds.height - (state.margin.top + state.margin.bottom);
    } else if (top && bottom) {
        box.y = bounds.y + bounds.height / 2 - box.height / 2;
    } else if (top) {
        box.y = bounds.y + state.margin.top;
    } else if (bottom) {
        box.y = bounds.y + bounds.height - box.height - state.margin.bottom;
    } else {
        box.y = bounds.y + bounds.height / 2 - box.height / 2;
    }

    tree->setPosition(box.x, box.y);
    wlr_layer_surface_v1_configure(wlr, uint32_t(std::max(box.width, 0)), uint32_t(std::max(box.height, 0)));
    if (wlr->surface->mapped && state.exclusive_zone > 0) {
        applyExclusiveZone(state, wlr_layer_surface_v1_get_exclusive_edge(wlr), usable);
    }
}

} // namespace vela
