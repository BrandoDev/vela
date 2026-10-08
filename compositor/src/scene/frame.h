// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SCENE_FRAME_H
#define VELA_SCENE_FRAME_H

// Dalla scena ai pixel di uno schermo (docs/renderer.md §6): la scena
// appiattita in una lista di quad, le parti coperte scartate, il danno
// calcolato confrontando con il frame precedente, e solo quello ridisegnato.
// Se una sola app opaca copre lo schermo, il suo buffer va direttamente sul
// piano primario (scanout diretto, §5.3).

#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_damage_ring.h>

struct vela_node;
struct vela_output;
struct vela_pass;
struct vela_renderer;
struct vela_scene;
struct wlr_color_transform;
struct wlr_drm_syncobj_timeline;
struct wlr_output;
struct wlr_output_state;
struct wlr_surface;
struct wlr_texture;

// Un quad da disegnare, in pixel della destinazione.
struct vela_element {
    const void *key; // chi è: la superficie o il nodo nostro
    struct wlr_surface *surface; // superfici delle app, altrimenti NULL
    struct wlr_texture *texture; // NULL: tinta unita
    struct wlr_render_color color;
    struct wlr_fbox src;
    enum wl_output_transform transform;
    struct wlr_box box;
    float opacity;
    bool linear; // filtro bilineare; false: copia 1:1
    // Dalla superficie ai pixel: pixel = origin + scale * coordinata logica.
    double origin_x, origin_y;
    double scale_x, scale_y;
    // La forma (§8.1): ritaglio arrotondato in pixel; raggio 0, nessuno.
    struct wlr_box shape_rect;
    float shape_radius;
    // Ombra (§8.2) se sigma > 0: proiettata da shape_rect, non sotto shadow_window.
    struct wlr_box shadow_window;
    float shadow_sigma;
    // Sfocatura dietro la superficie (ext-background-effect, §8.3): dove,
    // in pixel (vuoto: niente).
    struct wlr_box blur_box;
    bool visible; // non del tutto coperto
    int64_t visible_area; // pixel non coperti
    int order; // posizione nella lista, dal basso
};

// Un elenco di elementi, riusato da un frame all'altro (cresce soltanto).
struct vela_elements {
    struct vela_element *items;
    int count, capacity;
};

// Appiattisce `root` (dal basso verso l'alto) in `out` (che si svuota
// prima). Il punto logico (origin_x, origin_y) finisce nel pixel (0, 0);
// `bounds`: i pixel della destinazione.
struct vela_build_params {
    double origin_x, origin_y;
    double scale;
    struct wlr_box bounds;
    // Catture: la radice si disegna anche se nascosta (finestra ridotta a
    // icona) e senza la sua opacità (animazioni).
    bool capture_root;
};
void vela_build_elements(struct vela_scene *scene, struct vela_node *root, const struct vela_build_params *params,
    struct vela_elements *out);

// Disegna gli elementi visibili dentro `clip` (in pixel del buffer; NULL:
// tutto). Gli elementi sono nello spazio dello schermo ruotato (width x
// height); il buffer può essere ruotato rispetto a esso.
void vela_draw_elements(struct vela_scene *scene, struct vela_pass *pass, const struct vela_elements *elements,
    const pixman_region32_t *clip, enum wl_output_transform output_transform, int width, int height);

// Dopo un disegno che ha letto `elements`: le app con sincronizzazione
// esplicita riavranno i loro buffer quando scatta `sync_point` della
// timeline del renderer (la GPU ha finito).
void vela_add_release_points(struct vela_scene *scene, const struct vela_elements *elements,
    struct vela_renderer *renderer, uint64_t sync_point);

// L'ultimo frame consegnato: per misurarne il costo (§4.3) e, col vblank
// virtuale, sapere quando è pronto.
struct vela_frame_delivered {
    uint64_t point; // punto della timeline del renderer; 0: nessun disegno della GPU
    int timing_slot;
    bool scanout; // il buffer di un'app direttamente sullo schermo
    bool tearing; // e mostrato subito, senza aspettare il vblank
};

