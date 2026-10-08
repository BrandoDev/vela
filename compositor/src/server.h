// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SERVER_H
#define VELA_SERVER_H

// Il compositor: display Wayland, backend, renderer, scena e tutto ciò che
// vive quanto la sessione. Lo crea main.c e lo distrugge alla fine.
//
// La struttura tiene gli oggetti globali e i puntatori ai sottosistemi; ogni
// sottosistema ha il suo modulo, che lo crea e lo distrugge (server.c dice
// in che ordine).

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "scene/ready.h"
#include "shell.h"

struct vela_a11y;
struct vela_commands;
struct vela_icons;
struct vela_input;
struct vela_interaction;
struct vela_layer_surface;
struct vela_snapping;
struct vela_switcher;
struct vela_lock;
struct vela_text;
struct vela_output;
struct vela_renderer;
struct vela_scene;
struct vela_tree;
struct vela_view;
struct vela_vulkan;
struct vela_workspaces;
struct wlr_allocator;
struct wlr_compositor;
struct wlr_ext_foreign_toplevel_list_v1;
struct wlr_foreign_toplevel_manager_v1;
struct wlr_xdg_shell;
struct wlr_xwayland;
struct wlr_backend;
struct wlr_cursor;
struct wlr_output_layout;
struct wlr_renderer;
struct wlr_seat;
struct wlr_layer_shell_v1;
struct wlr_output_manager_v1;
struct wlr_session;
struct wlr_xcursor_manager;

// Gli strati della scena, dal basso verso l'alto. L'ordine di creazione è
// quello di impilamento.
struct vela_layers {
    struct vela_tree *background;
    struct vela_tree *bottom;
    struct vela_tree *windows;
    // Le finestre del desktop che si lascia, mentre scivolano via: sopra le
    // finestre e sotto i pannelli.
    struct vela_tree *windows_out;
    struct vela_tree *top;
    struct vela_tree *fullscreen;
    // I pannelli dello strato "top" che si richiamano (menu Start,
    // impostazioni rapide, Esegui...): come su Windows compaiono anche
    // sopra un gioco a schermo intero, che invece copre la taskbar.
    struct vela_tree *top_above_fullscreen;
    struct vela_tree *x11_popups; // menu e tooltip delle app X11
    struct vela_tree *overlay;
    struct vela_tree *drag; // l'icona di ciò che si trascina tra le app
    struct vela_tree *lock; // schermata di blocco, sopra tutto
};

// Dov'era uno schermo l'ultima volta (view.c: le finestre tornano al loro
// schermo quando lui torna).
struct vela_seen_output {
    char name[64];
    struct wlr_box box;
};

// Cosa fa il puntatore: niente di speciale, sposta o ridimensiona una
// finestra.
enum vela_cursor_mode {
    VELA_CURSOR_PASSTHROUGH,
    VELA_CURSOR_MOVE,
    VELA_CURSOR_RESIZE,
};

struct vela_server {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wlr_backend *backend;
    struct wlr_session *session; // solo nella sessione vera (DRM): cambio di TTY
    // Il renderer di Vela (docs/renderer.md): device Vulkan nostro, il
    // renderer (anche wlr_renderer per wlroots) e l'allocatore GBM dei
    // buffer di schermi, cursori e catture.
    struct vela_vulkan *vulkan;
    struct vela_renderer *renderer; // appartiene a wlroots: vedi vela_server_destroy
    struct wlr_renderer *wlr_renderer; // lo stesso, visto da wlroots
    struct wlr_allocator *allocator;
    struct wlr_output_layout *output_layout;
    struct vela_scene *scene;
    struct wl_list outputs; // vela_output.link
    // Frequenza di aggiornamento variabile (VRR): 0 mai, 1 con un'app a
    // schermo intero (i giochi), 2 sempre. vela.conf "variable-refresh".
    int vrr_mode;
    bool nested; // dentro un'altra sessione (finestra Wayland o X11)
    char socket_name[64]; // il nostro WAYLAND_DISPLAY

    struct vela_layers layers;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_list layer_surfaces; // vela_layer_surface.link (layer.h)
    struct wl_listener new_layer_surface;
    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_manager;
    enum vela_cursor_mode cursor_mode;
    struct vela_input *input; // dispositivi, tastiere, gesti (input.h)
    struct vela_a11y *a11y; // luce notturna, filtri, lente, tasti permanenti (a11y.h)
    struct vela_lock *lock; // blocco dello schermo e inattività (lock.h)
    // Bloccato (ext-session-lock-v1): si vede solo il programma di blocco.
    bool locked;

