// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server.h"

#include "a11y.h"
#include "command.h"
#include "config.h"
#include "input.h"
#include "interact.h"
#include "layer.h"
#include "lock.h"
#include "output.h"
#include "output_manager.h"
#include "render/renderer.h"
#include "scene/effects.h"
#include "scene/frame.h"
#include "scene/scene.h"
#include "session.h"
#include "snap.h"
#include "snapshot.h"
#include "switcher.h"
#include "text.h"
#include "icons.h"
#include "util.h"
#include "view.h"
#include "workspace.h"

#include <errno.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/config.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>
#if WLR_HAS_X11_BACKEND
#include <wlr/backend/x11.h>
#endif
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#endif

static int handle_terminate(int signal, void *data)
{
    wl_display_terminate(data);
    return 0;
}

// Per il debug del danno: `kill -USR1` fa ridisegnare tutto da capo. Se
// l'immagine cambia, il danno aveva lasciato pixel vecchi.
static int handle_redraw_all(int signal, void *data)
{
    struct vela_server *server = data;
    struct vela_output *output;
    wl_list_for_each (output, &server->outputs, link) {
        vela_output_frame_reset_damage(output->frame);
    }
    return 0;
}

static void find_windowed(struct wlr_backend *child, void *data)
{
    bool windowed = wlr_backend_is_wl(child);
#if WLR_HAS_X11_BACKEND
    windowed = windowed || wlr_backend_is_x11(child);
#endif
    if (windowed) {
        *(bool *)data = true;
    }
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct vela_server *server = wl_container_of(listener, server, new_output);
    vela_output_create(server, data);
}

// Ogni cambiamento (schermo collegato, spostato, nuova modalità) si
// racconta ai programmi di configurazione.
static void handle_layout_change(struct wl_listener *listener, void *data)
{
    struct vela_server *server = wl_container_of(listener, server, layout_change);
    vela_output_manager_update(server);
    vela_lock_update_layout(server->lock);
    vela_views_check_outputs_later(server);
}

// Il ciclo dei frame deve svegliarsi all'istante giusto anche con la CPU
// piena (una compilazione, un gioco): scheduling realtime, a priorità
// bassa, per il thread principale, come KWin. Chi viene lanciato da Vela
// (shell, app) non lo eredita. Serve RLIMIT_RTPRIO o CAP_SYS_NICE;
// VELA_REALTIME=0 lo spegne.
static void make_realtime(void)
{
    const char *realtime = getenv("VELA_REALTIME");
    if (realtime && strcmp(realtime, "0") == 0) {
        return;
    }
    struct sched_param param = { .sched_priority = vela_min(10, sched_get_priority_max(SCHED_RR)) };
    if (sched_setscheduler(0, SCHED_RR | SCHED_RESET_ON_FORK, &param) == 0) {
        wlr_log(WLR_INFO, "Main thread is realtime (SCHED_RR, priority %d)", param.sched_priority);
    } else {
        wlr_log(WLR_INFO, "No realtime scheduling (%s): frames may be late under load", strerror(errno));
    }
}

