// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_VIEW_H
#define VELA_VIEW_H

// Una finestra: di un'app Wayland (xdg-shell) o X11 (Xwayland). Tutto ciò
// che Vela fa con le finestre (animazioni, snap, taskbar, Alt+Tab) passa da
// qui; le poche cose che dipendono dal tipo sono nelle funzioni "verso
// l'app" (view.c per Wayland, xwayland.c per X11).
//
// Le finestre stanno in vela_server.views in ordine di uso recente: la
// prima è quella attiva. Una finestra nasce con la sua superficie e muore
// con lei; la lista la contiene solo mentre è "mappata" (visibile all'utente).

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "motion.h"
#include "snap.h"

struct vela_decoration;
struct vela_output;
struct vela_server;
struct vela_surface_node;
struct vela_tree;
struct vela_window_capture;
struct wlr_ext_foreign_toplevel_handle_v1;
struct wlr_ext_image_capture_source_v1;
struct wlr_foreign_toplevel_handle_v1;
struct wlr_surface;
struct wlr_xdg_toplevel;
struct wlr_xdg_toplevel_decoration_v1;
struct wlr_xwayland_surface;

// Chi possiede un albero della scena (il suo vela_node.data): una finestra
// o un pezzo della shell. È il primo campo delle loro strutture, così dal
// risultato di vela_scene_at si risale a loro.
enum vela_owner_kind {
    VELA_OWNER_VIEW,
    VELA_OWNER_LAYER, // struct vela_layer_surface (layer.h)
};

struct vela_owner {
    enum vela_owner_kind kind;
};

struct vela_view {
    struct vela_owner owner; // primo campo: il vela_node.data dell'albero
    struct wl_list link; // vela_server.views, finché è mappata
    struct vela_server *server;
    struct wlr_xdg_toplevel *xdg; // una delle due
    struct wlr_xwayland_surface *x11;
    // Origine dell'albero: quella della superficie (non della geometria,
    // che può avere un margine per l'ombra).
    struct vela_tree *tree;
    struct vela_surface_node *surface_node;

    bool mapped;
    bool activated; // la finestra attiva (tastiera)
    bool maximized;
    bool fullscreen;
    bool minimized;
    struct vela_snap snap; // dove sta, se agganciata (snap.h)
    // Gruppo di snap, come Windows 11: le finestre sistemate insieme con
    // Snap Assist; la taskbar le mostra e le riporta davanti insieme. 0:
    // nessuno. Si esce staccandosi (o chiudendo).
    uint32_t snap_group;
    // Desktop virtuali (workspace.c): quello della finestra, o tutti.
    int workspace;
    bool sticky;
    uint64_t map_serial; // ordine di apertura, per la taskbar
    struct wlr_box restore; // posizione e dimensione prima di massimizzare o agganciare
    // Lo schermo da cui è stata spostata perché scollegato (vuoto: nessuno)
    // e dove stava lì: quando lo schermo torna, ci torna anche lei.
    char home_output[64];
    int home_x;
    int home_y;
    struct wlr_box taskbar_rect; // il suo pulsante nella taskbar (globali), se noto

    // Come la taskbar vede e comanda questa finestra (foreign-toplevel).
    // Esiste solo mentre la finestra è mappata e sul desktop in uso.
    struct wlr_foreign_toplevel_handle_v1 *handle;
    // La stessa finestra nel protocollo ext-foreign-toplevel-list: ha un
    // identificativo univoco e serve per catturarne l'immagine (Alt+Tab).
    // Esiste mentre la finestra è mappata.
    struct wlr_ext_foreign_toplevel_handle_v1 *ext_handle;
    // Per catturarne l'immagine (anteprime di Alt+Tab): il nostro renderer
    // disegna solo lei.
    struct vela_window_capture *capture;
    // La barra del titolo di Vela (decoration.h), se la finestra la usa, e
    // la richiesta xdg-decoration dell'app.
    struct vela_decoration *decoration;
    struct wlr_xdg_toplevel_decoration_v1 *xdg_decoration;

