#include "server.hpp"

#include "render/allocator.hpp"

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

    // Il renderer di Vela (docs/renderer.md): solo Vulkan 1.4, sul device
    // della GPU che pilota gli schermi. Senza, Vela non parte e il log dice
    // perché.
    vulkan = render::VulkanDevice::create(wlr_backend_get_drm_fd(backend));
    if (vulkan) {
        velaRenderer = render::Renderer::create(*vulkan);
    }
    if (!velaRenderer) {
        wlr_log(WLR_ERROR, "Vela ha bisogno di una GPU con Vulkan 1.4 e il supporto ai dmabuf: "
                           "non posso disegnare");
        return false;
    }
    renderer = velaRenderer->wlr();
    allocator = render::createGbmAllocator(vulkan->renderFd);
    if (!allocator) {
        wlr_log(WLR_ERROR, "Impossibile creare l'allocatore dei buffer (GBM)");
        return false;
    }
    wlr_renderer_init_wl_shm(renderer, display);
    // Buffer GPU condivisi con le app, senza copie: i formati sono quelli
    // che il nostro device sa leggere.
    dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(display, 4, renderer);

    // Protocolli di base che quasi ogni applicazione moderna si aspetta.
    // Con il renderer, wlroots carica i buffer delle app nelle nostre
    // texture a ogni commit (solo la parte cambiata).
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

    sceneGraph = std::make_unique<scene::Scene>();
    sceneGraph->watch(compositor);

    // L'ordine di creazione è l'ordine di impilamento (dal basso).
    scene::Tree* root = &sceneGraph->root();
    layers.background = std::make_unique<scene::Tree>(root);
    layers.bottom = std::make_unique<scene::Tree>(root);
    layers.windows = std::make_unique<scene::Tree>(root);
    layers.top = std::make_unique<scene::Tree>(root);
    layers.fullscreen = std::make_unique<scene::Tree>(root);
    layers.overlay = std::make_unique<scene::Tree>(root);

    on(&backend->events.new_output, [this](void* data) {
        new Output(*this, static_cast<wlr_output*>(data));
    });

    xdgShell = wlr_xdg_shell_create(display, 5);
    on(&xdgShell->events.new_toplevel, [this](void* data) {
        new Toplevel(*this, static_cast<wlr_xdg_toplevel*>(data));
    });

    // Elenco delle finestre per la taskbar, con attiva/riduci/chiudi.
    foreignToplevels = wlr_foreign_toplevel_manager_v1_create(display);

    // Cattura di schermi e finestre (anteprime di Alt+Tab, e in futuro la
    // condivisione dello schermo): i protocolli standard ext-*.
    extToplevels = wlr_ext_foreign_toplevel_list_v1_create(display, 1);
    wlr_ext_image_copy_capture_manager_v1_create(display, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(display, 1);
    auto* windowCapture = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(display, 1);
    on(&windowCapture->events.new_request, [this](void* data) {
        auto* request = static_cast<wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request*>(data);
        auto* toplevel = static_cast<Toplevel*>(request->toplevel_handle->data);
        if (!toplevel) {
            return;
        }
        if (wlr_ext_image_capture_source_v1* source = toplevel->prepareCapture()) {
            wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, source);
        }
    });

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
        double x = event->x;
        double y = event->y;
        // Annidati: il backend divide per i pixel del buffer, che con un
        // ospite a scala frazionaria sono più delle unità della finestra.
        if (wlr_input_device_is_wl(&event->pointer->base)) {
            if (Output* out = outputNamed(event->pointer->output_name); out && out->nested) {
                x *= out->nested->pointerScaleX();
                y *= out->nested->pointerScaleY();
            }
        }
        wlr_cursor_warp_absolute(cursor, &event->pointer->base, x, y);
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

    // Mouse e tastiera virtuali, per i test automatici (tools/vela-input).
    // Spenti di default: permettono a qualunque programma di simulare input.
    if (envInt("VELA_DEBUG_INPUT", 0) != 0) {
        wlr_log(WLR_INFO, "VELA_DEBUG_INPUT: mouse e tastiera virtuali attivi");
        auto* pointers = wlr_virtual_pointer_manager_v1_create(display);
        on(&pointers->events.new_virtual_pointer, [this](void* data) {
            auto* event = static_cast<wlr_virtual_pointer_v1_new_pointer_event*>(data);
            onNewInput(&event->new_pointer->pointer.base);
        });
        auto* keyboards = wlr_virtual_keyboard_manager_v1_create(display);
        on(&keyboards->events.new_virtual_keyboard, [this](void* data) {
            onNewInput(&static_cast<wlr_virtual_keyboard_v1*>(data)->keyboard.base);
        });
    }

    // Per il debug del danno: `kill -USR1` fa ridisegnare tutto da capo. Se
    // l'immagine cambia, il danno aveva lasciato pixel vecchi.
    wl_event_loop_add_signal(loop, SIGUSR1,
        [](int, void* data) {
            for (Output* output : static_cast<Server*>(data)->outputs) {
                output->sceneFrame->resetDamage();
            }
            return 0;
        },
        this);
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
    m_snapshotAnimations.clear();
    m_snapPreview.rect.reset();

    wlr_xcursor_manager_destroy(cursorManager);
    wlr_cursor_destroy(cursor);
    wlr_backend_destroy(backend); // distrugge schermi e tastiere
    // Dopo gli schermi, che la usano per disegnare.
    layers = {};
    sceneGraph.reset();
    // Il renderer avvisa chi lo usa (wlr_compositor) e si distrugge.
    wlr_renderer_destroy(renderer);
    wlr_allocator_destroy(allocator);
    vulkan.reset(); // per ultimo: tutto il resto ne usa il device
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
    toplevel->tree->raiseToTop();
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
    // Una finestra che sparisce durante Alt+Tab: si chiude il selettore.
    if (std::find(m_switcher.windows.begin(), m_switcher.windows.end(), toplevel) != m_switcher.windows.end()) {
        switcherFinish(false);
    }
    if (grabbed == toplevel) {
        endSnapZone(false);
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

Output* Server::outputNamed(const char* name) const
{
    for (Output* output : outputs) {
        if (name && std::strcmp(output->wlr->name, name) == 0) {
            return output;
        }
    }
    return nullptr;
}

Output* Server::outputUnderCursor() const
{
    if (Output* output = outputAt(cursor->x, cursor->y)) {
        return output;
    }
    return outputs.empty() ? nullptr : outputs.front();
}

scene::Tree* Server::layerTree(zwlr_layer_shell_v1_layer layer) const
{
    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return layers.background.get();
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return layers.bottom.get();
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return layers.top.get();
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return layers.overlay.get();
    }
    return layers.top.get();
}

