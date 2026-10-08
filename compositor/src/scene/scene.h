// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_SCENE_H
#define VELA_SCENE_SCENE_H

// La scena di Vela (docs/renderer.md §5): cosa c'è sullo schermo, dal basso
// verso l'alto. Prende il posto di wlr_scene.
//
// - Le superfici delle app si leggono dal vivo (§5.2): un nodo superficie
//   indica solo la superficie radice; sottosuperfici, buffer e stato
//   sincronizzato si leggono da wlroots al momento di disegnare.
// - I nodi nostri (alberi, rettangoli, istantanee) tengono solo ciò che è
//   nostro: posizione, visibilità, opacità.
// - Il danno non si calcola a ogni modifica: a ogni frame lo schermo
//   confronta ciò che disegna con il frame precedente (scene/frame.c). Qui
//   ogni modifica si limita a chiedere un nuovo frame a tutti gli schermi.
//
// Coordinate logiche (double): un'unità = un pixel a scala 100% (§3.1).
//
// Chi possiede cosa: ogni nodo lo crea e lo distrugge il suo possessore
// (una finestra, un pannello, l'animazione), con vela_*_create e
// vela_node_destroy. Distruggere un albero non distrugge i figli: restano
// orfani (fuori dalla scena, non si disegnano) finché il loro possessore
// non li distrugge a sua volta.

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/box.h>

struct vela_scene;
struct vela_tree;
struct wlr_buffer;
struct wlr_compositor;
struct wlr_linux_dmabuf_v1;
struct wlr_surface;
struct wlr_tearing_control_manager_v1;
struct wlr_texture;
struct wlr_xdg_popup;

enum vela_node_type {
    VELA_NODE_TREE,
    VELA_NODE_SURFACE,
    VELA_NODE_RECT,
    VELA_NODE_BUFFER,
};

struct vela_node {
    enum vela_node_type type;
    struct vela_scene *scene;
    struct vela_tree *parent; // NULL: orfano (o la radice)
    struct wl_list link; // nei figli del genitore, dal basso verso l'alto
    double x, y; // rispetto al genitore
    bool enabled;
    float opacity; // si moltiplica con quella degli antenati

    // Chi lo possiede (per sapere cosa c'è sotto il cursore).
    void *data;
    // Rettangoli e immagini di solito lasciano passare i clic (anteprime,
    // istantanee); quelli della barra del titolo no.
    bool hittable;
    // Si vede ma non prende input, lui e i figli (es. l'icona trascinata,
    // che sta sotto il cursore e non deve coprire dove la si lascia).
    bool ignores_input;
    // I figli non ereditano il ritaglio della forma di un antenato (i popup:
    // un menu può uscire dalla finestra).
    bool unclipped;
};

// La forma di un albero (docs/renderer.md §8): i figli ritagliati in un
// rettangolo arrotondato, con l'ombra sotto. Coordinate logiche, rispetto
// all'albero.
struct vela_shape {
    bool enabled;
    double x, y, width, height;
    double radius;
    bool shadow;
    bool active; // la finestra attiva ha l'ombra più marcata
};

struct vela_tree {
    struct vela_node node;
    struct wl_list children; // vela_node.link, dal basso verso l'alto
    struct vela_shape shape;
};

// Una superficie di un'app con le sue sottosuperfici, lette dal vivo.
struct vela_surface_node {
    struct vela_node node;
    struct wlr_surface *surface;
};

// Un rettangolo a tinta unita (colore sRGB premoltiplicato, come wlroots).
struct vela_rect_node {
    struct vela_node node;
    double width, height;
    struct wlr_render_color color;
};

// Un buffer "congelato" (pezzo di un'istantanea, testo della barra): resta
// valido anche se l'app lo cambia o si chiude, perché è bloccato finché il
// nodo esiste. La texture deve vivere quanto il buffer.
struct vela_buffer_node {
    struct vela_node node;
    struct wlr_buffer *buffer;
    struct wlr_texture *texture;
    struct wlr_fbox src;
    enum wl_output_transform transform;
    double width, height;
};

