// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_RENDER_RENDERER_H
#define VELA_RENDER_RENDERER_H

// Il renderer di Vela (docs/renderer.md §6-7): tutto ciò che si disegna passa
// da qui, su un device Vulkan 1.4 nostro.
//
// Verso wlroots si presenta come un wlr_renderer. Così anche ciò che wlroots
// disegna per conto suo usa i nostri pixel e il nostro device: il cursore
// hardware, le catture degli schermi (screencopy, ext-image-copy-capture), il
// caricamento dei buffer delle app. Non esiste un secondo renderer.
//
// Chi possiede cosa:
// - vela_vulkan (device, formati) lo crea il server e lo distrugge per
//   ultimo, dopo il renderer;
// - vela_renderer appartiene a wlroots: nasce con vela_renderer_create e
//   muore con wlr_renderer_destroy(vela_renderer_wlr(r));
// - un vela_pass vive da vela_renderer_begin_pass a vela_pass_submit, che
//   lo consuma;
// - le texture le crea e distrugge wlroots (wlr_texture_from_buffer,
//   wlr_texture_destroy); le risorse Vulkan se ne vanno quando la GPU ha
//   finito di usarle.

#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/box.h>

struct vela_vulkan;
struct vela_renderer;
struct vela_pass;
struct wlr_allocator;
struct wlr_buffer;
struct wlr_drm_format_set;
struct wlr_drm_syncobj_timeline;
struct wlr_texture;

// ------------------------------------------------------------- device --

// backend_drm_fd: il device DRM del backend (-1 se non ce l'ha, es.
// headless): si sceglie la GPU corrispondente. NULL se Vulkan 1.4 o le
// estensioni necessarie mancano; il motivo è già nel log.
struct vela_vulkan *vela_vulkan_create(int backend_drm_fd);
void vela_vulkan_destroy(struct vela_vulkan *vk);
// Il render node della stessa GPU, aperto dal device: serve all'allocatore
// e alle timeline syncobj.
int vela_vulkan_render_fd(const struct vela_vulkan *vk);

// Allocatore dei buffer degli schermi (docs/renderer.md §7.2): GBM sul
// render node, buffer esportati come dmabuf con un modifier esplicito,
// adatti sia al disegno Vulkan sia allo scanout. Si distrugge con
// wlr_allocator_destroy(). Non chiude render_fd.
struct wlr_allocator *vela_gbm_allocator_create(int render_fd);

// ----------------------------------------------------------- renderer --

struct vela_renderer *vela_renderer_create(struct vela_vulkan *vk);
struct wlr_renderer *vela_renderer_wlr(struct vela_renderer *renderer);
int vela_renderer_render_fd(const struct vela_renderer *renderer);

// I formati (e modifier) dmabuf su cui si può disegnare, e quelli che si
// sanno leggere come texture.
const struct wlr_drm_format_set *vela_renderer_render_formats(const struct vela_renderer *renderer);
const struct wlr_drm_format_set *vela_renderer_texture_formats(const struct vela_renderer *renderer);

// La timeline syncobj del renderer (linux-drm-syncobj-v1, §7.3): ogni
// disegno ne fa scattare un punto quando la GPU ha finito. Sono i punti di
// rilascio dei buffer delle app. NULL se il kernel non la supporta.
struct wlr_drm_syncobj_timeline *vela_renderer_sync_timeline(const struct vela_renderer *renderer);

// L'ultimo punto della timeline Vulkan che la GPU ha finito.
uint64_t vela_renderer_completed(struct vela_renderer *renderer);

// Quando la GPU ha cominciato e finito un disegno (§4.3). Con i timestamp
// calibrati gli istanti sono su CLOCK_MONOTONIC (absolute); altrimenti
// conta solo la durata (end_ns).
struct vela_gpu_timing {
    int64_t start_ns;
    int64_t end_ns;
    bool absolute;
};
// true se il disegno (slot, punto) è finito e la misura è ancora lì.
bool vela_renderer_read_timing(struct vela_renderer *renderer, int slot, uint64_t point, struct vela_gpu_timing *out);

// La texture è nostra e il suo formato non ha alfa (un buffer opaco).
bool vela_texture_is_opaque(struct wlr_texture *texture);

// ------------------------------------------------------------- disegno --