    // Il tema scelto nelle Impostazioni ("Scegli la modalità"), mandato
    // dalla shell: chiara la shell (tinta acrylic) e chiare le app (barre
    // del titolo).
    bool light_shell;
    bool light_apps;
    // La tinta dello sfondo del desktop (Mica, docs/renderer.md §9.4): il
    // colore medio, in sRGB, mandato dalla shell. La versione cambia con
    // lei e con il tema: le barre del titolo si ridisegnano.
    bool has_wallpaper_tint;
    float wallpaper_tint[3];
    uint32_t wallpaper_tint_version;
    // Testo e icone delle barre del titolo, caricati al primo uso
    // (decoration.c); text_loaded: tentato, anche se non c'è un font.
    struct vela_text *text;
    bool text_loaded;
    struct vela_icons *icons;
    // Il tempo delle animazioni: l'istante in cui diventerà luce il frame
    // che si sta preparando (docs/renderer.md §4.2). Non torna mai indietro,
    // anche se schermi diversi prevedono istanti diversi; 0 prima del primo.
    double animation_now_ms;
    struct wl_list snapshot_animations; // le animazioni delle istantanee (snapshot.h)

    // Le finestre (view.h): in ordine di uso recente, la prima è quella
    // attiva; i protocolli che le portano.
    struct wl_list views; // vela_view.link
    struct wlr_compositor *compositor;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevels;
    struct wlr_ext_foreign_toplevel_list_v1 *ext_toplevels;
    struct wl_listener new_toplevel;
    struct wl_listener new_capture_request;
    struct wl_listener new_decoration;
    struct wl_listener request_activation;
    struct wlr_xwayland *xwayland; // NULL senza Xwayland
    struct wl_listener xwayland_ready;
    struct wl_listener xwayland_new_surface;
    struct wl_event_source *output_check;
    struct vela_seen_output *seen_outputs;
    int seen_output_count;
    // La finestra che il puntatore sta spostando o ridimensionando
    // (cursor_mode), o NULL.
    struct vela_view *grabbed;
    struct vela_workspaces *workspaces; // desktop virtuali (workspace.h)
    // La tastiera ai pezzi della shell (focus.h): quello che ce l'ha e
    // quello a cui torna quando quello sopra si chiude.
    struct vela_layer_surface *focused_layer;
    struct vela_layer_surface *previous_layer;
    struct vela_interaction *interaction; // il puntatore sulle finestre (interact.h)
    struct vela_switcher *switcher; // Alt+Tab (switcher.h)
    struct vela_snapping *snapping; // anteprima, assist, layout di snap (snap.h)

    // Commit delle app trattenuti finché la loro GPU non ha finito (§7.3).
    struct vela_ready ready;
    struct vela_shell shell; // la shell lanciata e sorvegliata (shell.h)
    struct vela_commands *commands; // il socket dei comandi (command.h)
    // wlr-output-management (output_manager.h).
    struct wlr_output_manager_v1 *output_manager;
    struct wl_listener output_manager_apply;
    struct wl_listener output_manager_test;
    struct wl_listener new_output;
    struct wl_listener layout_change;
};

// Crea il display, il backend, il renderer e i protocolli. NULL se manca
// qualcosa di indispensabile (il motivo è già nel log).
struct vela_server *vela_server_create(void);

// Apre il socket Wayland, avvia il backend e lancia `startup_command` (la
// shell, rilanciata se si chiude male; NULL o vuoto: niente).
bool vela_server_start(struct vela_server *server, const char *startup_command);

// Il ciclo degli eventi, fino all'uscita.
void vela_server_run(struct vela_server *server);

// Chiude i client e libera tutto.
void vela_server_destroy(struct vela_server *server);


// Uno schermo se ne va: pannelli chiusi, anteprime e lente spente, il
// blocco non aspetta più il suo frame.
void vela_server_output_destroyed(struct vela_server *server, struct vela_output *output);

// Prima di ogni frame: le animazioni avanzano all'istante in cui il frame
// diventerà luce (docs/renderer.md §4.2).
void vela_server_animate(struct vela_server *server, int64_t present_ns);

// A ogni frame consegnato (il blocco aspetta il nero su ogni schermo).
void vela_server_output_rendered(struct vela_server *server, struct vela_output *output);

// Un frame su ogni schermo (qualcosa si anima o è cambiato).
void vela_server_schedule_frames(struct vela_server *server);



#endif
