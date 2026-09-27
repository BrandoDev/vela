// Le app X11 (Steam, molti giochi, app vecchie) attraverso Xwayland.
//
// Xwayland parte solo quando la prima app X11 si collega. Ogni sua finestra
// "gestita" diventa un Toplevel come quelle Wayland: stesse animazioni,
// snap, taskbar e Alt+Tab. Le finestre "override-redirect" (menu, tooltip,
// tendine) si mettono dove dice l'app, sopra tutto, in uno strato loro.
//
// Le app X11 non conoscono la scala frazionaria: disegnano a 1× e il
// compositor le ingrandisce con il filtro di qualità (docs/renderer.md §3.9).

#include "server.hpp"

#include "decoration.hpp"

namespace vela {

namespace {

// Un menu, un tooltip o una tendina X11: nessun gestore di finestre, la
// posizione la sceglie l'app (in coordinate globali).
struct X11Unmanaged {
    X11Unmanaged(Server& server, wlr_xwayland_surface* surface);

    Server& server;
    wlr_xwayland_surface* x11;
    std::unique_ptr<scene::Tree> tree;
    std::unique_ptr<scene::SurfaceNode> surfaceNode;

    Listener associate;
    Listener dissociate;
    Listener map;
    Listener unmap;
    Listener setGeometry;
    Listener requestConfigure;
    Listener destroy;
};

X11Unmanaged::X11Unmanaged(Server& s, wlr_xwayland_surface* surface)
    : server(s)
    , x11(surface)
    , tree(std::make_unique<scene::Tree>(s.layers.x11Popups.get()))
{
    tree->setEnabled(false);
    associate.connect(&x11->events.associate, [this](void*) {
        surfaceNode = std::make_unique<scene::SurfaceNode>(tree.get(), x11->surface);
        map.connect(&x11->surface->events.map, [this](void*) {
            tree->setPosition(x11->x, x11->y);
            tree->raiseToTop();
            tree->setEnabled(true);
            // Alcuni menu X11 vogliono la tastiera (per scorrere le voci).
            if (wlr_xwayland_surface_override_redirect_wants_focus(x11)) {
                server.keyboardEnter(x11->surface);
            }
        });
        unmap.connect(&x11->surface->events.unmap, [this](void*) {
            tree->setEnabled(false);
            if (server.seat->keyboard_state.focused_surface == x11->surface) {
                server.refocus();
            }
        });
    });
    dissociate.connect(&x11->events.dissociate, [this](void*) {
        map.disconnect();
        unmap.disconnect();
        surfaceNode.reset();
    });
    setGeometry.connect(&x11->events.set_geometry, [this](void*) {
        tree->setPosition(x11->x, x11->y);
    });
    requestConfigure.connect(&x11->events.request_configure, [this](void* data) {
        auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);
        wlr_xwayland_surface_configure(x11, event->x, event->y, event->width, event->height);
    });
    destroy.connect(&x11->events.destroy, [this](void*) { delete this; });
}

} // namespace

// --------------------------------------------------------------- Xwayland --

void Server::initXwayland()
{
#if WLR_HAS_XWAYLAND
    // Pigro: il server X parte al primo client, niente costo se non serve.
    xwayland = wlr_xwayland_create(display, compositor, true);
    if (!xwayland) {
        wlr_log(WLR_ERROR, "Xwayland non disponibile: le app solo-X11 non partiranno");
        return;
    }
    xwaylandReady.connect(&xwayland->events.ready, [this](void*) {
        wlr_xwayland_set_seat(xwayland, seat);
        // Il cursore delle finestre X11 finché l'app non ne sceglie uno.
        if (wlr_xcursor_manager_load(cursorManager, 1.0f)) {
            if (wlr_xcursor* xcursor = wlr_xcursor_manager_get_xcursor(cursorManager, "default", 1.0f)) {
                wlr_xcursor_image* image = xcursor->images[0];
                wlr_xwayland_set_cursor(xwayland, wlr_xcursor_image_get_buffer(image), int32_t(image->hotspot_x),
                    int32_t(image->hotspot_y));
            }
        }
        wlr_log(WLR_INFO, "Xwayland pronto su DISPLAY=%s", xwayland->display_name);
    });
    xwaylandNewSurface.connect(&xwayland->events.new_surface, [this](void* data) {
        auto* surface = static_cast<wlr_xwayland_surface*>(data);
        if (surface->override_redirect) {
            new X11Unmanaged(*this, surface);
        } else {
            new Toplevel(*this, surface);
        }
    });
    wlr_log(WLR_INFO, "Xwayland su DISPLAY=%s (parte al primo client X11)", xwayland->display_name);
#endif
}

void Server::syncX11Windows()
{
    if (!xwayland) {
        return;
    }
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->x11) {
            toplevel->syncX11Geometry();
        }
    }
}

// ------------------------------------------------------- Toplevel X11 --

