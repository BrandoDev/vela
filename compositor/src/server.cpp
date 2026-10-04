#include "server.hpp"
#include "settings.hpp"

#include "scene/effects.hpp"

#include "decoration.hpp"
#include "outputconfig.hpp"
#include "render/allocator.hpp"

#include <algorithm>
#include <csignal>
#include <linux/input-event-codes.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <utility>

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
    backend = wlr_backend_autocreate(loop, &session);
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
    // Sincronizzazione esplicita (§7.3): le app dicono quando il buffer è
    // pronto e noi quando l'abbiamo finito di leggere, con timeline del
    // kernel invece delle fence implicite dei dmabuf. Le usano Vulkan (Mesa,
    // NVIDIA) e i giochi. WLR_RENDER_NO_EXPLICIT_SYNC=1 la spegne, come nel
    // resto di wlroots.
    const char* noExplicit = std::getenv("WLR_RENDER_NO_EXPLICIT_SYNC");
    if (renderer->features.timeline && !(noExplicit && std::strcmp(noExplicit, "0") != 0)) {
        if (wlr_linux_drm_syncobj_manager_v1_create(display, 1, vulkan->renderFd)) {
            wlr_log(WLR_INFO, "Sincronizzazione esplicita con le app (linux-drm-syncobj-v1) attiva");
        }
    }

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
    outputManager = wlr_output_manager_v1_create(display);
    on(&outputManager->events.apply, [this](void* data) {
        applyOutputConfiguration(static_cast<wlr_output_configuration_v1*>(data), false);
    });
    on(&outputManager->events.test, [this](void* data) {
        applyOutputConfiguration(static_cast<wlr_output_configuration_v1*>(data), true);
    });
    // Ogni cambiamento (schermo collegato, spostato, nuova modalità) si
    // racconta ai programmi di configurazione.
    on(&outputLayout->events.change, [this](void*) {
        updateOutputConfiguration();
        updateLockLayout();
    });

    sceneGraph = std::make_unique<scene::Scene>();
    sceneGraph->watch(compositor);
    sceneGraph->linuxDmabuf = dmabuf;
    sceneGraph->eventLoop = loop;

    // L'ordine di creazione è l'ordine di impilamento (dal basso).
    scene::Tree* root = &sceneGraph->root();
    layers.background = std::make_unique<scene::Tree>(root);
    layers.bottom = std::make_unique<scene::Tree>(root);
    layers.windows = std::make_unique<scene::Tree>(root);
    layers.top = std::make_unique<scene::Tree>(root);
    layers.fullscreen = std::make_unique<scene::Tree>(root);
    layers.x11Popups = std::make_unique<scene::Tree>(root);
    layers.overlay = std::make_unique<scene::Tree>(root);
    // Sopra le finestre e sotto i pannelli: il desktop che si lascia.
    layers.windowsOut = std::make_unique<scene::Tree>(root);
    layers.windowsOut->placeAbove(layers.windows.get());
    layers.windowsOut->ignoresInput = true;
    layers.drag = std::make_unique<scene::Tree>(root);
    layers.drag->ignoresInput = true;
    layers.lock = std::make_unique<scene::Tree>(root);

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
    // La barra del titolo di Vela per le app che la accettano (§9.1).
    auto* decorations = wlr_xdg_decoration_manager_v1_create(display);
    on(&decorations->events.new_toplevel_decoration, [](void* data) {
        auto* decoration = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);
        if (auto* toplevel = static_cast<Toplevel*>(decoration->toplevel->base->data)) {
            toplevel->setXdgDecoration(decoration);
        }
    });
    // La sfocatura dietro i pannelli e le app che la chiedono (§8.3).
    scene::initBackgroundEffects(display);
    on(&layerShell->events.new_surface, [this](void* data) {
        LayerSurface::create(*this, static_cast<wlr_layer_surface_v1*>(data));
    });

    // --- puntatore ---
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, outputLayout);
    cursorManager = wlr_xcursor_manager_create(std::getenv("XCURSOR_THEME"),
        static_cast<uint32_t>(envInt("XCURSOR_SIZE", 24)));
    initPointerProtocols();

    // Un'app chiede di portare in primo piano una sua finestra (un link
    // aperto in un Firefox già aperto, il clic su una notifica).
    auto* activation = wlr_xdg_activation_v1_create(display);
    on(&activation->events.request_activate, [this](void* data) {
        auto* event = static_cast<wlr_xdg_activation_v1_request_activate_event*>(data);
        for (Toplevel* toplevel : toplevels) {
            if (toplevel->surface() == event->surface) {
                focusToplevel(toplevel);
                return;
            }
        }
    });

    on(&cursor->events.motion, [this](void* data) {
        auto* event = static_cast<wlr_pointer_motion_event*>(data);
        noteActivity();
        // Il movimento grezzo va ai giochi anche se il cursore non si muove.
        wlr_relative_pointer_manager_v1_send_relative_motion(relativePointers, seat,
            uint64_t(event->time_msec) * 1000, event->delta_x, event->delta_y, event->unaccel_dx,
            event->unaccel_dy);
        double dx = event->delta_x;
        double dy = event->delta_y;
        if (!constrainMotion(dx, dy)) {
            return; // puntatore bloccato dall'app
        }
        wlr_cursor_move(cursor, &event->pointer->base, dx, dy);
        onCursorMotion(event->time_msec);
    });
    on(&cursor->events.motion_absolute, [this](void* data) {
        auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
        noteActivity();
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
        // Anche i movimenti assoluti (tavolette, Vela annidato) rispettano un
        // puntatore bloccato o confinato dall'app.
        double lx = 0.0;
        double ly = 0.0;
        wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, x, y, &lx, &ly);
        double dx = lx - cursor->x;
        double dy = ly - cursor->y;
        if (!constrainMotion(dx, dy)) {
            return;
        }
        wlr_cursor_move(cursor, &event->pointer->base, dx, dy);
        onCursorMotion(event->time_msec);
    });
    on(&cursor->events.button, [this](void* data) {
        onCursorButton(static_cast<wlr_pointer_button_event*>(data));
    });
    on(&cursor->events.axis, [this](void* data) {
        auto* event = static_cast<wlr_pointer_axis_event*>(data);
        noteActivity();
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
    // Trascinare tra app: si accetta solo da chi ha davvero il tasto premuto
    // sulla propria superficie (serial del clic).
    on(&seat->events.request_start_drag, [this](void* data) {
        auto* event = static_cast<wlr_seat_request_start_drag_event*>(data);
        if (wlr_seat_validate_pointer_grab_serial(seat, event->origin, event->serial)) {
            wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
        } else if (event->drag->source) {
            wlr_data_source_destroy(event->drag->source);
        }
    });
    on(&seat->events.start_drag, [this](void* data) {
        auto* drag = static_cast<wlr_drag*>(data);
        implicitGrab = {}; // da qui il puntatore lo guida il trascinamento
        if (!drag->icon) {
            return;
        }
        wlr_surface* surface = drag->icon->surface;
        auto icon = std::make_unique<DragIcon>();
        icon->tree = std::make_unique<scene::Tree>(layers.drag.get());
        icon->node = std::make_unique<scene::SurfaceNode>(icon->tree.get(), surface);
        icon->commit.connect(&surface->events.commit, [this, surface](void*) {
            if (dragIcon) {
                dragIcon->dx += surface->current.dx;
                dragIcon->dy += surface->current.dy;
                updateDragIcon();
            }
        });
        icon->destroy.connect(&drag->icon->events.destroy, [this](void*) { dragIcon.reset(); });
        dragIcon = std::move(icon);
        updateDragIcon();
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
    initXwayland();
    initLock();
    initWorkspaces();

    wl_event_loop_add_signal(loop, SIGINT, handleTerminate, display);
    wl_event_loop_add_signal(loop, SIGTERM, handleTerminate, display);

    // Il ciclo dei frame deve svegliarsi all'istante giusto anche con la CPU
    // piena (una compilazione, un gioco): scheduling realtime, a priorità
    // bassa, per il thread principale, come KWin. Chi viene lanciato da
    // Vela (shell, app) non lo eredita. Serve RLIMIT_RTPRIO o CAP_SYS_NICE;
    // VELA_REALTIME=0 lo spegne.
    if (const char* realtime = std::getenv("VELA_REALTIME"); !realtime || std::strcmp(realtime, "0") != 0) {
        sched_param param {};
        param.sched_priority = std::min(10, sched_get_priority_max(SCHED_RR));
        if (sched_setscheduler(0, SCHED_RR | SCHED_RESET_ON_FORK, &param) == 0) {
            wlr_log(WLR_INFO, "Thread principale in tempo reale (SCHED_RR, priorità %d)", param.sched_priority);
        } else {
            wlr_log(WLR_INFO, "Niente scheduling realtime (%s): sotto carico i frame possono tardare",
                std::strerror(errno));
        }
    }

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
    // Solo nella sessione vera (da SDDM o da una console): annidati o
    // headless si resta ospiti dell'ambiente che c'è.
    if (session) {
        setSessionEnvironment();
    }
    // Le app X11 lanciate da qui vanno nel nostro Xwayland, non in quello
    // della sessione ospite (KDE) da cui magari siamo partiti.
    if (xwayland) {
        setenv("DISPLAY", xwayland->display_name, true);
    } else {
        unsetenv("DISPLAY");
    }
    // Plasma la esporta perché le app sopravvivano a un crash di KWin. Qui
    // non serve (Vela non si riavvia da solo) e fa danni: alla chiusura di
    // Vela le app Qt provano a riconnettersi e vanno in crash dentro Qt.
    unsetenv("QT_WAYLAND_RECONNECT");

    listenForCommands();
    if (session) {
        runSessionHook("start");
    }

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
    stopListening();
    if (session) {
        runSessionHook("stop");
    }
#if WLR_HAS_XWAYLAND
    // Chiude le finestre X11 (e i loro Toplevel) prima dei client Wayland.
    if (xwayland) {
        xwaylandReady.disconnect();
        xwaylandNewSurface.disconnect();
        wlr_xwayland_destroy(xwayland);
        xwayland = nullptr;
    }
#endif

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
    // Renderer, allocatore e device Vulkan si smontano solo per cercare
    // risorse dimenticate (VELA_VULKAN_VALIDATION=1). Alla chiusura normale
    // il processo sta per finire e il kernel recupera tutto: da annidati,
    // ogni tanto il driver amdgpu andava in crash liberando la memoria
    // della GPU (lo stato che RADV e il GBM di Mesa condividono nel
    // processo risultava già rovinato), e una sessione che si chiude non
    // deve sembrare un crash.
    const char* validation = std::getenv("VELA_VULKAN_VALIDATION");
    if (validation && *validation && std::strcmp(validation, "0") != 0) {
        // Il renderer avvisa chi lo usa (wlr_compositor) e si distrugge.
        wlr_renderer_destroy(renderer);
        wlr_allocator_destroy(allocator);
        vulkan.reset(); // per ultimo: tutto il resto ne usa il device
    } else {
        (void)vulkan.release();
    }
    wl_display_destroy(display);
}

// ----------------------------------------------------------------- focus --

Toplevel* Server::focusedToplevel() const
{
    wlr_surface* focused = seat->keyboard_state.focused_surface;
    if (!focused) {
        return nullptr;
    }
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->surface() == focused) {
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
    if (!toplevel || !toplevel->mapped || locked) {
        return;
    }
    // Una finestra di un altro desktop: si va su quel desktop, come Windows.
    if (!toplevel->onCurrentWorkspace()) {
        switchWorkspace(toplevel->workspace, false);
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
    previousLayerSurface = nullptr;

    wlr_surface* surface = toplevel->surface();
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
    if (!layer || !layer->wlr->surface->mapped || locked) {
        return;
    }
    // Come su Windows: aprendo il menu Start la finestra attiva si "spegne".
    if (Toplevel* active = focusedToplevel()) {
        active->setActivated(false);
    }
    // Un menu aperto da un pannello (es. dal menu Start): chiuso il menu, la
    // tastiera torna al pannello.
    if (focusedLayerSurface && focusedLayerSurface != layer) {
        previousLayerSurface = focusedLayerSurface;
    }
    focusedLayerSurface = layer;
    keyboardEnter(layer->wlr->surface);
}

void Server::refocus()
{
    if (locked) {
        return; // la tastiera è della schermata di blocco
    }
    focusedLayerSurface = nullptr;
    LayerSurface* previous = std::exchange(previousLayerSurface, nullptr);
    if (previous && previous->wlr->surface->mapped && previous->wantsKeyboard()) {
        focusLayer(previous);
        return;
    }
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->mapped && !toplevel->minimized && toplevel->onCurrentWorkspace()) {
            focusToplevel(toplevel);
            return;
        }
    }
    // Nessuna finestra su questo desktop: la tastiera non va a nessuno.
    if (Toplevel* previous = focusedToplevel()) {
        previous->setActivated(false);
    }
    wlr_seat_keyboard_notify_clear_focus(seat);
}

void Server::forget(Toplevel* toplevel)
{
    workspaceForget(toplevel);
    if (m_snapLayoutsHover.toplevel == toplevel) {
        hoverMaximize(nullptr);
    }
    const bool wasFocused = focusedToplevel() == toplevel;
    if (hoveredDecoration == toplevel) {
        hoveredDecoration = nullptr;
    }
    if (lastTitleClick.toplevel == toplevel) {
        lastTitleClick = {};
    }
    if (lastIconClick.toplevel == toplevel) {
        lastIconClick = {};
    }
    if (pendingTitleDrag.toplevel == toplevel) {
        pendingTitleDrag = {};
    }
    if (m_keyboardGrab.toplevel == toplevel) {
        m_keyboardGrab = {};
    }
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
    if (previousLayerSurface == layer) {
        previousLayerSurface = nullptr;
    }
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

wlr_box Server::fitInto(wlr_box frame, const Output& output)
{
    const wlr_box& area = output.usable;
    frame.width = std::min(frame.width, area.width);
    frame.height = std::min(frame.height, area.height);
    frame.x = std::clamp(frame.x, area.x, area.x + area.width - frame.width);
    frame.y = std::clamp(frame.y, area.y, area.y + area.height - frame.height);
    return frame;
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
    syncX11Windows();
    for (Toplevel* toplevel : toplevels) {
        toplevel->updateShape(); // angoli e ombra secondo lo stato di adesso
    }
    m_animationNowMs = std::max(m_animationNowMs, double(presentNs) / 1e6);
    if (m_animating.empty() && m_snapshotAnimations.empty() && !m_snapPreview.rect && workspaces.direction == 0) {
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
    const bool switching = tickWorkspaceSwitch(nowMs);
    if (!m_animating.empty() || !m_snapshotAnimations.empty() || switching
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

const char* resizeCursor(uint32_t edges)
{
    const bool top = edges & WLR_EDGE_TOP;
    const bool bottom = edges & WLR_EDGE_BOTTOM;
    const bool left = edges & WLR_EDGE_LEFT;
    const bool right = edges & WLR_EDGE_RIGHT;
    if (top) {
        return left ? "nw-resize" : right ? "ne-resize" : "n-resize";
    }
    if (bottom) {
        return left ? "sw-resize" : right ? "se-resize" : "s-resize";
    }
    return left ? "w-resize" : "e-resize";
}

} // namespace

Toplevel* Server::resizeBorderAt(double lx, double ly, uint32_t& edges) const
{
    constexpr double band = 8.0; // fuori dalla finestra, come in Windows 11
    constexpr double inner = 4.0; // in alto anche dentro la barra del titolo
    constexpr double corner = 16.0; // gli angoli prendono anche un tratto dei lati
    edges = 0;
    // Sopra le finestre (taskbar, menu, pannelli): niente bordi.
    const Hit hit = hitTest(*sceneGraph, lx, ly);
    if (hit.owner && hit.owner->kind == SceneKind::Layer) {
        const auto layer = static_cast<LayerSurface*>(hit.owner)->layer;
        if (layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP || layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
            return nullptr;
        }
    }
    // Dalla finestra più in alto: la prima che copre il punto vince.
    const auto& children = layers.windows->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        auto* owner = static_cast<SceneOwner*>((*it)->data);
        if (!owner || owner->kind != SceneKind::Toplevel || !(*it)->enabled()) {
            continue;
        }
        auto* toplevel = static_cast<Toplevel*>(owner);
        if (!toplevel->mapped || toplevel->minimized) {
            continue;
        }
        const wlr_box f = toplevel->frameBox();
        const bool resizable = toplevel->decoration && !toplevel->maximized && !toplevel->fullscreen
            && toplevel->resizable();
        const bool inside = lx >= f.x && lx < f.x + f.width && ly >= f.y && ly < f.y + f.height;
        if (inside && !(resizable && ly < f.y + inner)) {
            return nullptr; // la finestra copre il punto
        }
        if (!resizable || lx < f.x - band || lx >= f.x + f.width + band || ly < f.y - band
            || ly >= f.y + f.height + band) {
            continue;
        }
        bool left = lx < f.x;
        bool right = lx >= f.x + f.width;
        bool top = ly < f.y + (inside ? inner : 0.0);
        bool bottom = ly >= f.y + f.height;
        if (top || bottom) {
            left = left || lx < f.x + corner;
            right = right || (!left && lx >= f.x + f.width - corner);
        }
        if (left || right) {
            top = top || ly < f.y + corner;
            bottom = bottom || (!top && ly >= f.y + f.height - corner);
        }
        edges = (top ? WLR_EDGE_TOP : 0) | (bottom ? WLR_EDGE_BOTTOM : 0) | (left ? WLR_EDGE_LEFT : 0)
            | (right ? WLR_EDGE_RIGHT : 0);
        return edges ? toplevel : nullptr;
    }
    return nullptr;
}

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

void Server::updateDragIcon()
{
    if (dragIcon) {
        dragIcon->tree->setPosition(cursor->x + dragIcon->dx, cursor->y + dragIcon->dy);
    }
}

void Server::onCursorMotion(uint32_t timeMsec)
{
    updateDragIcon();
    if (pendingTitleDrag.toplevel
        && std::hypot(cursor->x - pendingTitleDrag.x, cursor->y - pendingTitleDrag.y) > 4.0) {
        Toplevel* toplevel = pendingTitleDrag.toplevel;
        pendingTitleDrag = {};
        beginInteractive(toplevel, CursorMode::Move, 0, /*fromModifier=*/true);
    }
    if (cursorMode == CursorMode::Move && grabbed) {
        // Posizione esatta, anche frazionaria: al disegno la finestra si
        // aggancia al pixel fisico più vicino (§3.4). Con posizioni logiche
        // intere, al 125% la finestra avanzerebbe a scatti di 1 e 2 pixel.
        grabbed->tree->setPosition(cursor->x - grabX, cursor->y - grabY);
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

        const wlr_box geometry = grabbed->geometry();
        grabbed->tree->setPosition(left - geometry.x, top - geometry.y);
        grabbed->configureSize(right - left, bottom - top);
        return;
    }

    // Un tasto premuto su una superficie: il movimento resta suo finché non
    // lo si rilascia (la selezione a riquadro che esce dallo schermo, una
    // barra di scorrimento trascinata fuori dalla finestra).
    if (implicitGrab.surface && !seat->drag) {
        if (seat->pointer_state.button_count > 0 && seat->pointer_state.focused_surface == implicitGrab.surface) {
            wlr_seat_pointer_notify_motion(
                seat, timeMsec, cursor->x - implicitGrab.originX, cursor->y - implicitGrab.originY);
            return;
        }
        implicitGrab = {};
    }

    // Sul bordo di una finestra con la barra di Vela: le frecce per ridimensionare.
    if (cursorMode == CursorMode::Passthrough && seat->pointer_state.button_count == 0 && !seat->drag) {
        uint32_t edges = 0;
        if (resizeBorderAt(cursor->x, cursor->y, edges)) {
            if (hoveredDecoration && hoveredDecoration->decoration) {
                hoveredDecoration->decoration->setHover(Decoration::Part::None);
            }
            hoveredDecoration = nullptr;
            wlr_seat_pointer_clear_focus(seat);
            updatePointerConstraint(nullptr);
            wlr_cursor_set_xcursor(cursor, cursorManager, resizeCursor(edges));
            return;
        }
    }

    const Hit hit = hitTest(*sceneGraph, cursor->x, cursor->y);
    // Sopra la barra del titolo di Vela: i pulsanti si illuminano.
    Toplevel* decorated = hit.owner && !hit.surface && hit.owner->kind == SceneKind::Toplevel
        ? static_cast<Toplevel*>(hit.owner)
        : nullptr;
    if (decorated && !decorated->decoration) {
        decorated = nullptr;
    }
    if (hoveredDecoration && hoveredDecoration != decorated && hoveredDecoration->decoration) {
        hoveredDecoration->decoration->setHover(Decoration::Part::None);
    }
    hoveredDecoration = decorated;
    if (decorated) {
        const Decoration::Part part = decorated->decoration->partAt(cursor->x, cursor->y);
        decorated->decoration->setHover(part);
        hoverMaximize(part == Decoration::Part::Maximize ? decorated : nullptr);
    } else {
        hoverMaximize(nullptr);
    }
    if (!hit.surface) {
        wlr_cursor_set_xcursor(cursor, cursorManager, "default");
        wlr_seat_pointer_clear_focus(seat);
        updatePointerConstraint(nullptr);
        return;
    }
    wlr_seat_pointer_notify_enter(seat, hit.surface, hit.sx, hit.sy);
    wlr_seat_pointer_notify_motion(seat, timeMsec, hit.sx, hit.sy);
    updatePointerConstraint(hit.surface);
}

void Server::onCursorButton(wlr_pointer_button_event* event)
{
    superTap = false;
    noteActivity();
    // Bloccato: il clic dà la tastiera alla schermata di blocco sotto il
    // mouse (con più schermi) e arriva solo a lei.
    if (locked) {
        wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
        if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
            const Hit hit = hitTest(*sceneGraph, cursor->x, cursor->y);
            if (hit.surface) {
                keyboardEnter(hit.surface);
            }
        }
        return;
    }

    // "Sposta" o "Ridimensiona" da tastiera in corso: un clic conferma.
    if (keyboardGrabActive() && event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        finishKeyboardGrab(true);
        modifierGrab = true; // nemmeno il rilascio arriva all'app
        return;
    }

    // Sul bordo di una finestra con la barra di Vela: si ridimensiona.
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && event->button == BTN_LEFT
        && cursorMode == CursorMode::Passthrough && seat->pointer_state.button_count == 0) {
        uint32_t edges = 0;
        if (Toplevel* toplevel = resizeBorderAt(cursor->x, cursor->y, edges)) {
            focusToplevel(toplevel);
            beginInteractive(toplevel, CursorMode::Resize, edges, /*fromModifier=*/true);
            if (cursorMode != CursorMode::Passthrough) {
                modifierGrab = true; // nemmeno il rilascio arriva all'app
                return;
            }
        }
    }

    // Super + trascinamento sposta la finestra, Super + tasto destro la
    // ridimensiona (dall'angolo più vicino), come in KDE. Serve anche alle
    // finestre X11 senza barra del titolo propria, finché Vela non disegna
    // la sua. Il clic non arriva all'app.
    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    const bool super = keyboard && (wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_LOGO);
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && super && cursorMode == CursorMode::Passthrough
        && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
        const Hit hit = hitTest(*sceneGraph, cursor->x, cursor->y);
        if (hit.owner && hit.owner->kind == SceneKind::Toplevel) {
            auto* toplevel = static_cast<Toplevel*>(hit.owner);
            focusToplevel(toplevel);
            uint32_t edges = 0;
            if (event->button == BTN_RIGHT) {
                const wlr_box frame = toplevel->frameBox();
                edges |= cursor->x < frame.x + frame.width / 2.0 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
                edges |= cursor->y < frame.y + frame.height / 2.0 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
            }
            beginInteractive(toplevel, event->button == BTN_LEFT ? CursorMode::Move : CursorMode::Resize, edges,
                /*fromModifier=*/true);
            if (cursorMode != CursorMode::Passthrough) {
                modifierGrab = true;
                return;
            }
        }
    }
    if (modifierGrab && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        modifierGrab = false; // la pressione non era arrivata all'app
        pendingTitleDrag = {};
    } else {
        // Il primo tasto premuto su una superficie la "prende" (vedi implicitGrab).
        if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && seat->pointer_state.button_count == 0
            && seat->pointer_state.focused_surface && !seat->drag) {
            implicitGrab = { seat->pointer_state.focused_surface, cursor->x - seat->pointer_state.sx,
                cursor->y - seat->pointer_state.sy };
        }
        wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
    }

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        // Rilasciati tutti i tasti: il puntatore torna a ciò che ha sotto.
        if (implicitGrab.surface && seat->pointer_state.button_count == 0) {
            implicitGrab = {};
            if (cursorMode == CursorMode::Passthrough) {
                onCursorMotion(event->time_msec);
            }
        }
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
    // La barra del titolo di Vela: pulsanti, trascinamento, doppio clic;
    // col tasto destro il menu della finestra.
    if (hit.owner->kind == SceneKind::Toplevel && !hit.surface) {
        auto* toplevel = static_cast<Toplevel*>(hit.owner);
        if (toplevel->decoration && event->button == BTN_LEFT) {
            onDecorationPress(toplevel, event->time_msec);
            return;
        }
        if (toplevel->decoration && event->button == BTN_RIGHT
            && toplevel->decoration->partAt(cursor->x, cursor->y) == Decoration::Part::Title) {
            focusToplevel(toplevel);
            showWindowMenu(toplevel, cursor->x, cursor->y);
            return;
        }
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

void Server::onDecorationPress(Toplevel* toplevel, uint32_t timeMsec)
{
    focusToplevel(toplevel);
    switch (toplevel->decoration->partAt(cursor->x, cursor->y)) {
    case Decoration::Part::Close:
        toplevel->sendClose();
        return;
    case Decoration::Part::Maximize:
        toplevel->setMaximized(!toplevel->maximized);
        return;
    case Decoration::Part::Minimize:
        toplevel->setMinimized(true);
        return;
    case Decoration::Part::Icon: {
        // Come Windows: un clic sull'icona apre il menu della finestra, un
        // doppio clic la chiude.
        const bool doubleClick = lastIconClick.toplevel == toplevel && timeMsec - lastIconClick.timeMsec < 400;
        lastIconClick = { toplevel, timeMsec };
        if (doubleClick) {
            lastIconClick = {};
            toplevel->sendClose();
            return;
        }
        const wlr_box frame = toplevel->frameBox();
        showWindowMenu(toplevel, frame.x + Decoration::iconX - 4, frame.y + Decoration::height);
        return;
    }
    case Decoration::Part::Title: {
        // Doppio clic: massimizza o ripristina, come su Windows.
        const bool doubleClick = lastTitleClick.toplevel == toplevel && timeMsec - lastTitleClick.timeMsec < 400;
        lastTitleClick = { toplevel, timeMsec };
        if (doubleClick) {
            lastTitleClick = {};
            toplevel->setMaximized(!toplevel->maximized);
            return;
        }
        // Il trascinamento parte solo se il mouse si muove davvero: un clic
        // (o il primo di un doppio clic) non deve ripristinare una finestra
        // massimizzata.
        pendingTitleDrag = { toplevel, cursor->x, cursor->y };
        modifierGrab = true; // il rilascio non va all'app
        return;
    }
    case Decoration::Part::None:
        return;
    }
}

void Server::beginInteractive(Toplevel* toplevel, CursorMode mode, uint32_t edges, bool fromModifier)
{
    // Accetta la richiesta di un'app solo dalla finestra su cui si trova il
    // puntatore.
    wlr_surface* focused = seat->pointer_state.focused_surface;
    if (!fromModifier && (!focused || wlr_surface_get_root_surface(focused) != toplevel->surface())) {
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
        const int restoredWidth = toplevel->restoreBox().width;
        if (toplevel->maximized) {
            toplevel->setMaximized(false);
        } else {
            toplevel->setSnap(Snap::None);
        }
        const wlr_box geometry = toplevel->geometry();
        toplevel->tree->setPosition(static_cast<int>(cursor->x - fraction * restoredWidth) - geometry.x,
            frame.y - geometry.y);
    }
    // Ridimensionare una finestra agganciata la sgancia, lasciandola dov'è.
    if (toplevel->snap != Snap::None && mode == CursorMode::Resize) {
        toplevel->snap = Snap::None;
        toplevel->sendTiled(WLR_EDGE_NONE);
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

    // Ctrl+Alt+F1...F12: un'altra console (o la sessione di Plasma). La
    // tastiera traduce già la combinazione nel tasto XF86Switch_VT_n.
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
        if (session) {
            wlr_session_change_vt(session, unsigned(sym - XKB_KEY_XF86Switch_VT_1 + 1));
        }
        return true;
    }
    // L'app a fuoco tiene le scorciatoie per sé (macchina virtuale, desktop
    // remoto), o lo schermo è bloccato: resta solo il cambio di console
    // qui sopra.
    if (shortcutsInhibited() || locked) {
        return false;
    }
    // Win+L blocca lo schermo, come su Windows (Alt+L dentro KDE).
    if ((super || altNested) && (sym == XKB_KEY_l || sym == XKB_KEY_L)) {
        lockScreen();
        return true;
    }
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
            active->sendClose();
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
    // Desktop virtuali, come Windows: Win+Ctrl+←/→ per passare, Win+Ctrl+D
    // per crearne uno nuovo, Win+Ctrl+F4 per chiudere quello in uso (dentro
    // KDE con Alt+Ctrl); Win+Tab (Alt+W) la Visualizzazione attività.
    const bool ctrl = modifiers & WLR_MODIFIER_CTRL;
    if ((super || altNested) && ctrl) {
        if (sym == XKB_KEY_Left || sym == XKB_KEY_Right) {
            switchWorkspace(workspaces.current + (sym == XKB_KEY_Left ? -1 : 1));
            return true;
        }
        if (sym == XKB_KEY_d || sym == XKB_KEY_D) {
            switchWorkspace(addWorkspace());
            return true;
        }
        if (sym == XKB_KEY_F4) {
            removeWorkspace(workspaces.current);
            return true;
        }
    }
    if ((super && tab) || (altNested && (sym == XKB_KEY_w || sym == XKB_KEY_W))) {
        sendShellCommand("task-view");
        return true;
    }
    // Win+frecce: metà, quarti, massimizza, riduci (snap.cpp). Dentro KDE
    // Alt+frecce e Alt+M (massimizza o ripristina).
    if ((super || altNested) && (sym == XKB_KEY_Left || sym == XKB_KEY_Right || (super && (sym == XKB_KEY_Up || sym == XKB_KEY_Down)))) {
        if (Toplevel* active = focusedToplevel()) {
            snapWithKeyboard(active, sym);
        }
        return true;
    }
    if (altNested && sym == XKB_KEY_m) {
        if (Toplevel* active = focusedToplevel()) {
            active->setMaximized(!active->maximized);
        }
        return true;
    }
    // Win+Z: i layout di snap della finestra attiva.
    if ((super || altNested) && (sym == XKB_KEY_z || sym == XKB_KEY_Z)) {
        if (Toplevel* active = focusedToplevel()) {
            showSnapLayouts(active, true);
        }
        return true;
    }
    if (altNested && sym == XKB_KEY_s) {
        sendShellCommand("toggle-start");
        return true;
    }
    // Win+X: il menu del pulsante Start; Win+R: Esegui; Win+D: il desktop.
    // Dentro KDE con Alt.
    const xkb_keysym_t lower = xkb_keysym_to_lower(sym);
    if ((super || altNested) && lower == XKB_KEY_x) {
        sendShellCommand("winx");
        return true;
    }
    if ((super || altNested) && lower == XKB_KEY_r) {
        sendShellCommand("run");
        return true;
    }
    if ((super || altNested) && lower == XKB_KEY_d) {
        sendShellCommand("show-desktop");
        return true;
    }
    // Win+A: impostazioni rapide; Win+N: centro notifiche e calendario.
    if ((super || altNested) && lower == XKB_KEY_a) {
        sendShellCommand("quick-settings");
        return true;
    }
    if ((super || altNested) && lower == XKB_KEY_n) {
        sendShellCommand("notification-center");
        return true;
    }
    // Win+I: le Impostazioni; Win+E: Esplora file.
    if ((super || altNested) && lower == XKB_KEY_i) {
        sendShellCommand("settings");
        return true;
    }
    if ((super || altNested) && lower == XKB_KEY_e) {
        sendShellCommand("files");
        return true;
    }
    // Alt+Spazio: il menu della finestra, sotto la sua barra del titolo.
    if (alt && !super && sym == XKB_KEY_space) {
        if (Toplevel* active = focusedToplevel()) {
            const wlr_box frame = active->frameBox();
            const int bar = active->titleBarHeight() > 0 ? active->titleBarHeight() : 36;
            showWindowMenu(active, frame.x + 4, frame.y + bar, /*keyboard=*/true);
            return true;
        }
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
    // Solo il desktop in uso, come Windows.
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->mapped && toplevel->extHandle && toplevel->onCurrentWorkspace()) {
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

// ------------------------------------------------------------ schermi --

void Server::updateOutputConfiguration()
{
    wlr_output_configuration_v1* config = wlr_output_configuration_v1_create();
    for (Output* output : outputs) {
        wlr_output_configuration_head_v1* head = wlr_output_configuration_head_v1_create(config, output->wlr);
        wlr_box box {};
        wlr_output_layout_get_box(outputLayout, output->wlr, &box);
        head->state.enabled = output->wlr->enabled && !wlr_box_empty(&box);
        head->state.x = box.x;
        head->state.y = box.y;
    }
    wlr_output_manager_v1_set_configuration(outputManager, config);
}

void Server::applyOutputConfiguration(wlr_output_configuration_v1* config, bool testOnly)
{
    bool ok = true;
    std::vector<CurrentOutput> applied;
    wlr_output_configuration_head_v1* head;
    wl_list_for_each(head, &config->heads, link)
    {
        const wlr_output_head_v1_state& wanted = head->state;
        auto* output = static_cast<Output*>(wanted.output->data);
        if (!output) {
            ok = false;
            continue;
        }
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&wanted, &state);
        if (testOnly) {
            ok = wlr_output_test_state(wanted.output, &state) && ok;
        } else if (wanted.enabled) {
            // Prima la posizione: il primo frame alla nuova modalità si
            // disegna già lì.
            wlr_output_layout_add(outputLayout, wanted.output, wanted.x, wanted.y);
            ok = output->commitMode(state) && ok;
            applied.push_back({ wanted.output, true, wanted.x, wanted.y });
        } else {
            ok = wlr_output_commit_state(wanted.output, &state) && ok;
            wlr_output_layout_remove(outputLayout, wanted.output);
            applied.push_back({ wanted.output, false, 0, 0 });
        }
        wlr_output_state_finish(&state);
    }
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
    if (testOnly) {
        return;
    }

    // Le finestre rimaste fuori da ogni schermo tornano su quello principale.
    Output* fallback = nullptr;
    for (Output* output : outputs) {
        if (output->wlr->enabled && !wlr_box_empty(&output->usable)) {
            fallback = fallback ? fallback : output;
        }
        output->arrangeLayers();
    }
    for (Toplevel* toplevel : toplevels) {
        const wlr_box frame = toplevel->frameBox();
        if (fallback && !outputAt(frame.x + frame.width / 2.0, frame.y + frame.height / 2.0)) {
            // Al centro dell'area utile, come una finestra appena aperta.
            const wlr_box& area = fallback->usable;
            const double marginX = frame.x - toplevel->tree->x(); // margine d'ombra
            const double marginY = frame.y - toplevel->tree->y();
            toplevel->tree->setPosition(area.x + std::max(0, (area.width - frame.width) / 2) - marginX,
                area.y + std::max(0, (area.height - frame.height) / 2) - marginY);
        }
        toplevel->keepInPlace();
    }
    scene::Scene::changed();

    // Scelte dell'utente: si ricordano (solo nella sessione vera).
    if (session && ok) {
        saveOutputs(applied);
    }
    wlr_log(WLR_INFO, "Configurazione degli schermi %s", ok ? "applicata" : "applicata solo in parte");
}

// ----------------------------------------------------------- sessione --

// Nella sessione vera Vela è il desktop: lo dice alle app. Solo i valori
// che nessuno (SDDM, l'utente) ha già scelto.
void Server::setSessionEnvironment()
{
    setenv("XDG_CURRENT_DESKTOP", "Vela", false);
    setenv("XDG_SESSION_DESKTOP", "vela", false);
    setenv("XDG_SESSION_TYPE", "wayland", true); // da una console vale "tty"
    // Le app Qt e KDE con il tema scelto in KDE (stile, colori, font,
    // icone), come dentro Plasma.
    if (access("/usr/lib/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so", F_OK) == 0) {
        setenv("QT_QPA_PLATFORMTHEME", "kde", false);
    }
    // Il menu delle applicazioni di KDE: senza, Dolphin e gli altri non
    // sanno con cosa aprire i file.
    if (access("/etc/xdg/menus/plasma-applications.menu", F_OK) == 0) {
        setenv("XDG_MENU_PREFIX", "plasma-", false);
    }
}

void Server::runSessionHook(const char* action)
{
    // Accanto al compositor (cartella di build), altrimenti installato.
    std::string hook;
    char self[PATH_MAX] {};
    if (const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1); n > 0) {
        std::string dir(self, size_t(n));
        dir.resize(dir.rfind('/'));
        if (access((dir + "/vela-session-env").c_str(), X_OK) == 0) {
            hook = dir + "/vela-session-env";
        }
    }
    if (hook.empty()) {
        hook = std::string(VELA_LIBEXECDIR) + "/vela-session-env";
        if (access(hook.c_str(), X_OK) != 0) {
            wlr_log(WLR_INFO, "Sessione: non trovo vela-session-env, niente collegamento a systemd");
            return;
        }
    }
    const pid_t pid = fork();
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        execl(hook.c_str(), hook.c_str(), action, nullptr);
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        wlr_log(WLR_INFO, "Sessione: %s %s (%s)", hook.c_str(), action,
            WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "fatto" : "fallito");
    }
}

// ------------------------------------------------- comandi al compositor --

void Server::listenForCommands()
{
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir) {
        return;
    }
    m_commands.path = std::string(runtimeDir) + "/vela-" + socketName + ".sock";
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    if (m_commands.path.size() >= sizeof(address.sun_path)) {
        return;
    }
    std::strncpy(address.sun_path, m_commands.path.c_str(), sizeof(address.sun_path) - 1);
    unlink(m_commands.path.c_str()); // rimasto da un'esecuzione precedente

    m_commands.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (m_commands.fd < 0 || bind(m_commands.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || listen(m_commands.fd, 4) != 0) {
        wlr_log_errno(WLR_ERROR, "Impossibile ascoltare i comandi su %s", m_commands.path.c_str());
        stopListening();
        return;
    }
    chmod(m_commands.path.c_str(), 0600);
    m_commands.source = wl_event_loop_add_fd(loop, m_commands.fd, WL_EVENT_READABLE,
        [](int fd, uint32_t, void* data) {
            auto* self = static_cast<Server*>(data);
            const int clientFd = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (clientFd < 0) {
                return 0;
            }
            auto client = std::make_unique<CommandSocket::Client>(CommandSocket::Client { clientFd, nullptr, {} });
            CommandSocket::Client* raw = client.get();
            raw->source = wl_event_loop_add_fd(self->loop, clientFd, WL_EVENT_READABLE,
                [](int fd, uint32_t mask, void* data) {
                    auto* self = static_cast<Server*>(data);
                    auto& clients = self->m_commands.clients;
                    auto it = std::find_if(clients.begin(), clients.end(),
                        [fd](const std::unique_ptr<CommandSocket::Client>& c) { return c->fd == fd; });
                    if (it == clients.end()) {
                        return 0;
                    }
                    CommandSocket::Client& client = **it;
                    char chunk[256];
                    ssize_t n = 0;
                    while ((n = read(fd, chunk, sizeof(chunk))) > 0 && client.buffer.size() < 4096) {
                        client.buffer.append(chunk, size_t(n));
                    }
                    std::vector<std::string> lines;
                    size_t newline = 0;
                    while ((newline = client.buffer.find('\n')) != std::string::npos) {
                        lines.push_back(client.buffer.substr(0, newline));
                        client.buffer.erase(0, newline + 1);
                    }
                    // "modifiers": una domanda, con risposta. La shell non ha la
                    // tastiera quando si clicca la taskbar, quindi non sa se Maiusc è
                    // premuto (Maiusc+clic destro: il menu della finestra).
                    for (const std::string& line : lines) {
                        if (line == "modifiers") {
                            wlr_keyboard* keyboard = wlr_seat_get_keyboard(self->seat);
                            const std::string reply
                                = std::to_string(keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0) + "\n";
                            (void)!write(fd, reply.data(), reply.size());
                        } else if (line == "workspaces") {
                            // I desktop virtuali, per la shell appena partita.
                            const std::string reply = self->workspacesJson() + "\n";
                            (void)!write(fd, reply.data(), reply.size());
                        }
                    }
                    const bool closed = n == 0 || (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))
                        || client.buffer.size() >= 4096;
                    if (closed) {
                        wl_event_source_remove(client.source);
                        close(client.fd);
                        clients.erase(it);
                    }
                    // Dopo aver sistemato il client: un comando può chiudere tutto.
                    for (const std::string& line : lines) {
                        self->handleCommand(line);
                    }
                    return 0;
                },
                self);
            self->m_commands.clients.push_back(std::move(client));
            return 0;
        },
        this);
}

