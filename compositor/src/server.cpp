#include "server.hpp"

#include <algorithm>
#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

// glibc (almeno fino alla 2.44) non racchiude questo header in extern "C".
extern "C" {
#include <sys/pidfd.h>
}

namespace vela {

namespace {

int envInt(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return fallback;
    }
    return std::atoi(value);
}

int handleTerminate(int /*signal*/, void* data)
{
    wl_display_terminate(static_cast<wl_display*>(data));
    return 0;
}

// Eseguito nel processo figlio: non ritorna mai.
[[noreturn]] void execCommand(const std::string& command)
{
    setsid();
    // wlroots blocca alcuni segnali per gestirli nel suo loop: i processi
    // lanciati da noi devono ripartire con una maschera pulita.
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, nullptr);
    execl("/bin/sh", "/bin/sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
}

double secondsSince(const timespec& start)
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec - start.tv_sec)
        + static_cast<double>(now.tv_nsec - start.tv_nsec) / 1e9;
}

// Touchpad configurati come su Windows: tocco per cliccare, trascinamento
// col tocco, niente tocchi accidentali mentre si scrive, scorrimento
// "naturale". Mouse e trackpoint restano come sono.
void configurePointer(wlr_input_device* device)
{
#if WLR_HAS_LIBINPUT_BACKEND
    if (!wlr_input_device_is_libinput(device)) {
        return; // es. backend annidato: il puntatore è del sistema ospite
    }
    libinput_device* handle = wlr_libinput_get_device_handle(device);
    if (libinput_device_config_tap_get_finger_count(handle) == 0) {
        return;
    }
    libinput_device_config_tap_set_enabled(handle, LIBINPUT_CONFIG_TAP_ENABLED);
    libinput_device_config_tap_set_drag_enabled(handle, LIBINPUT_CONFIG_DRAG_ENABLED);
    if (libinput_device_config_dwt_is_available(handle)) {
        libinput_device_config_dwt_set_enabled(handle, LIBINPUT_CONFIG_DWT_ENABLED);
    }
    if (libinput_device_config_scroll_has_natural_scroll(handle)) {
        libinput_device_config_scroll_set_natural_scroll_enabled(handle,
            envInt("VELA_NATURAL_SCROLL", 1) != 0);
    }
    wlr_log(WLR_INFO, "Touchpad configurato: %s", libinput_device_get_name(handle));
#endif
}

} // namespace

Listener& Server::on(wl_signal* signal, Listener::Callback callback)
{
    Listener& listener = m_listeners.emplace_back();
    listener.connect(signal, std::move(callback));
    return listener;
}

// ------------------------------------------------------------------ init --