// Display, backend, renderer e scena; false se manca qualcosa di
// indispensabile (il motivo è nel log).
static bool init(struct vela_server *server)
{
    // vela.conf con i nomi inglesi, se ha ancora quelli italiani di prima.
    vela_config_migrate();
    wl_list_init(&server->outputs);
    wl_list_init(&server->new_output.link);
    wl_list_init(&server->layout_change.link);
    server->shell.pid = -1;
    server->shell.pidfd = -1;
    vela_snapshots_init(server);
    server->interaction = vela_interaction_create();
    server->switcher = vela_switcher_create();
    server->snapping = vela_snapping_create();
    server->vrr_mode = 1;
    server->display = wl_display_create();
    server->loop = wl_display_get_event_loop(server->display);
    struct wl_display *display = server->display;

    // Sceglie da solo il backend: DRM/KMS da una TTY, oppure una finestra
    // Wayland se lanciato dentro un'altra sessione (es. KDE) per i test.
    server->backend = wlr_backend_autocreate(server->loop, &server->session);
    if (!server->backend) {
        wlr_log(WLR_ERROR, "Can't create the backend");
        return false;
    }
    wlr_multi_for_each_backend(server->backend, find_windowed, &server->nested);

    // Il renderer di Vela (docs/renderer.md): solo Vulkan 1.4, sul device
    // della GPU che pilota gli schermi. Senza, Vela non parte e il log dice
    // perché.
    server->vulkan = vela_vulkan_create(wlr_backend_get_drm_fd(server->backend));
    if (server->vulkan) {
        server->renderer = vela_renderer_create(server->vulkan);
    }
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "Vela needs a GPU with Vulkan 1.4 and dmabuf support: can't draw");
        return false;
    }
    server->wlr_renderer = vela_renderer_wlr(server->renderer);
    server->allocator = vela_gbm_allocator_create(vela_vulkan_render_fd(server->vulkan));
    if (!server->allocator) {
        wlr_log(WLR_ERROR, "Can't create the buffer allocator (GBM)");
        return false;
    }
    wlr_renderer_init_wl_shm(server->wlr_renderer, display);
    // Buffer GPU condivisi con le app, senza copie: i formati sono quelli
    // che il nostro device sa leggere.
    struct wlr_linux_dmabuf_v1 *dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(display, 4, server->wlr_renderer);
    // Sincronizzazione esplicita (§7.3): le app dicono quando il buffer è
    // pronto e noi quando l'abbiamo finito di leggere, con timeline del
    // kernel invece delle fence implicite dei dmabuf. Le usano Vulkan (Mesa,
    // NVIDIA) e i giochi. WLR_RENDER_NO_EXPLICIT_SYNC=1 la spegne, come nel
    // resto di wlroots.
    const char *no_explicit = getenv("WLR_RENDER_NO_EXPLICIT_SYNC");
    if (server->wlr_renderer->features.timeline && !(no_explicit && strcmp(no_explicit, "0") != 0)
        && wlr_linux_drm_syncobj_manager_v1_create(display, 1, vela_vulkan_render_fd(server->vulkan))) {
        wlr_log(WLR_INFO, "Explicit sync with apps (linux-drm-syncobj-v1) enabled");
    }

    // Protocolli di base che quasi ogni applicazione moderna si aspetta.
    // Con il renderer, wlroots carica i buffer delle app nelle nostre
    // texture a ogni commit (solo la parte cambiata).
    server->compositor = wlr_compositor_create(display, 6, server->wlr_renderer);
    vela_ready_init(&server->ready, server->compositor);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);
    wlr_primary_selection_v1_device_manager_create(display);
    wlr_data_control_manager_v1_create(display);
    // Gli appunti per chi non ha una finestra: la cronologia degli appunti
    // della shell (Win+V) e lo Strumento di cattura.
    wlr_ext_data_control_manager_v1_create(display, 1);
    wlr_viewporter_create(display);
    wlr_single_pixel_buffer_manager_v1_create(display);
    wlr_fractional_scale_manager_v1_create(display, 1);
    wlr_screencopy_manager_v1_create(display);
    wlr_presentation_create(display, server->backend, 2);

    server->output_layout = wlr_output_layout_create(display);
    wlr_xdg_output_manager_v1_create(display, server->output_layout);
    vela_output_manager_init(server);
    server->layout_change.notify = handle_layout_change;
    wl_signal_add(&server->output_layout->events.change, &server->layout_change);

    server->scene = vela_scene_create();
    vela_scene_watch(server->scene, server->compositor);
    server->scene->linux_dmabuf = dmabuf;
    server->scene->event_loop = server->loop;

    // L'ordine di creazione è l'ordine di impilamento (dal basso).
    struct vela_tree *root = server->scene->root;
    struct vela_layers *layers = &server->layers;
    layers->background = vela_tree_create(root);
    layers->bottom = vela_tree_create(root);
    layers->windows = vela_tree_create(root);
    layers->top = vela_tree_create(root);
    layers->fullscreen = vela_tree_create(root);
    layers->top_above_fullscreen = vela_tree_create(root);
    layers->x11_popups = vela_tree_create(root);
    layers->overlay = vela_tree_create(root);
    // Sopra le finestre e sotto i pannelli: il desktop che si lascia.
    layers->windows_out = vela_tree_create(root);
    vela_node_place_above(&layers->windows_out->node, &layers->windows->node);
    layers->windows_out->node.ignores_input = true;
    layers->drag = vela_tree_create(root);
    layers->drag->node.ignores_input = true;
    layers->lock = vela_tree_create(root);

    server->new_output.notify = handle_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    // Le finestre: xdg-shell, xdg-decoration, foreign-toplevel, cattura,
    // attivazione (view.c).
    vela_views_init(server);
    // La sfocatura dietro i pannelli e le app che la chiedono (§8.3).
    vela_background_effects_init(display, server->scene);
    vela_layers_init(server); // i pezzi della shell (layer.c)
    // Seat, cursore, mouse, tastiere, gesti (input.c).
    server->input = vela_input_create(server);

    wl_event_loop_add_signal(server->loop, SIGUSR1, handle_redraw_all, server);
    vela_xwayland_init(server); // le app X11 (xwayland.c)
    server->lock = vela_lock_create(server); // blocco e inattività (lock.c)
    // Accessibilità e colore dello schermo (a11y.c); VRR e tearing.
    server->scene->tearing_control = wlr_tearing_control_manager_v1_create(display, 1);
    server->a11y = vela_a11y_create(server);
    vela_output_load_settings(server);
    vela_a11y_load(server->a11y);
    server->workspaces = vela_workspaces_create(server); // desktop virtuali (workspace.c)

    wl_event_loop_add_signal(server->loop, SIGINT, handle_terminate, display);
    wl_event_loop_add_signal(server->loop, SIGTERM, handle_terminate, display);
    make_realtime();
    return true;
}