void Server::stopListening()
{
    for (auto& client : m_commands.clients) {
        wl_event_source_remove(client->source);
        close(client->fd);
    }
    m_commands.clients.clear();
    if (m_commands.source) {
        wl_event_source_remove(m_commands.source);
        m_commands.source = nullptr;
    }
    if (m_commands.fd >= 0) {
        close(m_commands.fd);
        m_commands.fd = -1;
        unlink(m_commands.path.c_str());
    }
}

void Server::handleCommand(const std::string& command)
{
    if (command == "logout") {
        wlr_log(WLR_INFO, "Uscita chiesta dalla shell");
        wl_display_terminate(display);
    } else if (command.rfind("wallpaper-tint ", 0) == 0) {
        // wallpaper-tint R G B (0-255): il colore medio dello sfondo, per le barre.
        int r = 0;
        int g = 0;
        int b = 0;
        if (std::sscanf(command.c_str() + 15, "%d %d %d", &r, &g, &b) == 3) {
            wallpaperTint[0] = std::clamp(r, 0, 255) / 255.0f;
            wallpaperTint[1] = std::clamp(g, 0, 255) / 255.0f;
            wallpaperTint[2] = std::clamp(b, 0, 255) / 255.0f;
            hasWallpaperTint = true;
            ++wallpaperTintVersion;
            for (Toplevel* toplevel : toplevels) {
                if (toplevel->decoration) {
                    toplevel->decoration->update();
                }
            }
        }
    } else if (command == "modifiers" || command == "workspaces") {
        // già risposto a chi l'ha chiesto (listenForCommands)
    } else if (command.rfind("workspace ", 0) == 0) {
        // workspace switch|close <n>, workspace new [switch], workspace
        // rename <n> <nome>, workspace move <da> <a>: dalla Visualizzazione attività.
        char verb[16] = {};
        int a = -1;
        int b = -1;
        int consumed = 0;
        std::sscanf(command.c_str() + 10, "%15s %n", verb, &consumed);
        const std::string rest = command.substr(std::min(command.size(), size_t(10 + consumed)));
        const std::string what = verb;
        if (what == "switch" && std::sscanf(rest.c_str(), "%d", &a) == 1) {
            switchWorkspace(a);
        } else if (what == "new") {
            const int index = addWorkspace();
            if (rest == "switch") {
                switchWorkspace(index);
            }
        } else if (what == "close" && std::sscanf(rest.c_str(), "%d", &a) == 1) {
            removeWorkspace(a);
        } else if (what == "rename" && std::sscanf(rest.c_str(), "%d %n", &a, &consumed) >= 1) {
            renameWorkspace(a, rest.substr(std::min(rest.size(), size_t(consumed))));
        } else if (what == "move" && std::sscanf(rest.c_str(), "%d %d", &a, &b) == 2) {
            moveWorkspace(a, b);
        }
    } else if (command == "reload-config") {
        // Le Impostazioni hanno cambiato vela.conf.
        wlr_log(WLR_INFO, "Impostazioni: rileggo vela.conf");
        loadIdleSettings();
        const Settings settings = readSettings();
        for (Keyboard* keyboard : keyboards) {
            keyboard->applySettings(settings);
        }
        if (seat->keyboard_state.keyboard) {
            wlr_seat_set_keyboard(seat, seat->keyboard_state.keyboard); // il layout nuovo alle app
        }
    } else if (command == "lock") {
        lockScreen(); // per esempio prima di sospendere il computer
    } else if (command.rfind("window ", 0) == 0) {
        // window <identificativo ext-foreign-toplevel | active> <azione>: dal
        // menu della finestra.
        const size_t space = command.find(' ', 7);
        if (space == std::string::npos) {
            return;
        }
        const std::string id = command.substr(7, space - 7);
        const std::string action = command.substr(space + 1);
        Toplevel* target = nullptr;
        if (id == "active") {
            target = focusedToplevel();
        } else {
            for (Toplevel* toplevel : toplevels) {
                if (toplevel->extHandle && id == toplevel->extHandle->identifier) {
                    target = toplevel;
                }
            }
        }
        if (target && !locked) {
            windowAction(target, action);
        }
    } else if (command.rfind("end-task ", 0) == 0) {
        // "Termina attività": chiude subito i processi delle finestre di
        // quell'app, senza chiedere (come Windows).
        const std::string appId = command.substr(9);
        std::vector<pid_t> pids;
        for (Toplevel* toplevel : toplevels) {
            const pid_t pid = toplevel->pid();
            if (appId == toplevel->appId() && pid > 1 && pid != getpid()
                && std::find(pids.begin(), pids.end(), pid) == pids.end()) {
                pids.push_back(pid);
            }
        }
        for (pid_t pid : pids) {
            wlr_log(WLR_INFO, "Termina attività: %s (processo %d)", appId.c_str(), int(pid));
            kill(pid, SIGKILL);
        }
    } else if (!command.empty()) {
        wlr_log(WLR_DEBUG, "Comando sconosciuto: %s", command.c_str());
    }
}