bool Server::init()
{
    display = wl_display_create();
    loop = wl_display_get_event_loop(display);

    // Sceglie da solo il backend: DRM/KMS da una TTY, oppure una finestra
    // Wayland se lanciato dentro un'altra sessione (es. KDE) per i test.
    backend = wlr_backend_autocreate(loop, nullptr);
    if (!backend) {
        wlr_log(WLR_ERROR, "Impossibile creare il backend");
        return false;
    }
    wlr_multi_for_each_backend(backend,
        [](wlr_backend* child, void* data) {
            bool windowed = wlr_backend_is_wl(child);
#if WLR_HAS_X11_BACKEND
            windowed = windowed || wlr_backend_is_x11(child);
#endif
            if (windowed) {
                *static_cast<bool*>(data) = true;
            }
        },
        &nested);

    renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        wlr_log(WLR_ERROR, "Impossibile creare il renderer");
        return false;
    }
    wlr_renderer_init_wl_shm(renderer, display);

    // Buffer GPU condivisi con i client (niente copie in RAM) e, dove
    // possibile, "direct scanout" delle finestre a schermo intero.
    if (wlr_renderer_get_drm_fd(renderer) >= 0
        && wlr_renderer_get_texture_formats(renderer, WLR_BUFFER_CAP_DMABUF) != nullptr) {
        dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(display, 4, renderer);
    }

    allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator) {
        wlr_log(WLR_ERROR, "Impossibile creare l'allocator");
        return false;
    }

    // Protocolli di base che quasi ogni applicazione moderna si aspetta.
    compositor = wlr_compositor_create(display, 6, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);
    wlr_primary_selection_v1_device_manager_create(display);
    wlr_data_control_manager_v1_create(display);
    wlr_viewporter_create(display);
    wlr_single_pixel_buffer_manager_v1_create(display);
    wlr_fractional_scale_manager_v1_create(display, 1);
    wlr_screencopy_manager_v1_create(display);
    wlr_presentation_create(display, backend, 2);

    outputLayout = wlr_output_layout_create(display);
    wlr_xdg_output_manager_v1_create(display, outputLayout);

    scene = wlr_scene_create();
    sceneLayout = wlr_scene_attach_output_layout(scene, outputLayout);
    if (dmabuf) {
        wlr_scene_set_linux_dmabuf_v1(scene, dmabuf);
    }

    // L'ordine di creazione è l'ordine di impilamento (dal basso).
    layers.background = wlr_scene_tree_create(&scene->tree);
    layers.bottom = wlr_scene_tree_create(&scene->tree);
    layers.windows = wlr_scene_tree_create(&scene->tree);
    layers.top = wlr_scene_tree_create(&scene->tree);
    layers.fullscreen = wlr_scene_tree_create(&scene->tree);
    layers.overlay = wlr_scene_tree_create(&scene->tree);

    on(&backend->events.new_output, [this](void* data) {
        new Output(*this, static_cast<wlr_output*>(data));
    });

    xdgShell = wlr_xdg_shell_create(display, 5);
    on(&xdgShell->events.new_toplevel, [this](void* data) {
        new Toplevel(*this, static_cast<wlr_xdg_toplevel*>(data));
    });

    // Elenco delle finestre per la taskbar, con attiva/riduci/chiudi.
    foreignToplevels = wlr_foreign_toplevel_manager_v1_create(display);

    layerShell = wlr_layer_shell_v1_create(display, 4);
    on(&layerShell->events.new_surface, [this](void* data) {
        LayerSurface::create(*this, static_cast<wlr_layer_surface_v1*>(data));
    });

    // --- puntatore ---
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, outputLayout);
    cursorManager = wlr_xcursor_manager_create(std::getenv("XCURSOR_THEME"),
        static_cast<uint32_t>(envInt("XCURSOR_SIZE", 24)));

    on(&cursor->events.motion, [this](void* data) {
        auto* event = static_cast<wlr_pointer_motion_event*>(data);
        wlr_cursor_move(cursor, &event->pointer->base, event->delta_x, event->delta_y);
        onCursorMotion(event->time_msec);
    });
    on(&cursor->events.motion_absolute, [this](void* data) {
        auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
        wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
        onCursorMotion(event->time_msec);
    });
    on(&cursor->events.button, [this](void* data) {
        onCursorButton(static_cast<wlr_pointer_button_event*>(data));
    });
    on(&cursor->events.axis, [this](void* data) {
        auto* event = static_cast<wlr_pointer_axis_event*>(data);
        wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation,
            event->delta, event->delta_discrete, event->source,
            event->relative_direction);
    });
    on(&cursor->events.frame, [this](void*) {
        wlr_seat_pointer_notify_frame(seat);
    });

    // --- dispositivi di input e seat ---
    on(&backend->events.new_input, [this](void* data) {
        onNewInput(static_cast<wlr_input_device*>(data));
    });

    seat = wlr_seat_create(display, "seat0");
    on(&seat->events.request_set_cursor, [this](void* data) {
        auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
        if (seat->pointer_state.focused_client == event->seat_client) {
            wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
        }
    });
    on(&seat->events.request_set_selection, [this](void* data) {
        auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);
        wlr_seat_set_selection(seat, event->source, event->serial);
    });
    on(&seat->events.request_set_primary_selection, [this](void* data) {
        auto* event = static_cast<wlr_seat_request_set_primary_selection_event*>(data);
        wlr_seat_set_primary_selection(seat, event->source, event->serial);
    });

    // Le app Qt/GTK recenti chiedono la forma del cursore per nome invece
    // di disegnarlo da sole: più veloce e coerente col tema.
    wlr_cursor_shape_manager_v1* cursorShape = wlr_cursor_shape_manager_v1_create(display, 1);
    on(&cursorShape->events.request_set_shape, [this](void* data) {
        auto* event = static_cast<wlr_cursor_shape_manager_v1_request_set_shape_event*>(data);
        if (event->seat_client == seat->pointer_state.focused_client) {
            wlr_cursor_set_xcursor(cursor, cursorManager, wlr_cursor_shape_v1_name(event->shape));
        }
    });

    wl_event_loop_add_signal(loop, SIGINT, handleTerminate, display);
    wl_event_loop_add_signal(loop, SIGTERM, handleTerminate, display);

    return true;
}

