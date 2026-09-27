#include "server.hpp"

namespace vela {

// ----------------------------------------------------------------- Popup --

Popup::Popup(wlr_xdg_popup* popup, scene::Tree* parent, BoxFn box)
    : xdg(popup)
    , tree(std::make_unique<scene::Tree>(parent))
    , surfaceNode(std::make_unique<scene::SurfaceNode>(tree.get(), popup->base->surface))
    , constraintBox(std::move(box))
{
    commit.connect(&xdg->base->surface->events.commit, [this](void*) {
        if (xdg->base->initial_commit) {
            unconstrain(); // invia anche il primo configure
        }
        // La posizione dipende dalla geometria sua e del genitore, che
        // cambiano con i commit.
        double x = 0.0;
        double y = 0.0;
        scene::popupPosition(xdg, x, y);
        tree->setPosition(x, y);
    });
    reposition.connect(&xdg->events.reposition, [this](void*) {
        unconstrain();
    });
    newPopup.connect(&xdg->base->events.new_popup, [this](void* data) {
        // Un popup di un popup: l'area resta nelle coordinate della
        // superficie radice (come vuole wlroots).
        new Popup(static_cast<wlr_xdg_popup*>(data), tree.get(), constraintBox);
    });
    destroy.connect(&xdg->events.destroy, [this](void*) { delete this; });
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
    , tree(std::make_unique<scene::Tree>(s.layers.windows.get()))
    , surfaceNode(std::make_unique<scene::SurfaceNode>(tree.get(), toplevel->base->surface))
{
    tree->data = static_cast<SceneOwner*>(this);
    xdg->base->data = this; // per risalire alla finestra genitore

    wlr_surface* surface = xdg->base->surface;
    map.connect(&surface->events.map, [this](void*) { onMap(); });
    unmap.connect(&surface->events.unmap, [this](void*) { onUnmap(); });
    commit.connect(&surface->events.commit, [this](void*) { onCommit(); });
    clientCommit.connect(&surface->events.client_commit, [this](void*) {
        // L'app sta per togliere il contenuto con un buffer nullo (finestra
        // nascosta): fotografiamo prima che il commit venga applicato e la
        // scena perda il buffer.
        const wlr_surface_state& pending = xdg->base->surface->pending;
        if (mapped && !minimized && !m_closeAnimated
            && (pending.committed & WLR_SURFACE_STATE_BUFFER) && !pending.buffer) {
            m_closeAnimated = server.animateSnapshot(this, Server::SnapshotKind::Close);
        }
    });
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
    requestMinimize.connect(&xdg->events.request_minimize, [this](void*) {
        setMinimized(true);
    });
    setTitle.connect(&xdg->events.set_title, [this](void*) {
        if (handle) {
            wlr_foreign_toplevel_handle_v1_set_title(handle, xdg->title ? xdg->title : "");
        }
        updateExtHandle();
    });
    setAppId.connect(&xdg->events.set_app_id, [this](void*) {
        if (handle) {
            wlr_foreign_toplevel_handle_v1_set_app_id(handle, xdg->app_id ? xdg->app_id : "");
        }
        updateExtHandle();
    });
    setParent.connect(&xdg->events.set_parent, [this](void*) { updateHandleParent(); });
    newPopup.connect(&xdg->base->events.new_popup, [this](void* data) {
        new Popup(static_cast<wlr_xdg_popup*>(data), tree.get(), [this] {
            // Area dello schermo espressa rispetto all'origine della
            // superficie della finestra.
            Output* out = output();
            wlr_box box = out ? out->box() : wlr_box { 0, 0, 1920, 1080 };
            box.x -= int(std::lround(tree->x()));
            box.y -= int(std::lround(tree->y()));
            return box;
        });
    });
}

Toplevel::~Toplevel()
{
    destroyHandle();
    capture.reset();
    server.forget(this);
}

wlr_ext_image_capture_source_v1* Toplevel::prepareCapture()
{
    // L'aspetto attuale, anche se ridotta a icona o coperta da altre
    // finestre: il renderer disegna solo lei.
    if (!capture) {
        capture = std::make_unique<scene::WindowCapture>(tree.get(), [this] { return frameBox(); },
            *server.velaRenderer, server.allocator);
    }
    return capture->source();
}

wlr_box Toplevel::frameBox() const
{
    const wlr_box& geometry = xdg->base->geometry;
    return wlr_box {
        .x = int(std::lround(tree->x())) + geometry.x,
        .y = int(std::lround(tree->y())) + geometry.y,
        .width = geometry.width,
        .height = geometry.height,
    };
}

wlr_box Toplevel::minimizeTarget() const
{
    if (taskbarRect.width > 0) {
        return taskbarRect;
    }
    // Pulsante sconosciuto: il centro del bordo inferiore dello schermo.
    const Output* out = output();
    const wlr_box area = out ? out->box() : wlr_box { 0, 0, 1920, 1080 };
    return { area.x + area.width / 2 - 24, area.y + area.height - 48, 48, 48 };
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
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN
                | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE);
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
    if (m_animating || (!maximized && !fullscreen && snap == Snap::None)) {
        return;
    }
    Output* out = output();
    if (!out) {
        return;
    }
    const Area area = fullscreen ? out->fullArea() : maximized ? out->usableArea() : snapArea(out, snap);
    const Placement place = out->place(area);
    const wlr_box& geometry = xdg->base->geometry;
    tree->setPosition(place.x - geometry.x, place.y - geometry.y);
}

void Toplevel::onMap()
{
    mapped = true;

    Output* out = server.outputUnderCursor();
    const wlr_box& geometry = xdg->base->geometry;
    if (out && (fullscreen || maximized)) {
        const Placement place = out->place(fullscreen ? out->fullArea() : out->usableArea());
        m_targetX = place.x - geometry.x;
        m_targetY = place.y - geometry.y;
    } else if (out) {
        // Nuove finestre al centro dell'area utile, come fa Windows.
        const wlr_box& area = out->usable;
        m_targetX = area.x + std::max(0, (area.width - geometry.width) / 2) - geometry.x;
        m_targetY = area.y + std::max(0, (area.height - geometry.height) / 2) - geometry.y;
    }

    server.toplevels.push_front(this);
    createHandle();
    server.focusToplevel(this);
    startOpenAnimation();
}

void Toplevel::onUnmap()
{
    // Chiusura: la scena ha ancora gli ultimi buffer (anche se ha già
    // disabilitato l'albero), li fotografiamo e li facciamo sfumare. Se la
    // finestra è stata nascosta con un buffer nullo, l'istantanea c'è già.
    if (!minimized && !m_closeAnimated) {
        server.animateSnapshot(this, Server::SnapshotKind::Close);
    }
    m_closeAnimated = false;
    mapped = false;
    m_animating = false;
    if (minimized) {
        minimized = false;
        tree->setEnabled(true);
    }
    destroyHandle();
    server.forget(this);
}

// ---------------------------------------------------------------- taskbar --

void Toplevel::updateExtHandle()
{
    if (!extHandle) {
        return;
    }
    const wlr_ext_foreign_toplevel_handle_v1_state state {
        .title = xdg->title ? xdg->title : "",
        .app_id = xdg->app_id ? xdg->app_id : "",
    };
    wlr_ext_foreign_toplevel_handle_v1_update_state(extHandle, &state);
}

void Toplevel::createHandle()
{
    const wlr_ext_foreign_toplevel_handle_v1_state state {
        .title = xdg->title ? xdg->title : "",
        .app_id = xdg->app_id ? xdg->app_id : "",
    };
    extHandle = wlr_ext_foreign_toplevel_handle_v1_create(server.extToplevels, &state);
    extHandle->data = this;

    handle = wlr_foreign_toplevel_handle_v1_create(server.foreignToplevels);
    handle->data = this;
    wlr_foreign_toplevel_handle_v1_set_title(handle, xdg->title ? xdg->title : "");
    wlr_foreign_toplevel_handle_v1_set_app_id(handle, xdg->app_id ? xdg->app_id : "");
    wlr_foreign_toplevel_handle_v1_set_maximized(handle, maximized);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, fullscreen);
    updateHandleParent();