void Server::showWindowMenu(Toplevel* toplevel, double lx, double ly, bool keyboard)
{
    if (!toplevel || !toplevel->extHandle || locked) {
        return;
    }
    Output* out = outputAt(lx, ly);
    if (!out) {
        out = toplevel->output();
    }
    if (!out) {
        return;
    }
    // Alla shell: la finestra, lo schermo e il punto in coordinate dello schermo.
    const wlr_box box = out->box();
    char line[512];
    snprintf(line, sizeof line, "window-menu %s %s %d %d %d %d %d", toplevel->extHandle->identifier, out->wlr->name,
        int(std::lround(lx - box.x)), int(std::lround(ly - box.y)), toplevel->maximized ? 1 : 0,
        toplevel->resizable() ? 1 : 0, keyboard ? 1 : 0);
    sendShellCommand(line);
}

void Server::windowAction(Toplevel* toplevel, const std::string& action)
{
    if (action == "activate") {
        // Dalla Visualizzazione attività: in primo piano (sul suo desktop).
        if (toplevel->minimized) {
            toplevel->setMinimized(false);
        } else {
            focusToplevel(toplevel);
        }
    } else if (action == "restore") {
        if (toplevel->minimized) {
            toplevel->setMinimized(false);
            focusToplevel(toplevel);
        } else if (toplevel->maximized) {
            toplevel->setMaximized(false);
        } else if (toplevel->snap != Snap::None) {
            toplevel->setSnap(Snap::None);
        }
    } else if (action == "minimize") {
        toplevel->setMinimized(true);
    } else if (action == "maximize") {
        toplevel->setMaximized(true);
    } else if (action == "close") {
        toplevel->sendClose();
    } else if (action == "move") {
        beginKeyboardGrab(toplevel, CursorMode::Move);
    } else if (action == "resize") {
        beginKeyboardGrab(toplevel, CursorMode::Resize);
    } else if (action == "snap-left" || action == "snap-right") {
        // Dalla Visualizzazione attività: "Aggancia a sinistra/destra".
        focusToplevel(toplevel);
        toplevel->setSnap(action == "snap-left" ? Snap::Left : Snap::Right);
    } else if (action.rfind("snap ", 0) == 0) {
        // Dai layout di snap e da Snap Assist: "snap x0 y0 x1 y1 [quiet]", in dodicesimi.
        Snap tile;
        char quiet[8] = {};
        const int n = std::sscanf(action.c_str() + 5, "%d %d %d %d %7s", &tile.x0, &tile.y0, &tile.x1, &tile.y1, quiet);
        if (n >= 4 && tile.valid()) {
            if (toplevel->minimized) {
                toplevel->setMinimized(false);
            }
            focusToplevel(toplevel);
            toplevel->setSnap(tile);
            if (std::string(quiet) != "quiet") {
                offerSnapAssist(toplevel);
            }
        }
    } else if (action.rfind("move-to ", 0) == 0) {
        moveToWorkspace(toplevel, std::atoi(action.c_str() + 8));
    } else if (action == "move-to-new") {
        moveToWorkspace(toplevel, addWorkspace());
    } else if (action == "sticky" || action == "unsticky") {
        setSticky(toplevel, action == "sticky");
    } else if ((action == "app-sticky" || action == "app-unsticky") && toplevel->appId()) {
        setAppSticky(toplevel->appId(), action == "app-sticky");
    }
}