    // Animazione di apertura.
    bool animating;
    bool close_animated; // istantanea di chiusura già scattata
    struct vela_tween open_tween;
    int open_frames;
    double target_x;
    double target_y;

    // X11: dimensione chiesta all'app (0: la sua), ultima geometria
    // comunicata e quella chiesta dall'app prima di comparire.
    int x11_width;
    int x11_height;
    struct wlr_box x11_sent;
    struct wlr_box x11_initial;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener client_commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_minimize;
    struct wl_listener request_window_menu;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
    struct wl_listener set_parent;
    struct wl_listener new_popup;
    struct wl_listener decoration_mode;
    struct wl_listener decoration_destroy;
    // Solo X11.
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener request_configure;
    struct wl_listener request_activate;
    struct wl_listener set_decorations;
    // Le richieste della taskbar attraverso la maniglia.
    struct wl_listener handle_activate;
    struct wl_listener handle_close;
    struct wl_listener handle_maximize;
    struct wl_listener handle_minimize;
    struct wl_listener handle_fullscreen;
    struct wl_listener handle_rectangle;
};

// I protocolli delle finestre Wayland (xdg-shell, xdg-decoration,
// foreign-toplevel, xdg-activation) e la lista vela_server.views.
void vela_views_init(struct vela_server *server);
void vela_views_finish(struct vela_server *server);

// La finestra attiva (quella con la tastiera), o NULL.
struct vela_view *vela_views_focused(struct vela_server *server);
// Un dialogo di sistema visibile che aspetta una risposta, o NULL.
struct vela_view *vela_views_system_prompt(struct vela_server *server);

// ------------------------------------------------------------- geometria --

// La parte visibile, rispetto all'origine della superficie (le app
// Wayland possono disegnare un margine d'ombra attorno), barra di Vela
// compresa.
struct wlr_box vela_view_geometry(const struct vela_view *view);
// Il riquadro visibile della finestra (barra di Vela compresa), in
// coordinate globali logiche.
struct wlr_box vela_view_frame_box(struct vela_view *view);
// Lo schermo su cui sta il centro della finestra, o quello del cursore.
struct vela_output *vela_view_output(struct vela_view *view);
struct vela_tree *vela_view_tree(struct vela_view *view);
// Dove "va" quando si riduce a icona: il suo pulsante nella taskbar.
struct wlr_box vela_view_minimize_target(struct vela_view *view);
// Dove torna uscendo da massimizzata o schermo intero.
struct wlr_box vela_view_restore_box(struct vela_view *view);
// Altezza della barra del titolo di Vela (logica), 0 se la finestra non ce
// l'ha o è a schermo intero.
int vela_view_title_bar_height(const struct vela_view *view);

// ----------------------------------------------------------------- stati --

// animate: false quando la finestra esce dalla massimizzazione perché la
// si trascina (segue già il cursore).
void vela_view_set_maximized(struct vela_view *view, bool on, bool animate);
void vela_view_apply_maximized(struct vela_view *view);
void vela_view_set_fullscreen(struct vela_view *view, bool on);
// Come su Windows: la finestra sparisce, va in fondo all'ordine di Alt+Tab
// e la tastiera passa alla successiva.
void vela_view_set_minimized(struct vela_view *view, bool on);
// Fine dell'animazione di ripristino: la finestra vera ricompare.
void vela_view_finish_restore(struct vela_view *view);
void vela_view_set_activated(struct vela_view *view, bool on); // per l'app e per la taskbar
// Massimizzata, agganciata o a schermo intero: la riallinea al suo schermo.
void vela_view_keep_in_place(struct vela_view *view);
// La porta su `output` in (x, y) (rimessa dentro l'area utile,
// massimizzata, agganciata o a schermo intero come prima).
void vela_view_move_to_output(struct vela_view *view, struct vela_output *output, int x, int y);
// Opacità di tutta la finestra (apertura, massimizza e ripristina).
void vela_view_set_opacity(struct vela_view *view, float opacity);
void vela_view_finish_open_animation(struct vela_view *view);
// Le animazioni di apertura all'istante `now_ms`: true se ne restano.
bool vela_views_tick(struct vela_server *server, double now_ms);
bool vela_views_animating(struct vela_server *server);
// Angoli arrotondati e ombra (docs/renderer.md §8), come Windows 11: non da
// massimizzata, a schermo intero o agganciata, né per le app che
// disegnano da sé ombra e bordi (margini fuori dalla geometria, es. GTK).
void vela_view_update_shape(struct vela_view *view);
// Crea o toglie la barra di Vela secondo ciò che la finestra chiede, o la
// riallinea alla finestra.
void vela_view_update_decoration(struct vela_view *view);
void vela_view_refresh_decoration(struct vela_view *view);
// La taskbar mostra solo le finestre del desktop in uso.
void vela_view_show_in_taskbar(struct vela_view *view, bool on);
// Per catturarne l'immagine (Alt+Tab): l'aspetto attuale, anche se ridotta
// a icona o coperta.
struct wlr_ext_image_capture_source_v1 *vela_view_prepare_capture(struct vela_view *view);