    handleRequests.activate.connect(&handle->events.request_activate, [this](void*) {
        server.focusToplevel(this);
    });
    handleRequests.close.connect(&handle->events.request_close, [this](void*) {
        wlr_xdg_toplevel_send_close(xdg);
    });
    handleRequests.maximize.connect(&handle->events.request_maximize, [this](void* data) {
        setMaximized(static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data)->maximized);
    });
    handleRequests.minimize.connect(&handle->events.request_minimize, [this](void* data) {
        setMinimized(static_cast<wlr_foreign_toplevel_handle_v1_minimized_event*>(data)->minimized);
    });
    handleRequests.fullscreen.connect(&handle->events.request_fullscreen, [this](void* data) {
        setFullscreen(static_cast<wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data)->fullscreen);
    });
    handleRequests.rectangle.connect(&handle->events.set_rectangle, [this](void* data) {
        // La taskbar ci dice dove sta il pulsante di questa finestra, in
        // coordinate della sua superficie: lo portiamo in coordinate globali.
        auto* event = static_cast<wlr_foreign_toplevel_handle_v1_set_rectangle_event*>(data);
        taskbarRect = {};
        if (event->width <= 0 || event->height <= 0) {
            return;
        }
        for (LayerSurface* layer : server.layerSurfaces) {
            if (layer->wlr->surface == event->surface) {
                double lx = 0.0;
                double ly = 0.0;
                layer->tree->coords(lx, ly);
                taskbarRect = { int(std::lround(lx)) + event->x, int(std::lround(ly)) + event->y, event->width,
                    event->height };
                return;
            }
        }
    });
}