struct vela_server *vela_server_create(void)
{
    struct vela_server *server = calloc(1, sizeof(*server));
    if (!init(server)) {
        // Ciò che è già nato resta al processo, che sta per finire.
        free(server);
        return NULL;
    }
    return server;
}

bool vela_server_start(struct vela_server *server, const char *startup_command)
{
    // Nella sessione supervisionata il socket lo tiene il supervisore, che
    // lo passa a ogni compositor che avvia (supervisor.c).
    const char *socket = NULL;
    char given[64] = "";
    const char *socket_fd = getenv("VELA_WAYLAND_SOCKET_FD");
    const char *socket_display = getenv("VELA_WAYLAND_DISPLAY");
    bool supervised = socket_fd && socket_display;
    if (supervised) {
        snprintf(given, sizeof(given), "%s", socket_display);
        if (wl_display_add_socket_fd(server->display, atoi(socket_fd)) == 0) {
            socket = given;
        }
        unsetenv("VELA_WAYLAND_SOCKET_FD");
        unsetenv("VELA_WAYLAND_DISPLAY");
    } else {
        socket = wl_display_add_socket_auto(server->display);
    }
    if (!socket) {
        wlr_log(WLR_ERROR, "Can't create the Wayland socket");
        return false;
    }
    snprintf(server->socket_name, sizeof(server->socket_name), "%s", socket);

    if (!wlr_backend_start(server->backend)) {
        wlr_log(WLR_ERROR, "Can't start the backend");
        return false;
    }

    setenv("WAYLAND_DISPLAY", socket, 1);
    // Solo nella sessione vera (da SDDM o da una console): annidati o
    // headless si resta ospiti dell'ambiente che c'è.
    if (server->session) {
        vela_session_set_environment();
    }
    // Le app X11 lanciate da qui vanno nel nostro Xwayland, non in quello
    // della sessione ospite (KDE) da cui magari siamo partiti.
#if WLR_HAS_XWAYLAND
    if (server->xwayland) {
        setenv("DISPLAY", server->xwayland->display_name, 1);
    } else {
        unsetenv("DISPLAY");
    }
#else
    unsetenv("DISPLAY");
#endif
    // Le app Qt si ricollegano al compositor nuovo se questo va in crash:
    // solo se c'è il supervisore che lo riavvia sullo stesso socket. Senza
    // (annidati, headless) fa danni: alla chiusura di Vela le app Qt provano
    // a riconnettersi e vanno in crash dentro Qt.
    if (supervised) {
        setenv("QT_WAYLAND_RECONNECT", "1", 1);
    } else {
        unsetenv("QT_WAYLAND_RECONNECT");
    }

    vela_commands_listen(server);
    if (server->session) {
        vela_session_run_hook("start");
    }

    wlr_log(WLR_INFO, "Vela running on WAYLAND_DISPLAY=%s%s", socket,
        getenv("VELA_RESTARTED") ? " (restarted after a crash)" : "");
    unsetenv("VELA_RESTARTED");
    // Era bloccato quando il compositor di prima è caduto: si riparte
    // bloccati, schermo nero finché vela-lock non si presenta.
    if (getenv("VELA_START_LOCKED")) {
        unsetenv("VELA_START_LOCKED");
        wlr_log(WLR_INFO, "The screen was locked: restarting locked");
        vela_lock_engage(server->lock);
        vela_lock_screen(server->lock);
    }
    if (server->nested) {
        wlr_log(WLR_INFO, "Nested mode: Alt shortcuts enabled");
    }
    if (startup_command && *startup_command) {
        vela_shell_start(server, startup_command);
    }
    return true;
}

void vela_server_run(struct vela_server *server)
{
    wl_display_run(server->display);
}