void Server::beginKeyboardGrab(Toplevel* toplevel, CursorMode mode)
{
    if (toplevel->minimized || toplevel->maximized || toplevel->fullscreen || cursorMode != CursorMode::Passthrough) {
        return;
    }
    if (toplevel->snap != Snap::None) {
        toplevel->setSnap(Snap::None);
    }
    focusToplevel(toplevel);
    toplevel->finishOpenAnimation();
    const wlr_box frame = toplevel->frameBox();
    m_keyboardGrab = { toplevel, mode, false, toplevel->tree->x(), toplevel->tree->y(), toplevel->geometry() };
    // Come Windows: il puntatore va sulla barra del titolo (spostare) o al
    // centro della finestra (ridimensionare), e da lì la segue.
    if (mode == CursorMode::Move) {
        wlr_cursor_warp(cursor, nullptr, frame.x + frame.width / 2.0, frame.y + std::min(16, frame.height / 2));
        beginInteractive(toplevel, CursorMode::Move, 0, /*fromModifier=*/true);
    } else {
        wlr_cursor_warp(cursor, nullptr, frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
    }
    wlr_seat_pointer_clear_focus(seat);
    wlr_cursor_set_xcursor(cursor, cursorManager, mode == CursorMode::Move ? "move" : "all-scroll");
}

void Server::keyboardGrabKey(xkb_keysym_t sym, uint32_t modifiers)
{
    Toplevel* toplevel = m_keyboardGrab.toplevel;
    const double step = (modifiers & WLR_MODIFIER_CTRL) ? 1.0 : 10.0;
    double dx = 0.0;
    double dy = 0.0;
    uint32_t edge = 0;
    switch (sym) {
    case XKB_KEY_Left: dx = -step; edge = WLR_EDGE_LEFT; break;
    case XKB_KEY_Right: dx = step; edge = WLR_EDGE_RIGHT; break;
    case XKB_KEY_Up: dy = -step; edge = WLR_EDGE_TOP; break;
    case XKB_KEY_Down: dy = step; edge = WLR_EDGE_BOTTOM; break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        finishKeyboardGrab(true);
        return;
    case XKB_KEY_Escape:
        finishKeyboardGrab(false);
        return;
    default:
        return;
    }
    if (m_keyboardGrab.mode == CursorMode::Resize && !m_keyboardGrab.edgeChosen) {
        // Il primo tasto freccia sceglie il bordo da muovere.
        const wlr_box frame = toplevel->frameBox();
        const double x = edge == WLR_EDGE_LEFT ? frame.x : edge == WLR_EDGE_RIGHT ? frame.x + frame.width : frame.x + frame.width / 2.0;
        const double y = edge == WLR_EDGE_TOP ? frame.y : edge == WLR_EDGE_BOTTOM ? frame.y + frame.height : frame.y + frame.height / 2.0;
        wlr_cursor_warp(cursor, nullptr, x, y);
        beginInteractive(toplevel, CursorMode::Resize, edge, /*fromModifier=*/true);
        m_keyboardGrab.edgeChosen = true;
        return;
    }
    wlr_cursor_move(cursor, nullptr, dx, dy);
    onCursorMotion(0);
}

void Server::finishKeyboardGrab(bool confirm)
{
    Toplevel* toplevel = m_keyboardGrab.toplevel;
    if (!toplevel) {
        return;
    }
    if (!confirm) {
        // Esc: torna com'era.
        toplevel->tree->setPosition(m_keyboardGrab.treeX, m_keyboardGrab.treeY);
        if (m_keyboardGrab.mode == CursorMode::Resize && m_keyboardGrab.edgeChosen) {
            toplevel->configureSize(m_keyboardGrab.geometry.width, m_keyboardGrab.geometry.height);
        }
    }
    endSnapZone(false);
    m_keyboardGrab = {};
    cursorMode = CursorMode::Passthrough;
    grabbed = nullptr;
    wlr_cursor_set_xcursor(cursor, cursorManager, "default");
    onCursorMotion(0);
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