bool Server::start(const std::string& startupCommand)
{
    const char* socket = wl_display_add_socket_auto(display);
    if (!socket) {
        wlr_log(WLR_ERROR, "Impossibile creare il socket Wayland");
        return false;
    }
    socketName = socket;

    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "Impossibile avviare il backend");
        return false;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    // Niente Xwayland per ora: senza questa riga un'app solo-X11 lanciata da
    // qui si aprirebbe nella sessione "ospite" (KDE) creando confusione.
    unsetenv("DISPLAY");
    // Plasma la esporta perché le app sopravvivano a un crash di KWin. Qui
    // non serve (Vela non si riavvia da solo) e fa danni: alla chiusura di
    // Vela le app Qt provano a riconnettersi e vanno in crash dentro Qt.
    unsetenv("QT_WAYLAND_RECONNECT");

    wlr_log(WLR_INFO, "Vela in esecuzione su WAYLAND_DISPLAY=%s", socket);
    if (nested) {
        wlr_log(WLR_INFO, "Modalità annidata: scorciatoie Alt attive");
    }
    if (!startupCommand.empty()) {
        supervise(startupCommand);
    }
    return true;
}

void Server::run()
{
    wl_display_run(display);
}

void Server::shutdown()
{
    // Stiamo chiudendo noi: la shell che se ne va non va rilanciata.
    stopSupervising();

    // Chiudere i client distrugge finestre e superfici della shell, che si
    // rimuovono da sole dalle nostre liste.
    wl_display_destroy_clients(display);

    // wlroots controlla che nessun listener resti attaccato agli oggetti che
    // distrugge: scolleghiamo tutti quelli globali prima di procedere.
    m_listeners.clear();
    m_snapshotAnimations.clear(); // le istantanee vivono nella scena

    wlr_scene_node_destroy(&scene->tree.node);
    wlr_xcursor_manager_destroy(cursorManager);
    wlr_cursor_destroy(cursor);
    wlr_allocator_destroy(allocator);
    wlr_renderer_destroy(renderer);
    wlr_backend_destroy(backend); // distrugge schermi e tastiere
    wl_display_destroy(display);
}

// ----------------------------------------------------------------- focus --

Toplevel* Server::focusedToplevel() const
{
    wlr_surface* focused = seat->keyboard_state.focused_surface;
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->xdg->base->surface == focused) {
            return toplevel;
        }
    }
    return nullptr;
}

void Server::keyboardEnter(wlr_surface* surface)
{
    if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat)) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
            keyboard->num_keycodes, &keyboard->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(seat, surface, nullptr, 0, nullptr);
    }
}