void vela_server_destroy(struct vela_server *server)
{
    // Stiamo chiudendo noi: la shell che se ne va non va rilanciata.
    vela_shell_stop(server);
    vela_commands_stop(server);
    if (server->session) {
        vela_session_run_hook("stop");
    }
    vela_xwayland_finish(server); // le finestre X11 prima dei client Wayland

    // Chiudere i client distrugge finestre e superfici della shell, che si
    // rimuovono da sole dalle nostre liste.
    wl_display_destroy_clients(server->display);

    // wlroots controlla che nessun listener resti attaccato agli oggetti che
    // distrugge: si staccano tutti quelli globali prima di procedere.
    vela_output_manager_finish(server);
    wl_list_remove(&server->layout_change.link);
    wl_list_remove(&server->new_output.link);
    wl_list_remove(&server->new_layer_surface.link);
    vela_views_finish(server);
    vela_workspaces_destroy(server->workspaces);
    server->workspaces = NULL;
    vela_snapshots_finish(server);
    vela_snapping_destroy(server->snapping);
    server->snapping = NULL;
    vela_switcher_destroy(server->switcher);
    server->switcher = NULL;
    vela_interaction_destroy(server->interaction);
    server->interaction = NULL;
    vela_input_destroy(server->input);
    server->input = NULL;
    vela_a11y_destroy(server->a11y);
    server->a11y = NULL;

    wlr_xcursor_manager_destroy(server->cursor_manager);
    wlr_cursor_destroy(server->cursor);
    wlr_backend_destroy(server->backend); // distrugge schermi e tastiere
    // Dopo gli schermi, che li usano: il blocco, gli strati e la scena (ogni
    // nodo, distrutto, avvisa la sua scena).
    vela_lock_destroy(server->lock);
    server->lock = NULL;
    struct vela_layers *l = &server->layers;
    struct vela_tree *trees[] = {
        l->background, l->bottom, l->windows, l->windows_out, l->top, l->fullscreen, l->top_above_fullscreen,
        l->x11_popups, l->overlay, l->drag, l->lock,
    };
    for (size_t i = 0; i < sizeof(trees) / sizeof(trees[0]); ++i) {
        vela_node_destroy(&trees[i]->node);
    }
    *l = (struct vela_layers) { 0 };
    vela_text_destroy(server->text);
    vela_icons_destroy(server->icons);
    vela_scene_destroy(server->scene);
    server->scene = NULL;
    vela_ready_finish(&server->ready);
    // Renderer, allocatore e device Vulkan si smontano solo per cercare
    // risorse dimenticate (VELA_VULKAN_VALIDATION=1). Alla chiusura normale
    // il processo sta per finire e il kernel recupera tutto: da annidati,
    // ogni tanto il driver amdgpu andava in crash liberando la memoria della
    // GPU (lo stato che RADV e il GBM di Mesa condividono nel processo
    // risultava già rovinato), e una sessione che si chiude non deve
    // sembrare un crash.
    if (vela_env_flag("VELA_VULKAN_VALIDATION")) {
        // Il renderer avvisa chi lo usa (wlr_compositor) e si distrugge.
        wlr_renderer_destroy(server->wlr_renderer);
        wlr_allocator_destroy(server->allocator);
        vela_vulkan_destroy(server->vulkan); // per ultimo: tutto il resto ne usa il device
    }
    wl_display_destroy(server->display);
    free(server);
}

// ---------------------------------------------------------- dagli schermi --

void vela_server_output_destroyed(struct vela_server *server, struct vela_output *output)
{
    // Le superfici della shell legate a questo schermo vanno chiuse.
    vela_layers_close_output(server, output);
    vela_snap_end_zone(server, false); // l'anteprima potrebbe essere su questo schermo
    vela_a11y_output_destroyed(server->a11y, output);
    // Un blocco in corso non deve più aspettare il nero su questo schermo.
    if (server->lock) {
        vela_lock_output_rendered(server->lock, output);
    }
}

void vela_server_output_rendered(struct vela_server *server, struct vela_output *output)
{
    if (server->lock) {
        vela_lock_output_rendered(server->lock, output);
    }
}

void vela_server_animate(struct vela_server *server, int64_t present_ns)
{
    vela_xwayland_sync(server);
    struct vela_view *view;
    wl_list_for_each (view, &server->views, link) {
        vela_view_update_shape(view); // angoli e ombra secondo lo stato di adesso
    }
    server->animation_now_ms = fmax(server->animation_now_ms, present_ns / 1e6);
    if (!vela_views_animating(server) && !vela_snapshots_running(server) && !vela_snap_preview_shown(server)
        && server->workspaces->direction == 0 && !vela_a11y_animating(server->a11y)) {
        return;
    }
    double now_ms = server->animation_now_ms;
    bool accessibility = vela_a11y_tick(server->a11y, now_ms);
    bool opening = vela_views_tick(server, now_ms);
    vela_snapshots_tick(server, now_ms);
    bool previewing = vela_snap_tick_preview(server, now_ms);
    bool switching = vela_workspaces_tick(server, now_ms);
    if (opening || vela_snapshots_running(server) || switching || accessibility || previewing) {
        vela_server_schedule_frames(server);
    }
}