Toplevel::Toplevel(Server& s, wlr_xwayland_surface* surface)
    : SceneOwner(SceneKind::Toplevel)
    , server(s)
    , x11(surface)
    , tree(std::make_unique<scene::Tree>(s.layers.windows.get()))
{
    tree->data = static_cast<SceneOwner*>(this);
    x11->data = this; // per risalire alla finestra genitore

    // La superficie Wayland arriva dopo (associate) e può andarsene prima
    // della finestra X11 (dissociate).
    associate.connect(&x11->events.associate, [this](void*) { connectX11Surface(); });
    dissociate.connect(&x11->events.dissociate, [this](void*) {
        map.disconnect();
        unmap.disconnect();
        commit.disconnect();
        surfaceNode.reset();
    });
    destroy.connect(&x11->events.destroy, [this](void*) { delete this; });

    requestConfigure.connect(&x11->events.request_configure, [this](void* data) {
        auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);
        if (!mapped) {
            // Prima di comparire l'app sceglie dove e quanto (ce lo si
            // ricorda: è la sua dimensione "normale").
            m_x11Initial = { event->x, event->y, event->width, event->height };
            wlr_xwayland_surface_configure(x11, event->x, event->y, event->width, event->height);
            return;
        }
        if (maximized || fullscreen || snap != Snap::None) {
            m_x11Sent = {}; // decidiamo noi: si ripete la nostra geometria
            syncX11Geometry();
            return;
        }
        // Finestra libera: l'app può spostarsi e ridimensionarsi (giochi che
        // cambiano risoluzione, finestre di dialogo che si centrano), ma
        // resta dentro lo schermo dove vuole andare: Steam, per esempio,
        // torna alla posizione e alla dimensione che ricorda, anche se non
        // ci stanno più.
        const int bar = titleBarHeight();
        wlr_box frame { event->x, event->y - bar, event->width, event->height + bar };
        Output* out = server.outputAt(frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
        if (!out) {
            out = output();
        }
        if (out) {
            frame = Server::fitInto(frame, *out);
        }
        tree->setPosition(frame.x, frame.y + bar);
        m_x11Width = frame.width;
        m_x11Height = frame.height - bar;
        syncX11Geometry();
    });
    requestMove.connect(&x11->events.request_move, [this](void*) {
        server.beginInteractive(this, CursorMode::Move, 0);
    });
    requestResize.connect(&x11->events.request_resize, [this](void* data) {
        auto* event = static_cast<wlr_xwayland_resize_event*>(data);
        server.beginInteractive(this, CursorMode::Resize, event->edges);
    });
    requestMaximize.connect(&x11->events.request_maximize, [this](void*) {
        setMaximized(x11->maximized_horz || x11->maximized_vert);
    });
    requestFullscreen.connect(&x11->events.request_fullscreen, [this](void*) { setFullscreen(x11->fullscreen); });
    requestMinimize.connect(&x11->events.request_minimize, [this](void* data) {
        setMinimized(static_cast<wlr_xwayland_minimize_event*>(data)->minimize);
    });
    requestActivate.connect(&x11->events.request_activate, [this](void*) { server.focusToplevel(this); });
    setTitle.connect(&x11->events.set_title, [this](void*) {
        if (handle) {
            wlr_foreign_toplevel_handle_v1_set_title(handle, title());
        }
        updateExtHandle();
        if (decoration) {
            decoration->update();
        }
    });
    setDecorations.connect(&x11->events.set_decorations, [this](void*) {
        if (mapped) {
            updateDecoration();
        }
    });
    setAppId.connect(&x11->events.set_class, [this](void*) {
        if (handle) {
            wlr_foreign_toplevel_handle_v1_set_app_id(handle, appId());
        }
        updateExtHandle();
    });
    setParent.connect(&x11->events.set_parent, [this](void*) { updateHandleParent(); });
}

void Toplevel::connectX11Surface()
{
    wlr_surface* surface = x11->surface;
    surfaceNode = std::make_unique<scene::SurfaceNode>(tree.get(), surface);
    map.connect(&surface->events.map, [this](void*) {
        updateDecoration();
        // Le app X11 possono chiedere di partire massimizzate o a schermo
        // intero (giochi).
        if (x11->fullscreen) {
            setFullscreen(true);
        } else if (x11->maximized_horz || x11->maximized_vert) {
            setMaximized(true);
        }
        onMap();
    });
    unmap.connect(&surface->events.unmap, [this](void*) { onUnmap(); });
    commit.connect(&surface->events.commit, [this](void*) { onCommit(); });
}

void Toplevel::syncX11Geometry()
{
    if (!x11 || !x11->surface) {
        return;
    }
    const wlr_box wanted {
        .x = int(std::lround(tree->x())),
        .y = int(std::lround(tree->y())),
        .width = m_x11Width > 0 ? m_x11Width : x11->width,
        .height = m_x11Height > 0 ? m_x11Height : x11->height,
    };
    if (wanted.width <= 0 || wanted.height <= 0
        || (wanted.x == m_x11Sent.x && wanted.y == m_x11Sent.y && wanted.width == m_x11Sent.width
            && wanted.height == m_x11Sent.height)) {
        return;
    }
    wlr_xwayland_surface_configure(x11, int16_t(wanted.x), int16_t(wanted.y), uint16_t(wanted.width),
        uint16_t(wanted.height));
    m_x11Sent = wanted;
}

} // namespace vela