// La scena: la radice, gli schermi che la disegnano e le scelte che valgono
// per tutti. Una sola, del server.
struct vela_scene {
    struct vela_tree *root;
    struct wl_list frames; // vela_output_frame.link: gli schermi attivi
    // Per il feedback dmabuf per superficie (§5.3); può mancare.
    struct wlr_linux_dmabuf_v1 *linux_dmabuf;
    // Per i punti di rilascio della sincronizzazione esplicita.
    struct wl_event_loop *event_loop;
    // La tinta acrylic delle sfocature (sRGB premoltiplicato): quella del
    // tema scuro di Windows 11, o del chiaro se la shell è chiara.
    struct wlr_render_color acrylic_tint;
    // I filtri colore di tutti gli schermi: matrice 3x3 per righe in spazio
    // lineare, se color_filtered. Si applicano nel disegno.
    bool color_filtered;
    float color_filter[9];
    // La Luce notturna: quanto resta di rosso, verde e blu (luce lineare).
    // Sugli schermi veri va nella gamma del monitor (fuori dal disegno:
    // niente tinta negli screenshot, scanout diretto salvo); dove non si
    // può, nel disegno come i filtri. La versione cambia con i valori.
    bool night_active;
    float night_gains[3];
    uint32_t night_version;
    // wp-tearing-control: le app a schermo intero che chiedono di mostrare
    // ogni frame subito, anche a metà schermo (giochi). Si concede se
    // allow_tearing (vela.conf "tearing").
    struct wlr_tearing_control_manager_v1 *tearing_control;
    bool allow_tearing;

    struct wl_listener new_surface;
};

struct vela_scene *vela_scene_create(void);
// Prima vanno distrutti gli schermi (i loro frame) e i nodi dei possessori.
void vela_scene_destroy(struct vela_scene *scene);

// Da chiamare a ogni cambiamento: chiede un frame a tutti gli schermi.
void vela_scene_changed(struct vela_scene *scene);

// Le superfici che si impegnano ad arrivare su schermo passano da qui: il
// danno dei loro commit va agli schermi che le mostrano.
void vela_scene_watch(struct vela_scene *scene, struct wlr_compositor *compositor);

// Cosa c'è in un punto del layout (solo ciò che accetta input).
struct vela_hit {
    struct wlr_surface *surface; // NULL: un nodo nostro (la barra del titolo)
    double sx, sy;
    void *owner; // il `data` più vicino risalendo l'albero
};
struct vela_hit vela_scene_at(struct vela_scene *scene, double lx, double ly);

struct vela_tree *vela_tree_create(struct vela_tree *parent);
struct vela_surface_node *vela_surface_node_create(struct vela_tree *parent, struct wlr_surface *surface);
struct vela_rect_node *vela_rect_node_create(struct vela_tree *parent, double width, double height,
    const struct wlr_render_color *color);
// `buffer` viene bloccato finché il nodo esiste.
struct vela_buffer_node *vela_buffer_node_create(struct vela_tree *parent, struct wlr_buffer *buffer,
    struct wlr_texture *texture, const struct wlr_fbox *src, enum wl_output_transform transform, double width,
    double height);
void vela_node_destroy(struct vela_node *node);

void vela_node_set_position(struct vela_node *node, double x, double y);
// Posizione assoluta (logica).
void vela_node_coords(const struct vela_node *node, double *lx, double *ly);
void vela_node_set_enabled(struct vela_node *node, bool enabled);
// Abilitato lui e tutti gli antenati, e appeso alla radice.
bool vela_node_visible(const struct vela_node *node);
void vela_node_set_opacity(struct vela_node *node, float opacity);
void vela_node_raise_to_top(struct vela_node *node);
void vela_node_place_above(struct vela_node *node, struct vela_node *sibling);
void vela_node_place_below(struct vela_node *node, struct vela_node *sibling);
void vela_node_reparent(struct vela_node *node, struct vela_tree *parent);

void vela_tree_set_shape(struct vela_tree *tree, const struct vela_shape *shape);
void vela_rect_node_set_size(struct vela_rect_node *rect, double width, double height);
void vela_rect_node_set_color(struct vela_rect_node *rect, const struct wlr_render_color *color);
void vela_buffer_node_set_size(struct vela_buffer_node *buffer, double width, double height);

// Ogni superficie di un albero di superfici (radice e sottosuperfici
// mappate), nell'ordine di disegno, con la posizione relativa alla radice.
typedef void (*vela_surface_iterator)(struct wlr_surface *surface, int x, int y, void *data);
void vela_for_each_surface(struct wlr_surface *root, vela_surface_iterator iterator, void *data);

// Posizione di un popup rispetto all'origine della superficie genitore
// (anche se il genitore è un pannello della shell).
void vela_popup_position(struct wlr_xdg_popup *popup, double *x, double *y);

#endif