void Server::focusToplevel(Toplevel* toplevel)
{
    if (!toplevel || !toplevel->mapped) {
        return;
    }
    if (toplevel->minimized) {
        toplevel->setMinimized(false); // la riaccende e torna qui
        return;
    }

    // Porta in primo piano e in testa alla lista MRU in ogni caso.
    wlr_scene_node_raise_to_top(&toplevel->tree->node);
    toplevels.remove(toplevel);
    toplevels.push_front(toplevel);

    // Un pannello che ha chiesto la tastiera in modo esclusivo (es. schermata
    // di blocco) non la cede a una finestra.
    if (focusedLayerSurface
        && focusedLayerSurface->wlr->current.keyboard_interactive
            == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
        return;
    }
    focusedLayerSurface = nullptr;

    wlr_surface* surface = toplevel->xdg->base->surface;
    if (seat->keyboard_state.focused_surface == surface) {
        return;
    }
    // Cerchiamo la finestra attiva tra le NOSTRE finestre vive: una appena
    // chiusa è già stata tolta dalla lista, e non le mandiamo nulla.
    Toplevel* previous = focusedToplevel();
    if (previous && previous != toplevel) {
        previous->setActivated(false);
    }
    toplevel->setActivated(true);
    keyboardEnter(surface);
}

void Server::focusLayer(LayerSurface* layer)
{
    // Si usa lo stato della superficie: l'evento "map" arriva prima del
    // commit in cui aggiorniamo layer->mapped.
    if (!layer || !layer->wlr->surface->mapped) {
        return;
    }
    // Come su Windows: aprendo il menu Start la finestra attiva si "spegne".
    if (Toplevel* active = focusedToplevel()) {
        active->setActivated(false);
    }
    focusedLayerSurface = layer;
    keyboardEnter(layer->wlr->surface);
}

void Server::refocus()
{
    focusedLayerSurface = nullptr;
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->mapped && !toplevel->minimized) {
            focusToplevel(toplevel);
            return;
        }
    }
    wlr_seat_keyboard_notify_clear_focus(seat);
}

void Server::forget(Toplevel* toplevel)
{
    const bool wasFocused = focusedToplevel() == toplevel;
    toplevels.remove(toplevel);
    std::erase(m_animating, toplevel);
    cancelSnapshotAnimations(toplevel);
    if (grabbed == toplevel) {
        grabbed = nullptr;
        cursorMode = CursorMode::Passthrough;
    }
    if (wasFocused && !focusedLayerSurface) {
        refocus();
    }
}

void Server::forget(LayerSurface* layer)
{
    layerSurfaces.remove(layer);
    if (focusedLayerSurface == layer) {
        refocus();
    }
}

// ----------------------------------------------------------------- scene --

Output* Server::outputAt(double lx, double ly) const
{
    wlr_output* output = wlr_output_layout_output_at(outputLayout, lx, ly);
    return output ? static_cast<Output*>(output->data) : nullptr;
}

Output* Server::outputUnderCursor() const
{
    if (Output* output = outputAt(cursor->x, cursor->y)) {
        return output;
    }
    return outputs.empty() ? nullptr : outputs.front();
}

wlr_scene_tree* Server::layerTree(zwlr_layer_shell_v1_layer layer) const
{
    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return layers.background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return layers.bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return layers.top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return layers.overlay;
    }
    return layers.top;
}

// ------------------------------------------------------------ animazioni --

void Server::addAnimation(Toplevel* toplevel)
{
    if (std::find(m_animating.begin(), m_animating.end(), toplevel) == m_animating.end()) {
        m_animating.push_back(toplevel);
    }
    scheduleFrames();
}

void Server::tickAnimations(const timespec& now)
{
    if (m_animating.empty() && m_snapshotAnimations.empty()) {
        return;
    }
    const double nowMs = static_cast<double>(now.tv_sec) * 1000.0
        + static_cast<double>(now.tv_nsec) / 1'000'000.0;

    const auto running = m_animating; // la lista può cambiare durante il giro
    for (Toplevel* toplevel : running) {
        if (!toplevel->tickOpen(nowMs)) {
            std::erase(m_animating, toplevel);
        }
    }
    tickSnapshotAnimations(nowMs);
    if (!m_animating.empty() || !m_snapshotAnimations.empty()) {
        scheduleFrames();
    }
}