// ------------------------------------------------------------ animazioni --

void Server::addAnimation(Toplevel* toplevel)
{
    if (std::find(m_animating.begin(), m_animating.end(), toplevel) == m_animating.end()) {
        m_animating.push_back(toplevel);
    }
    scheduleFrames();
}

void Server::tickAnimations(int64_t presentNs)
{
    m_animationNowMs = std::max(m_animationNowMs, double(presentNs) / 1e6);
    if (m_animating.empty() && m_snapshotAnimations.empty() && !m_snapPreview.rect) {
        return;
    }
    const double nowMs = m_animationNowMs;

    const auto running = m_animating; // la lista può cambiare durante il giro
    for (Toplevel* toplevel : running) {
        if (!toplevel->tickOpen(nowMs)) {
            std::erase(m_animating, toplevel);
        }
    }
    tickSnapshotAnimations(nowMs);
    tickSnapPreview(nowMs);
    if (!m_animating.empty() || !m_snapshotAnimations.empty()
        || (m_snapPreview.rect && !m_snapPreview.tween.finished(nowMs))) {
        scheduleFrames();
    }
}

void Server::scheduleFrames()
{
    for (Output* output : outputs) {
        output->scheduleFrame();
    }
}

// ------------------------------------------------------------- puntatore --

namespace {

// Cosa c'è sotto il cursore: la superficie e chi la possiede.
struct Hit {
    SceneOwner* owner = nullptr;
    wlr_surface* surface = nullptr;
    double sx = 0.0;
    double sy = 0.0;
};

Hit hitTest(const scene::Scene& scene, double lx, double ly)
{
    const scene::Scene::Hit found = scene.at(lx, ly);
    return { static_cast<SceneOwner*>(found.owner), found.surface, found.sx, found.sy };
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
        grabbed->tree->setPosition(std::lround(cursor->x - grabX), std::lround(cursor->y - grabY));
        updateSnapZone();
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
        grabbed->tree->setPosition(left - geometry.x, top - geometry.y);
        wlr_xdg_toplevel_set_size(grabbed->xdg, right - left, bottom - top);
        return;
    }