// Comincia un disegno su `buffer` (un dmabuf in uno dei formati di
// disegno). NULL se non si può.
struct vela_pass *vela_renderer_begin_pass(struct vela_renderer *renderer, struct wlr_buffer *buffer);

int vela_pass_width(const struct vela_pass *pass);
int vela_pass_height(const struct vela_pass *pass);
struct wlr_render_pass *vela_pass_wlr(struct vela_pass *pass);

struct vela_texture_draw {
    struct wlr_texture *texture; // nostra; le altre si ignorano
    struct wlr_fbox src; // in pixel della texture; vuoto: tutta
    struct wlr_box dst; // in pixel della destinazione
    enum wl_output_transform transform; // applicata alla texture
    float alpha;
    bool linear; // filtro bilineare (bicubico se ingrandisce); false: copia 1:1
    bool blend;
    const pixman_region32_t *clip; // in pixel della destinazione; NULL: nessuno
    // Sincronizzazione esplicita (linux-drm-syncobj-v1): il punto da
    // aspettare prima di leggere la texture, al posto della fence
    // implicita del dmabuf.
    struct wlr_drm_syncobj_timeline *wait_timeline;
    uint64_t wait_point;
    // Ritaglio arrotondato (§8.1), in pixel della destinazione: raggio 0 o
    // rettangolo vuoto, niente ritaglio.
    struct wlr_box shape_rect;
    float shape_radius;
};
void vela_pass_add_texture(struct vela_pass *pass, const struct vela_texture_draw *draw);

// Colore come in wlroots: sRGB, premoltiplicato. shape_rect può essere NULL.
void vela_pass_add_rect(struct vela_pass *pass, const struct wlr_box *box, const struct wlr_render_color *color,
    const pixman_region32_t *clip, bool blend, const struct wlr_box *shape_rect, float shape_radius);

// Ombra di un rettangolo arrotondato (§8.2) dentro `box`: proiettata da
// `caster`, non disegnata sotto `window`. Colore sRGB premoltiplicato.
void vela_pass_add_shadow(struct vela_pass *pass, const struct wlr_box *box, const struct wlr_box *caster,
    const struct wlr_box *window, float radius, float sigma, const struct wlr_render_color *color,
    const pixman_region32_t *clip);

// Sfocatura dal vivo (§8.3) sotto un pannello: legge ciò che è già stato
// disegnato dietro `region` (pixel della destinazione, clip compreso), lo
// sfoca (dual Kawase) e lo disegna con la ricetta acrylic. La forma la dà
// l'alfa di `panel`, la superficie che va sopra (da aggiungere dopo, come
// al solito). `strength`: l'ampiezza dei passaggi, in pixel; `tint`: la
// tinta acrylic (sRGB premoltiplicato). Niente, se la destinazione non si
// può leggere.
void vela_pass_add_blur(struct vela_pass *pass, const struct vela_texture_draw *panel,
    const pixman_region32_t *region, float strength, const struct wlr_render_color *tint);

// Fin dove legge la sfocatura attorno a una zona, in pixel.
int vela_blur_reach(float strength);

// Il filtro colore dello schermo (Luce notturna, filtri colore): matrice
// 3x3 per righe, in spazio lineare, applicata a tutto ciò che si disegna.
// NULL: nessuno.
void vela_pass_set_color_filter(struct vela_pass *pass, const float *matrix);

// Misura i tempi della GPU di questo disegno (vela_renderer_read_timing).
void vela_pass_measure(struct vela_pass *pass);

// A fine lavoro fa scattare anche questo punto (wlroots, per esempio una
// cattura con sincronizzazione esplicita).
void vela_pass_signal_on_done(struct vela_pass *pass, struct wlr_drm_syncobj_timeline *timeline, uint64_t point);

// Invia il disegno e libera il pass. Nel risultato: il punto della
// timeline del renderer, lo slot della misura (-1: non misurato) e il punto
// della timeline syncobj che scatta a fine lavoro (0 se non c'è): il
// rilascio dei buffer delle app lette da questo disegno.
struct vela_pass_result {
    uint64_t point;
    int timing_slot;
    uint64_t sync_point;
};
bool vela_pass_submit(struct vela_pass *pass, struct vela_pass_result *result);

#endif