void Server::scheduleFrames()
{
    for (Output* output : outputs) {
        wlr_output_schedule_frame(output->wlr);
    }
}

// ------------------------------------------------------------- puntatore --

namespace {

struct Hit {
    SceneOwner* owner = nullptr;
    wlr_surface* surface = nullptr;
    double sx = 0.0;
    double sy = 0.0;
};

Hit hitTest(wlr_scene* scene, double lx, double ly)
{
    Hit hit;
    wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, lx, ly, &hit.sx, &hit.sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return hit;
    }
    wlr_scene_surface* sceneSurface = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
    if (!sceneSurface) {
        return hit;
    }
    hit.surface = sceneSurface->surface;
    for (wlr_scene_tree* tree = node->parent; tree; tree = tree->node.parent) {
        if (tree->node.data) {
            hit.owner = static_cast<SceneOwner*>(tree->node.data);
            break;
        }
    }
    return hit;
}

} // namespace

void Server::onNewInput(wlr_input_device* device)
{
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        new Keyboard(*this, wlr_keyboard_from_input_device(device));
        break;
    case WLR_INPUT_DEVICE_POINTER:
        configurePointer(device);
        wlr_cursor_attach_input_device(cursor, device);
        break;
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!keyboards.empty()) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(seat, caps);
}

void Server::onCursorMotion(uint32_t timeMsec)
{
    if (cursorMode == CursorMode::Move && grabbed) {
        wlr_scene_node_set_position(&grabbed->tree->node,
            static_cast<int>(std::lround(cursor->x - grabX)),
            static_cast<int>(std::lround(cursor->y - grabY)));
        return;
    }

    if (cursorMode == CursorMode::Resize && grabbed) {
        const double borderX = cursor->x - grabX;
        const double borderY = cursor->y - grabY;
        int left = grabBox.x;
        int right = grabBox.x + grabBox.width;
        int top = grabBox.y;
        int bottom = grabBox.y + grabBox.height;

        if (resizeEdges & WLR_EDGE_TOP) {
            top = std::min(static_cast<int>(borderY), bottom - 1);
        } else if (resizeEdges & WLR_EDGE_BOTTOM) {
            bottom = std::max(static_cast<int>(borderY), top + 1);
        }
        if (resizeEdges & WLR_EDGE_LEFT) {
            left = std::min(static_cast<int>(borderX), right - 1);
        } else if (resizeEdges & WLR_EDGE_RIGHT) {
            right = std::max(static_cast<int>(borderX), left + 1);
        }

        const wlr_box& geometry = grabbed->xdg->base->geometry;
        wlr_scene_node_set_position(&grabbed->tree->node, left - geometry.x, top - geometry.y);
        wlr_xdg_toplevel_set_size(grabbed->xdg, right - left, bottom - top);
        return;
    }

    const Hit hit = hitTest(scene, cursor->x, cursor->y);
    if (!hit.surface) {
        wlr_cursor_set_xcursor(cursor, cursorManager, "default");
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    wlr_seat_pointer_notify_enter(seat, hit.surface, hit.sx, hit.sy);
    wlr_seat_pointer_notify_motion(seat, timeMsec, hit.sx, hit.sy);
}

void Server::onCursorButton(wlr_pointer_button_event* event)
{
    superTap = false;
    wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (cursorMode != CursorMode::Passthrough) {
            cursorMode = CursorMode::Passthrough;
            grabbed = nullptr;
            onCursorMotion(event->time_msec);
        }
        return;
    }

    const Hit hit = hitTest(scene, cursor->x, cursor->y);
    if (!hit.owner) {
        return;
    }
    if (hit.owner->kind == SceneKind::Toplevel) {
        focusToplevel(static_cast<Toplevel*>(hit.owner));
    } else {
        auto* layer = static_cast<LayerSurface*>(hit.owner);
        if (layer->wantsKeyboard()) {
            focusLayer(layer);
        }
    }
}

