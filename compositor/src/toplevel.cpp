#include "server.hpp"

#include "decoration.hpp"

namespace vela {

// ----------------------------------------------------------------- Popup --

Popup::Popup(wlr_xdg_popup* popup, scene::Tree* parent, BoxFn box)
    : xdg(popup)
    , tree(std::make_unique<scene::Tree>(parent))
    , surfaceNode(std::make_unique<scene::SurfaceNode>(tree.get(), popup->base->surface))
    , constraintBox(std::move(box))
{
    tree->unclipped = true; // un menu esce dalla finestra: niente angoli della finestra
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
    // Le app con la barra propria (GTK, libadwaita) chiedono il menu della
    // finestra col clic destro sulla loro barra.
    requestWindowMenu.connect(&xdg->events.request_show_window_menu, [this](void* data) {
        auto* event = static_cast<wlr_xdg_toplevel_show_window_menu_event*>(data);
        server.showWindowMenu(this, tree->x() + event->x, tree->y() + event->y);
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
    decoration.reset();
    destroyHandle();
    capture.reset();
    server.forget(this);
}

// -------------------------------------------------------- verso l'app --

wlr_surface* Toplevel::surface() const
{
    return xdg ? xdg->base->surface : x11->surface;
}

wlr_box Toplevel::geometry() const
{
    if (xdg) {
        // Con la barra di Vela il riquadro comincia sopra la superficie.
        const wlr_box g = xdg->base->geometry;
        const int bar = titleBarHeight();
        return { g.x, g.y - bar, g.width, g.height + bar };
    }
    // X11: niente margini d'ombra; la barra di Vela, se c'è, sta sopra.
    const int bar = titleBarHeight();
    return { 0, -bar, x11->width, x11->height + bar };
}

int Toplevel::titleBarHeight() const
{
    return decoration && !fullscreen ? Decoration::height : 0;
}

void Toplevel::updateDecoration()
{
    // Le finestre X11 che lasciano la barra al gestore di finestre (le app
    // con una barra propria, come Steam, dicono di no), ma non gli schermi
    // di avvio; le app Wayland che accettano la barra lato server
    // (xdg-decoration: Qt e KDE, Chromium se lo si sceglie).
    const bool wanted = x11
        ? !x11->override_redirect && x11->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL
            && !wlr_xwayland_surface_has_window_type(x11, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH)
        : xdgDecoration && xdgDecoration->current.mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    if (wanted && !decoration) {
        decoration = std::make_unique<Decoration>(*this);
    } else if (!wanted && decoration) {
        decoration.reset();
    } else if (decoration) {
        decoration->update();
    }
}

bool Toplevel::configurable() const
{
    return xdg ? xdg->base->initialized : x11->surface != nullptr;
}

const char* Toplevel::title() const
{
    const char* title = xdg ? xdg->title : x11->title;
    return title ? title : "";
}

const char* Toplevel::appId() const
{
    // X11: la classe della finestra (WM_CLASS), che fa da app id.
    const char* id = xdg ? xdg->app_id : x11->class_;
    return id ? id : "";
}

Toplevel* Toplevel::parent() const
{
    if (xdg) {
        return xdg->parent ? static_cast<Toplevel*>(xdg->parent->base->data) : nullptr;
    }
    return x11->parent ? static_cast<Toplevel*>(x11->parent->data) : nullptr;
}

void Toplevel::configureSize(int width, int height)
{
    if (xdg) {
        // Anche qui: all'app va la parte sotto la barra di Vela.
        wlr_xdg_toplevel_set_size(xdg, width, height > 0 ? std::max(1, height - titleBarHeight()) : 0);
        return;
    }
    // Le dimensioni sono del riquadro intero: all'app va la parte sotto la
    // barra di Vela.
    m_x11Width = width;
    m_x11Height = height > 0 ? std::max(1, height - titleBarHeight()) : 0;
    syncX11Geometry();
}

void Toplevel::sendMaximized(bool on)
{
    if (xdg) {
        wlr_xdg_toplevel_set_maximized(xdg, on);
    } else {
        wlr_xwayland_surface_set_maximized(x11, on, on);
    }
}

void Toplevel::sendFullscreen(bool on)
{
    if (xdg) {
        wlr_xdg_toplevel_set_fullscreen(xdg, on);
    } else {
        wlr_xwayland_surface_set_fullscreen(x11, on);
    }
}

void Toplevel::sendTiled(uint32_t edges)
{
    if (xdg) {
        wlr_xdg_toplevel_set_tiled(xdg, edges);
    }
}

void Toplevel::sendActivated(bool on)
{
    if (xdg) {
        if (xdg->base->initialized) {
            wlr_xdg_toplevel_set_activated(xdg, on);
        }
        return;
    }
    if (x11->surface) {
        wlr_xwayland_surface_activate(x11, on);
        if (on) {
            wlr_xwayland_surface_restack(x11, nullptr, XCB_STACK_MODE_ABOVE);
        }
    }
}

void Toplevel::sendClose()
{
    if (xdg) {
        wlr_xdg_toplevel_send_close(xdg);
    } else {
        wlr_xwayland_surface_close(x11);
    }
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
    const wlr_box geometry = this->geometry();
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
    if (xdg && xdg->base->initial_commit) {
        // Primo commit: diciamo al client cosa sappiamo fare e lasciamo che
        // scelga la sua dimensione (0x0), a meno che non abbia già chiesto
        // di partire massimizzato o a schermo intero.
        wlr_xdg_toplevel_set_wm_capabilities(xdg,
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN
                | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU);
        // La barra la disegna Vela, sempre (§9.1).
        if (xdgDecoration) {
            wlr_xdg_toplevel_decoration_v1_set_mode(xdgDecoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
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
    if (xdg) {
        updateDecoration(); // la barra lato server comincia (o finisce) con questo commit
    } else if (decoration) {
        decoration->update(); // dimensione e scala possono essere cambiate
    }
}

void Toplevel::setXdgDecoration(wlr_xdg_toplevel_decoration_v1* deco)
{
    xdgDecoration = deco;
    decorationMode.connect(&deco->events.request_mode, [this](void*) {
        // Qualunque cosa chieda l'app, la barra la disegna Vela (§9.1).
        if (xdg->base->initialized) {
            wlr_xdg_toplevel_decoration_v1_set_mode(xdgDecoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
    });
    decorationDestroy.connect(&deco->events.destroy, [this](void*) {
        decorationMode.disconnect();
        decorationDestroy.disconnect();
        xdgDecoration = nullptr;
        updateDecoration();
    });
    if (xdg->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
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
    const wlr_box geometry = this->geometry();
    tree->setPosition(place.x - geometry.x, place.y - geometry.y);
}

void Toplevel::onMap()
{
    mapped = true;
    if (xdg) {
        updateDecoration(); // la barra fa parte del riquadro da posizionare
    }

    Output* out = server.outputUnderCursor();
    const wlr_box geometry = this->geometry();
    if (out && (fullscreen || maximized)) {
        const Placement place = out->place(fullscreen ? out->fullArea() : out->usableArea());
        m_targetX = place.x - geometry.x;
        m_targetY = place.y - geometry.y;
    } else if (out) {
        // Nuove finestre al centro dell'area utile, come fa Windows; mai più
        // grandi di lei (alcune app ricordano la dimensione che avevano su
        // un altro schermo o in un'altra sessione).
        const wlr_box& area = out->usable;
        const wlr_box frame = Server::fitInto({ 0, 0, geometry.width, geometry.height }, *out);
        if (frame.width < geometry.width || frame.height < geometry.height) {
            configureSize(frame.width, frame.height);
        }
        m_targetX = area.x + (area.width - frame.width) / 2 - geometry.x;
        m_targetY = area.y + (area.height - frame.height) / 2 - geometry.y;
    }

    server.toplevels.push_front(this);
    server.workspaceMapped(this);
    createHandle();
    server.focusToplevel(this);
    startOpenAnimation();
    server.announceWorkspaces();
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
    server.announceWorkspaces();
}

// ---------------------------------------------------------------- taskbar --

void Toplevel::updateExtHandle()
{
    if (!extHandle) {
        return;
    }
    const wlr_ext_foreign_toplevel_handle_v1_state state {
        .title = title(),
        .app_id = appId(),
    };
    wlr_ext_foreign_toplevel_handle_v1_update_state(extHandle, &state);
}

void Toplevel::createHandle()
{
    const wlr_ext_foreign_toplevel_handle_v1_state state {
        .title = title(),
        .app_id = appId(),
    };
    extHandle = wlr_ext_foreign_toplevel_handle_v1_create(server.extToplevels, &state);
    extHandle->data = this;
    if (onCurrentWorkspace()) {
        createTaskbarHandle();
    }
}

void Toplevel::createTaskbarHandle()
{
    handle = wlr_foreign_toplevel_handle_v1_create(server.foreignToplevels);
    handle->data = this;
    wlr_foreign_toplevel_handle_v1_set_title(handle, title());
    wlr_foreign_toplevel_handle_v1_set_app_id(handle, appId());
    wlr_foreign_toplevel_handle_v1_set_maximized(handle, maximized);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, fullscreen);
    wlr_foreign_toplevel_handle_v1_set_minimized(handle, minimized);
    wlr_foreign_toplevel_handle_v1_set_activated(handle, activated);
    updateHandleParent();

    handleRequests.activate.connect(&handle->events.request_activate, [this](void*) {
        server.focusToplevel(this);
    });
    handleRequests.close.connect(&handle->events.request_close, [this](void*) { sendClose(); });
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
    destroyTaskbarHandle();
}

void Toplevel::destroyTaskbarHandle()
{
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
    taskbarRect = {};
}

// Le finestre di dialogo dichiarano la finestra da cui dipendono: la taskbar
// così sa di non mostrarle come app separate.
void Toplevel::updateHandleParent()
{
    if (!handle) {
        return;
    }
    const Toplevel* owner = parent();
    wlr_foreign_toplevel_handle_v1_set_parent(handle, owner ? owner->handle : nullptr);
}

void Toplevel::setActivated(bool on)
{
    activated = on;
    sendActivated(on);
    if (decoration) {
        decoration->update();
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

// Dove torna la finestra quando smette di essere massimizzata o a schermo
// intero. Se è comparsa già così (molte app ricordano lo stato), non c'è
// una posizione salvata: si usa la dimensione che l'app X11 aveva chiesto,
// altrimenti due terzi dello schermo, al centro.
wlr_box Toplevel::restoreBox() const
{
    if (restore.width > 0 && restore.height > 0) {
        return restore;
    }
    const Output* out = output();
    if (!out) {
        return restore;
    }
    const wlr_box& area = out->usable;
    wlr_box frame { 0, 0, area.width * 2 / 3, area.height * 2 / 3 };
    if (x11 && m_x11Initial.width > 0 && m_x11Initial.height > 0) {
        frame.width = m_x11Initial.width;
        frame.height = m_x11Initial.height + (decoration ? Decoration::height : 0);
    }
    frame = Server::fitInto(frame, *out);
    frame.x = area.x + (area.width - frame.width) / 2;
    frame.y = area.y + (area.height - frame.height) / 2;
    return frame;
}

void Toplevel::setMaximized(bool on)
{
    if (!configurable()) {
        return;
    }
    if (on == maximized || fullscreen) {
        // Il protocollo vuole comunque una risposta alla richiesta.
        if (xdg) {
            wlr_xdg_surface_schedule_configure(xdg->base);
        }
        return;
    }
    finishOpenAnimation();

    if (on) {
        leaveSnapGroup();
        if (mapped && snap == Snap::None) {
            restore = frameBox(); // da agganciata si torna alla dimensione libera
        }
        if (snap != Snap::None) {
            snap = Snap::None;
            sendTiled(WLR_EDGE_NONE);
        }
        maximized = true;
        applyMaximized();
        if (decoration) {
            decoration->update(); // il pulsante diventa "ripristina"
        }
        return;
    }

    maximized = false;
    sendMaximized(false);
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_maximized(handle, false);
    }
    const wlr_box back = restoreBox();
    configureSize(back.width, back.height);
    if (mapped && back.width > 0) {
        const wlr_box geometry = this->geometry();
        tree->setPosition(back.x - geometry.x, back.y - geometry.y);
    }
    if (decoration) {
        decoration->update();
    }
}

void Toplevel::applyMaximized()
{
    Output* out = mapped ? output() : server.outputUnderCursor();
    if (!out || !configurable()) {
        return;
    }
    sendMaximized(true);
    const Placement place = out->place(out->usableArea());
    configureSize(place.width, place.height);
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_maximized(handle, true);
    }
    keepInPlace();
}

void Toplevel::setFullscreen(bool on)
{
    if (!configurable()) {
        return;
    }
    if (on == fullscreen) {
        if (xdg) {
            wlr_xdg_surface_schedule_configure(xdg->base);
        }
        return;
    }
    finishOpenAnimation();
    Output* out = mapped ? output() : server.outputUnderCursor();

    if (on) {
        if (mapped && !maximized && snap == Snap::None) {
            restore = frameBox();
        }
        fullscreen = true;
        sendFullscreen(true);
        if (handle) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, true);
        }
        if (out) {
            const Placement place = out->place(out->fullArea());
            configureSize(place.width, place.height);
        }
        // Sopra taskbar e pannelli.
        tree->reparent(server.layers.fullscreen.get());
        keepInPlace();
        if (decoration) {
            decoration->update(); // a schermo intero la barra sparisce
        }
        return;
    }

    fullscreen = false;
    sendFullscreen(false);
    if (handle) {
        wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, false);
    }
    tree->reparent(server.layers.windows.get());
    if (maximized) {
        applyMaximized();
    } else if (snap != Snap::None) {
        applySnap(output());
    } else {
        const wlr_box back = restoreBox();
        configureSize(back.width, back.height);
        if (mapped && back.width > 0) {
            const wlr_box geometry = this->geometry();
            tree->setPosition(back.x - geometry.x, back.y - geometry.y);
        }
    }
    if (decoration) {
        decoration->update();
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

bool Toplevel::resizable() const
{
    if (xdg) {
        const wlr_xdg_toplevel_state& state = xdg->current;
        const bool fixedWidth = state.max_width > 0 && state.min_width == state.max_width;
        const bool fixedHeight = state.max_height > 0 && state.min_height == state.max_height;
        return !(fixedWidth && fixedHeight);
    }
    if (x11 && x11->size_hints) {
        const xcb_size_hints_t* hints = x11->size_hints;
        const bool hasMin = hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE;
        const bool hasMax = hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
        return !(hasMin && hasMax && hints->min_width == hints->max_width && hints->min_height == hints->max_height);
    }
    return true;
}

pid_t Toplevel::pid() const
{
    if (x11) {
        return x11->pid;
    }
    if (xdg && xdg->resource) {
        pid_t pid = 0;
        wl_client_get_credentials(wl_resource_get_client(xdg->resource), &pid, nullptr, nullptr);
        return pid;
    }
    return 0;
}

void Toplevel::updateShape()
{
    static constexpr double cornerRadius = 8.0; // logici, come Windows 11
    scene::Shape shape;
    bool ownShadow = false;
    if (xdg && xdg->base->surface) {
        const wlr_box g = xdg->base->geometry;
        const int width = xdg->base->surface->current.width;
        const int height = xdg->base->surface->current.height;
        ownShadow = g.x > 0 || g.y > 0 || (g.width > 0 && g.width < width) || (g.height > 0 && g.height < height);
    }
    if (mapped && !minimized && !maximized && !fullscreen && snap == Snap::None && !ownShadow) {
        const wlr_box g = geometry();
        if (g.width > 0 && g.height > 0) {
            shape.enabled = true;
            shape.x = g.x;
            shape.y = g.y;
            shape.width = g.width;
            shape.height = g.height;
            shape.radius = cornerRadius;
            shape.shadow = true;
            shape.active = activated;
        }
    }
    tree->setShape(shape);
}

} // namespace vela