// Lo schermo visto dalla scena. Lo crea e lo distrugge il suo vela_output,
// a cui chiede i frame.
struct vela_output_frame {
    struct wl_list link; // vela_scene.frames
    struct vela_scene *scene;
    struct vela_renderer *renderer;
    struct wlr_output *output;
    struct vela_output *owner;
    struct wlr_damage_ring ring;
    int width, height;
    float scale;

    // Il frame precedente e quello in costruzione (si scambiano): da qui il danno.
    struct vela_elements last;
    struct vela_elements current;
    // Le superfici visibili nell'ultimo frame (per i frame callback).
    struct wlr_surface **visible_surfaces;
    int visible_count, visible_capacity;

    struct vela_frame_delivered delivered;
    bool scanout; // l'ultimo frame era uno scanout diretto
    bool tearing; // l'ultimo scanout era con tearing
    const char *scanout_reason; // perché non c'è scanout (VELA_DEBUG_SCANOUT)
    double zoom;
    double zoom_x, zoom_y;

    // La Luce notturna nella gamma del monitor: la versione applicata (quella
    // della scena), se il monitor la accetta.
    struct wlr_color_transform *night_transform;
    bool night_commit_pending;
    uint32_t night_version;
    bool night_in_gamma; // la gamma la sta mostrando
    bool gamma_refused; // questo schermo non la accetta: nel disegno
    bool cursor_locked; // col filtro nel disegno, il cursore lo disegniamo noi
    // Sincronizzazione esplicita dello scanout: il backend fa scattare qui il
    // rilascio del buffer di un'app quando smette di mostrarlo.
    struct wlr_drm_syncobj_timeline *scanout_timeline;
    uint64_t scanout_point;
    int feedback_debounce;
    struct wlr_surface *feedback_surface; // chi ha il feedback di scanout da noi

    struct wl_listener damage;
    struct wl_listener needs_frame;
};

struct vela_output_frame *vela_output_frame_create(struct vela_scene *scene, struct vela_renderer *renderer,
    struct wlr_output *output, struct vela_output *owner);
void vela_output_frame_destroy(struct vela_output_frame *frame);

// Costruisce il frame per lo schermo che nel layout sta in (lx, ly) e fa il
// commit se c'è qualcosa da mostrare. false: niente da fare. `pending`: uno
// stato da applicare nello stesso commit (nuova modalità o scala): il frame
// si disegna già alla nuova dimensione, senza il buffer nero che wlroots
// metterebbe altrimenti.
bool vela_output_frame_render(struct vela_output_frame *frame, double lx, double ly, struct wlr_output_state *pending);

// Qualcuno (wlroots) ha scritto nei buffer dello schermo: nessuno di loro
// contiene più ciò che crediamo.
void vela_output_frame_reset_damage(struct vela_output_frame *frame);
void vela_output_frame_damage_whole(struct vela_output_frame *frame);

// Dopo il frame: i frame callback alle superfici visibili scandite da
// questo schermo.
void vela_output_frame_send_frame_done(struct vela_output_frame *frame, const struct timespec *when);

// Lente di ingrandimento (Accessibilità): lo schermo mostra la zona del
// layout che comincia nel punto logico (x, y), ingrandita `zoom` volte.
// zoom 1: lo schermo com'è.
void vela_output_frame_set_magnifier(struct vela_output_frame *frame, double zoom, double x, double y);

// Dalla scena: una superficie mostrata da questo schermo ha fatto un
// commit, o sparisce.
bool vela_output_frame_shows(struct vela_output_frame *frame, struct wlr_surface *surface);
void vela_output_frame_surface_committed(struct vela_output_frame *frame, struct wlr_surface *surface);
void vela_output_frame_surface_destroyed(struct vela_output_frame *frame, struct wlr_surface *surface);

#endif