    const Hit hit = hitTest(*sceneGraph, cursor->x, cursor->y);
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
            if (cursorMode == CursorMode::Move) {
                endSnapZone(true); // rilasciata su un bordo: si aggancia
            }
            cursorMode = CursorMode::Passthrough;
            grabbed = nullptr;
            onCursorMotion(event->time_msec);
        }
        return;
    }

    const Hit hit = hitTest(*sceneGraph, cursor->x, cursor->y);
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

    // Trascinare una finestra massimizzata o agganciata la ripristina sotto
    // il cursore, mantenendo il punto afferrato alla stessa proporzione e la
    // barra del titolo sotto il cursore (come Windows).
    if ((toplevel->maximized || toplevel->snap != Snap::None) && mode == CursorMode::Move) {
        const wlr_box frame = toplevel->frameBox();
        const double fraction = frame.width > 0 ? (cursor->x - frame.x) / frame.width : 0.5;
        const int restoredWidth = toplevel->restore.width > 0 ? toplevel->restore.width : frame.width;
        if (toplevel->maximized) {
            toplevel->setMaximized(false);
        } else {
            toplevel->setSnap(Snap::None);
        }
        const wlr_box& geometry = toplevel->xdg->base->geometry;
        toplevel->tree->setPosition(static_cast<int>(cursor->x - fraction * restoredWidth) - geometry.x,
            frame.y - geometry.y);
    }
    // Ridimensionare una finestra agganciata la sgancia, lasciandola dov'è.
    if (toplevel->snap != Snap::None && mode == CursorMode::Resize) {
        toplevel->snap = Snap::None;
        wlr_xdg_toplevel_set_tiled(toplevel->xdg, WLR_EDGE_NONE);
    }

    grabbed = toplevel;
    cursorMode = mode;

    if (mode == CursorMode::Move) {
        grabX = cursor->x - toplevel->tree->x();
        grabY = cursor->y - toplevel->tree->y();
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
    // Alt+Tab (dentro KDE: Alt+J), con Maiusc all'indietro. Si sceglie
    // finché Alt resta premuto; rilasciandolo si passa alla finestra scelta
    // (vedi Keyboard::onKey), Esc annulla.
    const bool tab = sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab;
    const bool nestedTab = sym == XKB_KEY_j || sym == XKB_KEY_J;
    if ((alt && tab) || (altNested && nestedTab)) {
        switcherStep(shift || sym == XKB_KEY_ISO_Left_Tab ? -1 : 1);
        return true;
    }
    if (m_switcher.active && sym == XKB_KEY_Escape) {
        switcherFinish(false);
        return true;
    }
    if ((super && sym == XKB_KEY_Up) || (altNested && sym == XKB_KEY_m)) {
        if (Toplevel* active = focusedToplevel()) {
            active->setMaximized(sym == XKB_KEY_Up ? true : !active->maximized);
        }
        return true;
    }
    if (super && sym == XKB_KEY_Down) {
        // Come su Windows: prima si ripristina, poi si riduce a icona.
        if (Toplevel* active = focusedToplevel()) {
            if (active->maximized) {
                active->setMaximized(false);
            } else if (active->snap != Snap::None) {
                active->setSnap(Snap::None);
            } else {
                active->setMinimized(true);
            }
        }
        return true;
    }
    if ((super || altNested) && (sym == XKB_KEY_Left || sym == XKB_KEY_Right)) {
        // Metà sinistra/destra; verso il lato opposto la finestra si sgancia.
        if (Toplevel* active = focusedToplevel()) {
            const Snap side = sym == XKB_KEY_Left ? Snap::Left : Snap::Right;
            const Snap opposite = side == Snap::Left ? Snap::Right : Snap::Left;
            active->setSnap(active->snap == opposite ? Snap::None : side);
        }
        return true;
    }
    if (altNested && sym == XKB_KEY_s) {
        sendShellCommand("toggle-start");
        return true;
    }
    return false;
}

// --------------------------------------------------------------- Alt+Tab --

void Server::switcherStep(int direction)
{
    if (m_switcher.active) {
        const size_t count = m_switcher.windows.size();
        m_switcher.selected = (m_switcher.selected + count + direction) % count;
        sendSwitcher("select");
        return;
    }

    // Tutte le finestre, dalla più recente; le ridotte a icona stanno già
    // in fondo alla lista e si ripristinano se scelte.
    m_switcher.windows.clear();
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->mapped && toplevel->extHandle) {
            m_switcher.windows.push_back(toplevel);
        }
    }
    const size_t count = m_switcher.windows.size();
    if (count == 0) {
        return;
    }
    m_switcher.active = true;
    // Si parte dalla finestra usata prima di quella attiva (o dalla più
    // recente, se nessuna è attiva).
    const bool frontActive = focusedToplevel() == m_switcher.windows.front();
    if (direction > 0) {
        m_switcher.selected = frontActive ? 1 % count : 0;
    } else {
        m_switcher.selected = count - 1;
    }
    sendSwitcher("show");
}

void Server::switcherFinish(bool activate)
{
    if (!m_switcher.active) {
        return;
    }
    m_switcher.active = false;
    Toplevel* chosen = activate && m_switcher.selected < m_switcher.windows.size()
        ? m_switcher.windows[m_switcher.selected]
        : nullptr;
    m_switcher.windows.clear();
    sendShellCommand("switcher-hide");
    if (chosen) {
        focusToplevel(chosen);
    }
}

// "switcher-show N id1 id2..." oppure "switcher-select N": la shell trova
// le finestre per identificativo (ext-foreign-toplevel-list).
void Server::sendSwitcher(const char* command)
{
    std::string line = std::string("switcher-") + command + " " + std::to_string(m_switcher.selected);
    if (std::strcmp(command, "show") == 0) {
        for (Toplevel* toplevel : m_switcher.windows) {
            line += " ";
            line += toplevel->extHandle->identifier;
        }
    }
    sendShellCommand(line);
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