void Toplevel::destroyHandle()
{
    if (extHandle) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(extHandle);
        extHandle = nullptr;
    }
    if (!handle) {
        return;
    }
    // wlroots verifica che nessuno ascolti più la maniglia quando la distrugge.
    handleRequests.activate.disconnect();
    handleRequests.close.disconnect();
    handleRequests.maximize.disconnect();
    handleRequests.minimize.disconnect();
    handleRequests.fullscreen.disconnect();
    handleRequests.rectangle.disconnect();
    wlr_foreign_toplevel_handle_v1_destroy(handle);
    handle = nullptr;
}

// Le finestre di dialogo dichiarano la finestra da cui dipendono: la taskbar
// così sa di non mostrarle come app separate.
void Toplevel::updateHandleParent()
{
    if (!handle) {
        return;
    }
    const Toplevel* parent = xdg->parent ? static_cast<Toplevel*>(xdg->parent->base->data) : nullptr;
    wlr_foreign_toplevel_handle_v1_set_parent(handle, parent ? parent->handle : nullptr);
}

void Toplevel::setActivated(bool on)
{
    if (xdg->base->initialized) {
        wlr_xdg_toplevel_set_activated(xdg, on);
    }
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_activated(handle, on);
    }
}

// ------------------------------------------------------------ animazione --

void Toplevel::startOpenAnimation()
{
    m_openTween = Tween(motion::windowOpenMs, &motion::decelerate);
    m_animating = true;
    m_openFrames = 0;
    applyOpenFrame(0.0);
    server.addAnimation(this);
}