// ----------------------------------------------------------- verso l'app --

struct wlr_surface *vela_view_surface(const struct vela_view *view); // NULL finché X11 non è associata
bool vela_view_configurable(const struct vela_view *view); // può già ricevere dimensioni e stati
const char *vela_view_title(const struct vela_view *view); // mai NULL
const char *vela_view_app_id(const struct vela_view *view); // mai NULL (X11: la classe)
// Un dialogo di sistema che aspetta una risposta (portachiavi, password di
// amministratore, PIN di GnuPG): sta sopra le finestre normali.
bool vela_view_is_system_prompt(const struct vela_view *view);
bool vela_view_resizable(const struct vela_view *view); // no se min = max
pid_t vela_view_pid(const struct vela_view *view); // il processo (per "Termina attività")
// Dimensione del riquadro intero (barra compresa); 0x0: la sceglie l'app.
void vela_view_configure_size(struct vela_view *view, int width, int height);
void vela_view_send_maximized(struct vela_view *view, bool on);
void vela_view_send_tiled(struct vela_view *view, uint32_t edges);
void vela_view_close(struct vela_view *view);
// X11: le app devono sapere dove sta la finestra sullo schermo (per
// posizionare i propri menu). Si chiama a ogni frame.
void vela_view_sync_x11(struct vela_view *view);

// --------------------------------------------------------- schermi e scena --

// L'area utile di uno schermo è cambiata: le finestre massimizzate e
// agganciate su di lui si risistemano.
void vela_views_usable_changed(struct vela_server *server, struct vela_output *output);
// Un'app a schermo intero, visibile, su quello schermo (VRR "games").
bool vela_views_fullscreen_on(struct vela_server *server, struct vela_output *output);
// Schermi collegati e scollegati: le finestre di uno schermo che sparisce
// vanno su un altro, e tornano quando lui torna. "later": dopo che tutto
// si è sistemato (uno schermo che sparisce lascia il layout prima di
// essere distrutto), al prossimo giro del ciclo.
void vela_views_check_outputs_later(struct vela_server *server);
void vela_views_check_outputs(struct vela_server *server);

// ----------------------------------------------- per view.c e xwayland.c --

// Il giro di vita comune: comparsa, sparizione, commit (view.c).
void vela_view_init(struct vela_view *view, struct vela_server *server);
void vela_view_mapped(struct vela_view *view);
void vela_view_unmapped(struct vela_view *view);
void vela_view_committed(struct vela_view *view);
void vela_view_destroy(struct vela_view *view);
void vela_view_title_changed(struct vela_view *view);
void vela_view_app_id_changed(struct vela_view *view);
void vela_view_parent_changed(struct vela_view *view);
// Le app X11 (xwayland.c).
void vela_xwayland_init(struct vela_server *server);
void vela_xwayland_finish(struct vela_server *server);
void vela_xwayland_sync(struct vela_server *server); // a ogni frame: le app X11 sanno dove sono

#endif
