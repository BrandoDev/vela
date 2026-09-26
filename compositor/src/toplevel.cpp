#include "server.hpp"

namespace vela {

// ----------------------------------------------------------------- Popup --

Popup::Popup(wlr_xdg_popup* popup, wlr_scene_tree* parent, BoxFn box)
    : xdg(popup)
    , tree(wlr_scene_xdg_surface_create(parent, popup->base))
    , constraintBox(std::move(box))
{
    commit.connect(&xdg->base->surface->events.commit, [this](void*) {
        if (xdg->base->initial_commit) {
            unconstrain(); // invia anche il primo configure
        }
    });
    reposition.connect(&xdg->events.reposition, [this](void*) {
        unconstrain();
    });
    newPopup.connect(&xdg->base->events.new_popup, [this](void* data) {
        new Popup(static_cast<wlr_xdg_popup*>(data), tree, constraintBox);
    });
    destroy.connect(&xdg->events.destroy, [this](void*) {
        delete this; // l'albero della scena si distrugge da solo
    });
}

void Popup::unconstrain()
{
    const wlr_box box = constraintBox();
    wlr_xdg_popup_unconstrain_from_box(xdg, &box);
}

// -------------------------------------------------------------- Toplevel --

Toplevel::Toplevel(Server& s, wlr_xdg_toplevel* toplevel)
    : SceneOwner(SceneKind::Toplevel)
    , server(s)
    , xdg(toplevel)
    , tree(wlr_scene_xdg_surface_create(s.layers.windows, toplevel->base))
{
    tree->node.data = static_cast<SceneOwner*>(this);

    wlr_surface* surface = xdg->base->surface;
    map.connect(&surface->events.map, [this](void*) { onMap(); });
    unmap.connect(&surface->events.unmap, [this](void*) { onUnmap(); });
    commit.connect(&surface->events.commit, [this](void*) { onCommit(); });
    destroy.connect(&xdg->events.destroy, [this](void*) { delete this; });

    requestMove.connect(&xdg->events.request_move, [this](void*) {
        server.beginInteractive(this, CursorMode::Move, 0);
    });
    requestResize.connect(&xdg->events.request_resize, [this](void* data) {
        auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
        server.beginInteractive(this, CursorMode::Resize, event->edges);
    });
    requestMaximize.connect(&xdg->events.request_maximize, [this](void*) {
        if (xdg->base->initialized) {
            setMaximized(xdg->requested.maximized);
        }
    });
    requestFullscreen.connect(&xdg->events.request_fullscreen, [this](void*) {
        if (xdg->base->initialized) {
            setFullscreen(xdg->requested.fullscreen);
        }
    });
    newPopup.connect(&xdg->base->events.new_popup, [this](void* data) {
        new Popup(static_cast<wlr_xdg_popup*>(data), tree, [this] {
            // Area dello schermo espressa rispetto all'origine della finestra.
            Output* out = output();
            wlr_box box = out ? out->box() : wlr_box { 0, 0, 1920, 1080 };
            box.x -= tree->node.x;
            box.y -= tree->node.y;
            return box;
        });
    });
}

Toplevel::~Toplevel()
{
    server.forget(this);
}

wlr_box Toplevel::frameBox() const
{
    const wlr_box& geometry = xdg->base->geometry;
    return wlr_box {
        .x = tree->node.x + geometry.x,
        .y = tree->node.y + geometry.y,
        .width = geometry.width,
        .height = geometry.height,
    };
}

Output* Toplevel::output() const
{
    const wlr_box frame = frameBox();
    if (Output* out = server.outputAt(frame.x + frame.width / 2.0, frame.y + frame.height / 2.0)) {
        return out;
    }
    return server.outputUnderCursor();
}

void Toplevel::onCommit()
{
    if (xdg->base->initial_commit) {
        // Primo commit: diciamo al client cosa sappiamo fare e lasciamo che
        // scelga la sua dimensione (0x0), a meno che non abbia già chiesto
        // di partire massimizzato o a schermo intero.
        wlr_xdg_toplevel_set_wm_capabilities(xdg,
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        if (xdg->requested.fullscreen) {
            setFullscreen(true);
        } else if (xdg->requested.maximized) {
            setMaximized(true);
        } else {
            wlr_xdg_toplevel_set_size(xdg, 0, 0);
        }
        return;
    }
    if (mapped) {
        keepInPlace();
    }
}

// Le finestre massimizzate o a schermo intero possono cambiare il margine
// d'ombra (geometry.x/y) quando ridisegnano: le teniamo allineate.
void Toplevel::keepInPlace()
{
    if (m_animating || (!maximized && !fullscreen)) {
        return;
    }
    Output* out = output();
    if (!out) {
        return;
    }
    const wlr_box area = fullscreen ? out->box() : out->usable;
    const wlr_box& geometry = xdg->base->geometry;
    wlr_scene_node_set_position(&tree->node, area.x - geometry.x, area.y - geometry.y);
}

void Toplevel::onMap()
{
    mapped = true;

    Output* out = server.outputUnderCursor();
    const wlr_box& geometry = xdg->base->geometry;
    if (out && fullscreen) {
        const wlr_box area = out->box();
        m_targetX = area.x - geometry.x;
        m_targetY = area.y - geometry.y;
    } else if (out && maximized) {
        m_targetX = out->usable.x - geometry.x;
        m_targetY = out->usable.y - geometry.y;
    } else if (out) {
        // Nuove finestre al centro dell'area utile, come fa Windows.
        const wlr_box& area = out->usable;
        m_targetX = area.x + std::max(0, (area.width - geometry.width) / 2) - geometry.x;
        m_targetY = area.y + std::max(0, (area.height - geometry.height) / 2) - geometry.y;
    }

    server.toplevels.push_front(this);
    server.focusToplevel(this);
    startOpenAnimation();
}

void Toplevel::onUnmap()
{
    mapped = false;
    m_animating = false;
    server.forget(this);
}

// ------------------------------------------------------------ animazione --

void Toplevel::startOpenAnimation()
{
    m_openTween = Tween(motion::windowOpenMs, &motion::decelerate);
    m_animating = true;
    applyOpenFrame(0.0);
    server.addAnimation(this);
}

bool Toplevel::tickOpen(double nowMs)
{
    if (!m_animating) {
        return false;
    }
    applyOpenFrame(m_openTween.progress(nowMs));
    if (m_openTween.finished(nowMs)) {
        finishOpenAnimation();
        return false;
    }
    return true;
}

void Toplevel::finishOpenAnimation()
{
    if (!m_animating) {
        return;
    }
    m_animating = false;
    applyOpenFrame(1.0);
}

void Toplevel::applyOpenFrame(double progress)
{
    // Massimizzate e a schermo intero compaiono solo in dissolvenza.
    const int rise = (maximized || fullscreen) ? 0 : motion::windowOpenRisePx;
    const int y = m_targetY + static_cast<int>(std::lround((1.0 - progress) * rise));
    wlr_scene_node_set_position(&tree->node, m_targetX, y);
    // L'opacità arriva a 1 un po' prima del movimento: la finestra risulta
    // leggibile subito, il movimento finale è solo "assestamento".
    setOpacity(static_cast<float>(std::min(1.0, progress * 1.4)));
}

void Toplevel::setOpacity(float opacity)
{
    wlr_scene_node_for_each_buffer(&tree->node,
        [](wlr_scene_buffer* buffer, int, int, void* data) {
            wlr_scene_buffer_set_opacity(buffer, *static_cast<float*>(data));
        },
        &opacity);
}

// --------------------------------------------------- massimizza/fullscreen --

void Toplevel::setMaximized(bool on)
{
    if (!xdg->base->initialized) {
        return;
    }
    if (on == maximized || fullscreen) {
        // Il protocollo vuole comunque una risposta alla richiesta.
        wlr_xdg_surface_schedule_configure(xdg->base);
        return;
    }
    finishOpenAnimation();

    if (on) {
        if (mapped) {
            restore = frameBox();
        }
        maximized = true;
        applyMaximized();
        return;
    }

    maximized = false;
    wlr_xdg_toplevel_set_maximized(xdg, false);
    wlr_xdg_toplevel_set_size(xdg, restore.width, restore.height);
    if (mapped && restore.width > 0) {
        const wlr_box& geometry = xdg->base->geometry;
        wlr_scene_node_set_position(&tree->node, restore.x - geometry.x, restore.y - geometry.y);
    }
}

void Toplevel::applyMaximized()
{
    Output* out = mapped ? output() : server.outputUnderCursor();
    if (!out || !xdg->base->initialized) {
        return;
    }
    wlr_xdg_toplevel_set_maximized(xdg, true);
    wlr_xdg_toplevel_set_size(xdg, out->usable.width, out->usable.height);
    keepInPlace();
}

void Toplevel::setFullscreen(bool on)
{
    if (!xdg->base->initialized) {
        return;
    }
    if (on == fullscreen) {
        wlr_xdg_surface_schedule_configure(xdg->base);
        return;
    }
    finishOpenAnimation();
    Output* out = mapped ? output() : server.outputUnderCursor();

    if (on) {
        if (mapped && !maximized) {
            restore = frameBox();
        }
        fullscreen = true;
        wlr_xdg_toplevel_set_fullscreen(xdg, true);
        if (out) {
            const wlr_box area = out->box();
            wlr_xdg_toplevel_set_size(xdg, area.width, area.height);
        }
        // Sopra taskbar e pannelli.
        wlr_scene_node_reparent(&tree->node, server.layers.fullscreen);
        keepInPlace();
        return;
    }

    fullscreen = false;
    wlr_xdg_toplevel_set_fullscreen(xdg, false);
    wlr_scene_node_reparent(&tree->node, server.layers.windows);
    if (maximized) {
        applyMaximized();
    } else {
        wlr_xdg_toplevel_set_size(xdg, restore.width, restore.height);
        if (mapped && restore.width > 0) {
            const wlr_box& geometry = xdg->base->geometry;
            wlr_scene_node_set_position(&tree->node, restore.x - geometry.x, restore.y - geometry.y);
        }
    }
}

} // namespace vela