void Server::beginInteractive(Toplevel* toplevel, CursorMode mode, uint32_t edges)
{
    // Accetta la richiesta solo dalla finestra su cui si trova il puntatore.
    wlr_surface* focused = seat->pointer_state.focused_surface;
    if (!focused || wlr_surface_get_root_surface(focused) != toplevel->xdg->base->surface) {
        return;
    }
    if (toplevel->fullscreen) {
        return;
    }
    toplevel->finishOpenAnimation();

    // Trascinare una finestra massimizzata la ripristina sotto il cursore,
    // mantenendo il punto afferrato alla stessa proporzione (come Windows).
    if (toplevel->maximized && mode == CursorMode::Move) {
        const wlr_box frame = toplevel->frameBox();
        const double fraction = frame.width > 0 ? (cursor->x - frame.x) / frame.width : 0.5;
        const int restoredWidth = toplevel->restore.width > 0 ? toplevel->restore.width : frame.width;
        toplevel->setMaximized(false);
        const wlr_box& geometry = toplevel->xdg->base->geometry;
        wlr_scene_node_set_position(&toplevel->tree->node,
            static_cast<int>(cursor->x - fraction * restoredWidth) - geometry.x,
            toplevel->tree->node.y);
    }

    grabbed = toplevel;
    cursorMode = mode;

    if (mode == CursorMode::Move) {
        grabX = cursor->x - toplevel->tree->node.x;
        grabY = cursor->y - toplevel->tree->node.y;
        return;
    }

    const wlr_box frame = toplevel->frameBox();
    const double borderX = frame.x + ((edges & WLR_EDGE_RIGHT) ? frame.width : 0);
    const double borderY = frame.y + ((edges & WLR_EDGE_BOTTOM) ? frame.height : 0);
    grabX = cursor->x - borderX;
    grabY = cursor->y - borderY;
    grabBox = frame;
    resizeEdges = edges;
}

// --------------------------------------------------------------- comandi --

bool Server::handleBinding(uint32_t modifiers, xkb_keysym_t sym)
{
    const bool alt = modifiers & WLR_MODIFIER_ALT;
    const bool super = modifiers & WLR_MODIFIER_LOGO;
    const bool shift = modifiers & WLR_MODIFIER_SHIFT;

    // Le varianti con Alt+lettera esistono perché, quando Vela gira in una
    // finestra dentro KDE, KDE si tiene per sé il tasto Super. Nella sessione
    // vera restano alle app, che le usano per aprire i propri menu.
    const bool altNested = alt && nested;

    if ((alt && shift && sym == XKB_KEY_Escape)) {
        wl_display_terminate(display);
        return true;
    }
    if ((altNested || super) && sym == XKB_KEY_Return) {
        const char* terminal = std::getenv("VELA_TERMINAL");
        spawn(terminal ? terminal : "konsole || foot || kitty || alacritty || xterm");
        return true;
    }
    if ((alt && sym == XKB_KEY_F4) || (altNested && sym == XKB_KEY_q)) {
        if (Toplevel* active = focusedToplevel()) {
            wlr_xdg_toplevel_send_close(active->xdg);
        }
        return true;
    }
    if ((alt && sym == XKB_KEY_Tab) || (altNested && sym == XKB_KEY_j)) {
        // Passa alla finestra usata prima di quella attuale; se non ce n'è
        // una attiva (es. tutte ridotte a icona), riprende la più recente.
        // Le finestre ridotte a icona si ripristinano, come su Windows.
        if (!focusedToplevel()) {
            focusToplevel(toplevels.empty() ? nullptr : toplevels.front());
        } else if (toplevels.size() >= 2) {
            focusToplevel(*std::next(toplevels.begin()));
        }
        return true;
    }
    if ((super && sym == XKB_KEY_Up) || (altNested && sym == XKB_KEY_m)) {
        if (Toplevel* active = focusedToplevel()) {
            active->setMaximized(sym == XKB_KEY_Up ? true : !active->maximized);
        }
        return true;
    }
    if (super && sym == XKB_KEY_Down) {
        if (Toplevel* active = focusedToplevel()) {
            active->setMaximized(false);
        }
        return true;
    }
    if (altNested && sym == XKB_KEY_s) {
        sendShellCommand("toggle-start");
        return true;
    }
    return false;
}