bool Toplevel::tickOpen(double nowMs)
{
    if (!m_animating) {
        return false;
    }
    applyOpenFrame(m_openTween.progress(nowMs));
    ++m_openFrames;
    if (m_openTween.finished(nowMs)) {
        wlr_log(WLR_DEBUG, "Animazione di apertura: %d frame", m_openFrames);
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
    tree->setPosition(m_targetX, m_targetY + std::round((1.0 - progress) * rise));
    // L'opacità arriva a 1 un po' prima del movimento: la finestra risulta
    // leggibile subito, il movimento finale è solo "assestamento".
    setOpacity(static_cast<float>(std::min(1.0, progress * 1.4)));
}

void Toplevel::setOpacity(float opacity)
{
    tree->setOpacity(opacity);
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
        if (mapped && snap == Snap::None) {
            restore = frameBox(); // da agganciata si torna alla dimensione libera
        }
        if (snap != Snap::None) {
            snap = Snap::None;
            wlr_xdg_toplevel_set_tiled(xdg, WLR_EDGE_NONE);
        }
        maximized = true;
        applyMaximized();
        return;
    }

    maximized = false;
    wlr_xdg_toplevel_set_maximized(xdg, false);
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_maximized(handle, false);
    }
    wlr_xdg_toplevel_set_size(xdg, restore.width, restore.height);
    if (mapped && restore.width > 0) {
        const wlr_box& geometry = xdg->base->geometry;
        tree->setPosition(restore.x - geometry.x, restore.y - geometry.y);
    }
}

void Toplevel::applyMaximized()
{
    Output* out = mapped ? output() : server.outputUnderCursor();
    if (!out || !xdg->base->initialized) {
        return;
    }
    wlr_xdg_toplevel_set_maximized(xdg, true);
    const Placement place = out->place(out->usableArea());
    wlr_xdg_toplevel_set_size(xdg, place.width, place.height);
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_maximized(handle, true);
    }
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
        if (mapped && !maximized && snap == Snap::None) {
            restore = frameBox();
        }
        fullscreen = true;
        wlr_xdg_toplevel_set_fullscreen(xdg, true);
        if (handle) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, true);
        }
        if (out) {
            const Placement place = out->place(out->fullArea());
            wlr_xdg_toplevel_set_size(xdg, place.width, place.height);
        }
        // Sopra taskbar e pannelli.
        tree->reparent(server.layers.fullscreen.get());
        keepInPlace();
        return;
    }

    fullscreen = false;
    wlr_xdg_toplevel_set_fullscreen(xdg, false);
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, false);
    }
    tree->reparent(server.layers.windows.get());
    if (maximized) {
        applyMaximized();
    } else if (snap != Snap::None) {
        applySnap(output());
    } else {
        wlr_xdg_toplevel_set_size(xdg, restore.width, restore.height);
        if (mapped && restore.width > 0) {
            const wlr_box& geometry = xdg->base->geometry;
            tree->setPosition(restore.x - geometry.x, restore.y - geometry.y);
        }
    }
}

// ------------------------------------------------------ riduci a icona --

// Come su Windows: la finestra sparisce, va in fondo all'ordine di Alt+Tab e
// la tastiera passa alla finestra successiva. Si ripristina dalla taskbar o
// con Alt+Tab (vedi Server::focusToplevel).
void Toplevel::setMinimized(bool on)
{
    if (!mapped || on == minimized) {
        return;
    }
    minimized = on;
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_minimized(handle, on);
    }

    if (!on) {
        // La finestra vera ricompare a fine volo (finishRestore); intanto
        // riceve già la tastiera.
        server.focusToplevel(this);
        if (!server.animateSnapshot(this, Server::SnapshotKind::Restore)) {
            finishRestore();
        }
        return;
    }

    finishOpenAnimation();
    const bool wasFocused = server.focusedToplevel() == this;
    tree->setEnabled(false);
    server.animateSnapshot(this, Server::SnapshotKind::Minimize);
    if (server.grabbed == this) {
        server.endSnapZone(false);
        server.grabbed = nullptr;
        server.cursorMode = CursorMode::Passthrough;
    }
    server.toplevels.remove(this);
    server.toplevels.push_back(this);
    if (wasFocused) {
        setActivated(false);
        server.refocus();
    }
}

void Toplevel::finishRestore()
{
    if (mapped && !minimized) {
        tree->setEnabled(true);
    }
}

} // namespace vela