void Server::spawn(const std::string& command)
{
    // Doppio fork: il figlio viene adottato da init e non resta zombie.
    const pid_t child = fork();
    if (child == 0) {
        if (fork() == 0) {
            execCommand(command);
        }
        _exit(0);
    }
    if (child > 0) {
        waitpid(child, nullptr, 0);
    }
}

void Server::supervise(const std::string& command)
{
    // Fork singolo, così il processo resta nostro figlio e un pidfd nel loop
    // di Wayland ci avvisa quando termina.
    m_supervised.command = command;
    const pid_t pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "Impossibile avviare \"%s\"", command.c_str());
        return;
    }
    if (pid == 0) {
        execCommand(command);
    }

    const int pidfd = pidfd_open(pid, 0);
    if (pidfd < 0) {
        wlr_log_errno(WLR_ERROR, "pidfd_open: \"%s\" non verrà riavviato", command.c_str());
        return;
    }
    m_supervised.pid = pid;
    m_supervised.pidfd = pidfd;
    clock_gettime(CLOCK_MONOTONIC, &m_supervised.startedAt);
    m_supervised.source = wl_event_loop_add_fd(loop, pidfd, WL_EVENT_READABLE,
        [](int, uint32_t, void* data) {
            static_cast<Server*>(data)->onSupervisedExit();
            return 0;
        },
        this);
}

void Server::onSupervisedExit()
{
    int status = 0;
    waitpid(m_supervised.pid, &status, 0);
    const std::string command = m_supervised.command;
    const double uptime = secondsSince(m_supervised.startedAt);
    stopSupervising();

    // Uscita pulita (es. "già in esecuzione"): era voluta.
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        wlr_log(WLR_INFO, "\"%s\" è terminato", command.c_str());
        return;
    }
    const std::string reason = WIFSIGNALED(status)
        ? std::string("segnale ") + strsignal(WTERMSIG(status))
        : "codice " + std::to_string(WEXITSTATUS(status));

    // Se si chiude di continuo appena partito, riavviarlo non serve a nulla.
    m_supervised.quickCrashes = uptime < 5.0 ? m_supervised.quickCrashes + 1 : 0;
    if (m_supervised.quickCrashes >= 3) {
        wlr_log(WLR_ERROR, "\"%s\" continua a chiudersi (%s): non lo riavvio",
            command.c_str(), reason.c_str());
        return;
    }
    wlr_log(WLR_ERROR, "\"%s\" si è chiuso (%s): lo riavvio", command.c_str(), reason.c_str());
    supervise(command);
}

void Server::stopSupervising()
{
    if (m_supervised.source) {
        wl_event_source_remove(m_supervised.source);
        m_supervised.source = nullptr;
    }
    if (m_supervised.pidfd >= 0) {
        close(m_supervised.pidfd);
        m_supervised.pidfd = -1;
    }
    m_supervised.pid = -1;
}

void Server::sendShellCommand(const std::string& command)
{
    // La shell ascolta su un socket Unix legato a questa sessione Wayland.
    // Non blocca mai: se la shell non c'è, il comando va perso e basta.
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir || socketName.empty()) {
        return;
    }
    const std::string path = std::string(runtimeDir) + "/vela-shell-" + socketName + ".sock";

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return;
    }
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
        const std::string line = command + "\n";
        send(fd, line.data(), line.size(), MSG_NOSIGNAL);
    } else {
        wlr_log(WLR_DEBUG, "Shell non raggiungibile su %s", path.c_str());
    }
    close(fd);
}

} // namespace vela
