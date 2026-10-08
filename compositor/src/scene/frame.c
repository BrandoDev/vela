// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scene/frame.h"

#include "geometry.h"
#include "output.h"
#include "render/renderer.h"
#include "scene/effects.h"
#include "scene/scene.h"
#include "scene/surface.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/headless.h>
#include <wlr/render/color.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>

// --------------------------------------------------------------- aiuti --

// La Luce notturna come tabella della gamma del monitor: ogni valore
// codificato sRGB torna in luce lineare, prende il suo guadagno e si
// ricodifica.
static struct wlr_color_transform *night_lut(const float gains[3])
{
    enum { SIZE = 256 };
    uint16_t channels[3][SIZE];
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < SIZE; ++i) {
            double x = (double)i / (double)(SIZE - 1);
            double linear = (x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4)) * gains[c];
            double encoded = linear <= 0.0031308 ? linear * 12.92 : 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
            channels[c][i] = (uint16_t)lround(vela_clampd(encoded, 0.0, 1.0) * 65535.0);
        }
    }
    return wlr_color_transform_init_lut_3x1d(SIZE, channels[0], channels[1], channels[2]);
}

static bool same_box(const struct wlr_box *a, const struct wlr_box *b)
{
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

static bool same_fbox(const struct wlr_fbox *a, const struct wlr_fbox *b)
{
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

static bool same_color(const struct wlr_render_color *a, const struct wlr_render_color *b)
{
    return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

static int64_t region_area(const pixman_region32_t *region)
{
    int count = 0;
    const pixman_box32_t *rects = pixman_region32_rectangles(region, &count);
    int64_t area = 0;
    for (int i = 0; i < count; ++i) {
        area += (int64_t)(rects[i].x2 - rects[i].x1) * (rects[i].y2 - rects[i].y1);
    }
    return area;
}

static void union_box(pixman_region32_t *region, const struct wlr_box *box)
{
    pixman_region32_union_rect(region, region, box->x, box->y, (unsigned)(box->width > 0 ? box->width : 0),
        (unsigned)(box->height > 0 ? box->height : 0));
}

static struct vela_element *push_element(struct vela_elements *list)
{
    list->items = vela_grow(list->items, &list->capacity, list->count + 1, sizeof(*list->items));
    struct vela_element *e = &list->items[list->count++];
    memset(e, 0, sizeof(*e));
    return e;
}

// Un rettangolo logico in pixel: si arrotondano i bordi, non posizione e
// dimensione (§3.2), così due rettangoli adiacenti restano adiacenti.
static struct wlr_box to_pixels(double x, double y, double width, double height, const struct vela_build_params *p)
{
    struct vela_pixel_box box = vela_edges_to_pixels(x, y, width, height, p->origin_x, p->origin_y, p->scale);
    return (struct wlr_box) { box.x, box.y, box.width, box.height };
}

static bool on_screen(const struct wlr_box *box, const struct vela_build_params *p)
{
    struct wlr_box clipped;
    return box->width > 0 && box->height > 0 && wlr_box_intersection(&clipped, box, &p->bounds);
}

// Copia 1:1 (nessun ricampionamento) quando un pixel del buffer cade
// esattamente su un pixel dello schermo (§3.3).
static bool is_one_to_one(const struct wlr_fbox *src, const struct wlr_box *box, enum wl_output_transform transform)
{
    return vela_one_to_one(src->x, src->y, src->width, src->height, box->width, box->height,
        transform & WL_OUTPUT_TRANSFORM_90);
}

// L'ampiezza della sfocatura in pixel dello schermo: il raggio è in unità
// logiche, quindi la stessa sfocatura a ogni scala (§8.3).
static float blur_strength(double scale)
{
    return vela_clampf((float)(1.25 * scale), 1.0f, 3.0f);
}

// Una regione della superficie (coordinate sue) in pixel, allargata verso
// l'esterno: per la sfocatura conta coprire tutto.
static void surface_region_to_pixels(const struct vela_element *e, const pixman_region32_t *region,
    pixman_region32_t *out)
{
    pixman_region32_clear(out);
    int count = 0;
    const pixman_box32_t *rects = pixman_region32_rectangles(region, &count);
    for (int i = 0; i < count; ++i) {
        int x1 = (int)floor(e->origin_x + rects[i].x1 * e->scale_x);
        int y1 = (int)floor(e->origin_y + rects[i].y1 * e->scale_y);
        int x2 = (int)ceil(e->origin_x + rects[i].x2 * e->scale_x);
        int y2 = (int)ceil(e->origin_y + rects[i].y2 * e->scale_y);
        if (x2 > x1 && y2 > y1) {
            pixman_region32_union_rect(out, out, x1, y1, (unsigned)(x2 - x1), (unsigned)(y2 - y1));
        }
    }
    pixman_region32_intersect_rect(out, out, e->box.x, e->box.y, (unsigned)e->box.width, (unsigned)e->box.height);
}

// -------------------------------------------------------- appiattimento --

// Il ritaglio arrotondato ereditato dalla forma di un antenato (§8.1).
struct clip {
    struct wlr_box rect;
    float radius;
};

static void apply_clip(struct vela_element *e, const struct clip *clip)
{
    if (clip->radius > 0.0f) {
        e->shape_rect = clip->rect;
        e->shape_radius = clip->radius;
    }
}

// Le ombre di una forma, come Windows 11: una ampia e morbida più una
// stretta "di contatto", più marcate per la finestra attiva (§8.2).
static void add_shadows(const struct vela_tree *tree, const struct wlr_box *window, float radius, float opacity,
    bool active, const struct vela_build_params *p, struct vela_elements *out)
{
    static const struct {
        double offset_y; // logici
        double sigma; // logici
        float active_alpha;
        float inactive_alpha;
    } layers[] = {
        { 8.0, 14.0, 0.42f, 0.24f },
        { 1.0, 2.0, 0.30f, 0.16f },
    };
    for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i) {
        float sigma = (float)(layers[i].sigma * p->scale);
        struct wlr_box caster = *window;
        caster.y += (int)lround(layers[i].offset_y * p->scale);
        int reach = (int)ceilf(3.0f * sigma) + 1;
        struct wlr_box box = { caster.x - reach, caster.y - reach, caster.width + 2 * reach, caster.height + 2 * reach };
        if (!on_screen(&box, p)) {
            continue;
        }
        float alpha = (active ? layers[i].active_alpha : layers[i].inactive_alpha) * opacity;
        struct vela_element *e = push_element(out);
        // Un nome per strato, dentro l'albero stesso (nessun altro nodo o
        // superficie può avere quell'indirizzo).
        e->key = (const char *)tree + 1 + i;
        e->color = (struct wlr_render_color) { 0.0f, 0.0f, 0.0f, alpha };
        e->box = box;
        e->opacity = 1.0f;
        e->shape_rect = caster;
        e->shape_radius = radius;
        e->shadow_window = *window;
        e->shadow_sigma = sigma;
    }
}

// Ciò che serve a visit() per le superfici di un nodo superficie.
struct visit {
    struct vela_scene *scene;
    const struct vela_build_params *params;
    struct vela_elements *out;
    double lx, ly;
    float opacity;
    struct clip clip;
};

static void add_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
    struct visit *v = data;
    const struct vela_build_params *p = v->params;
    struct wlr_texture *texture = wlr_surface_get_texture(surface);
    if (!texture) {
        return; // nessun buffer: non c'è niente da mostrare
    }
    double lx = v->lx + sx;
    double ly = v->ly + sy;
    double width = surface->current.width;
    double height = surface->current.height;
    struct wlr_fbox src;
    wlr_surface_get_buffer_source_box(surface, &src);
    enum wl_output_transform transform = wlr_output_transform_invert(surface->current.transform);
    struct wlr_box box = to_pixels(lx, ly, width, height, p);

    // Nitidezza (§3.3, §3.4): se l'app ha disegnato alla scala di questo
    // schermo (buffer grande quanto la sua area fisica, a meno
    // dell'arrotondamento), la superficie si aggancia a un pixel fisico e
    // occupa esattamente i pixel del buffer: copia 1:1, senza filtri, anche
    // se la posizione logica cade a metà di un pixel.
    bool swapped = transform & WL_OUTPUT_TRANSFORM_90;
    double buffer_width = swapped ? src.height : src.width;
    double buffer_height = swapped ? src.width : src.height;
    if (vela_buffer_matches_area(src.x, src.y, buffer_width, buffer_height, width, height, p->scale)) {
        box.width = (int)buffer_width;
        box.height = (int)buffer_height;
    }
    if (!on_screen(&box, p)) {
        return;
    }
    struct vela_element *e = push_element(v->out);
    e->key = surface;
    e->surface = surface;
    e->texture = texture;
    e->src = src;
    e->transform = transform;
    e->box = box;
    e->opacity = v->opacity;
    e->linear = !is_one_to_one(&e->src, &box, e->transform);
    e->origin_x = (lx - p->origin_x) * p->scale;
    e->origin_y = (ly - p->origin_y) * p->scale;
    e->scale_x = width > 0 ? box.width / width : p->scale;
    e->scale_y = height > 0 ? box.height / height : p->scale;
    apply_clip(e, &v->clip);
    const pixman_region32_t *blur = vela_blur_region(surface);
    if (blur) {
        pixman_region32_t pixels;
        pixman_region32_init(&pixels);
        surface_region_to_pixels(e, blur, &pixels);
        if (pixman_region32_not_empty(&pixels)) {
            const pixman_box32_t *ext = pixman_region32_extents(&pixels);
            e->blur_box = (struct wlr_box) { ext->x1, ext->y1, ext->x2 - ext->x1, ext->y2 - ext->y1 };
        }
        pixman_region32_fini(&pixels);
    }
}

static void visit(struct vela_scene *scene, struct vela_node *node, double lx, double ly, float opacity,
    const struct vela_build_params *p, struct vela_elements *out, bool is_root, struct clip clip)
{
    bool captured = is_root && p->capture_root;
    if (!node->enabled && !captured) {
        return;
    }
    lx += node->x;
    ly += node->y;
    if (!captured) {
        opacity *= node->opacity;
    }
    if (opacity <= 0.0f) {
        return;
    }

    switch (node->type) {
    case VELA_NODE_TREE: {
        struct vela_tree *tree = (struct vela_tree *)node;
        if (node->unclipped) {
            clip = (struct clip) { 0 };
        }
        const struct vela_shape *shape = &tree->shape;
        if (shape->enabled && shape->width > 0.0 && shape->height > 0.0) {
            struct wlr_box rect = to_pixels(lx + shape->x, ly + shape->y, shape->width, shape->height, p);
            float radius = (float)(shape->radius * p->scale);
            // Nelle catture di una finestra l'ombra non c'entra.
            if (shape->shadow && !captured) {
                add_shadows(tree, &rect, radius, opacity, shape->active, p, out);
            }
            clip = (struct clip) { rect, radius };
        }
        struct vela_node *child;
        wl_list_for_each (child, &tree->children, link) {
            visit(scene, child, lx, ly, opacity, p, out, false, clip);
        }
        break;
    }
    case VELA_NODE_SURFACE: {
        struct wlr_surface *root = ((struct vela_surface_node *)node)->surface;
        if (!root->mapped) {
            break;
        }
        struct visit context = { scene, p, out, lx, ly, opacity, clip };
        vela_for_each_surface(root, add_surface, &context);
        break;
    }
    case VELA_NODE_RECT: {
        struct vela_rect_node *rect = (struct vela_rect_node *)node;
        struct wlr_box box = to_pixels(lx, ly, rect->width, rect->height, p);
        if (!on_screen(&box, p) || rect->color.a <= 0.0f) {
            break;
        }
        struct vela_element *e = push_element(out);
        e->key = node;
        e->color = rect->color;
        e->color.r *= opacity;
        e->color.g *= opacity;
        e->color.b *= opacity;
        e->color.a *= opacity;
        e->box = box;
        e->opacity = 1.0f;
        apply_clip(e, &clip);
        break;
    }
    case VELA_NODE_BUFFER: {
        struct vela_buffer_node *buffer = (struct vela_buffer_node *)node;
        struct wlr_box box = to_pixels(lx, ly, buffer->width, buffer->height, p);
        if (!on_screen(&box, p) || !buffer->texture) {
            break;
        }
        struct vela_element *e = push_element(out);
        e->key = node;
        e->texture = buffer->texture;
        e->src = buffer->src;
        e->transform = buffer->transform;
        e->box = box;
        e->opacity = opacity;
        e->linear = !is_one_to_one(&e->src, &box, e->transform);
        apply_clip(e, &clip);
        break;
    }
    }
}

void vela_build_elements(struct vela_scene *scene, struct vela_node *root, const struct vela_build_params *params,
    struct vela_elements *out)
{
    out->count = 0;
    double lx = 0.0;
    double ly = 0.0;
    if (root->parent) {
        vela_node_coords(&root->parent->node, &lx, &ly);
    }
    visit(scene, root, lx, ly, 1.0f, params, out, true, (struct clip) { 0 });
}

// ------------------------------------------------------------ occlusione --

// Gli angoli arrotondati non sono opachi: si tolgono quattro quadrati.
static void subtract_corners(const struct vela_element *e, pixman_region32_t *region)
{
    if (e->shape_radius <= 0.0f) {
        return;
    }
    const struct wlr_box *r = &e->shape_rect;
    pixman_region32_intersect_rect(region, region, r->x, r->y, (unsigned)r->width, (unsigned)r->height);
    int c = (int)ceilf(e->shape_radius);
    pixman_region32_t corners;
    pixman_region32_init(&corners);
    pixman_region32_union_rect(&corners, &corners, r->x, r->y, (unsigned)c, (unsigned)c);
    pixman_region32_union_rect(&corners, &corners, r->x + r->width - c, r->y, (unsigned)c, (unsigned)c);
    pixman_region32_union_rect(&corners, &corners, r->x, r->y + r->height - c, (unsigned)c, (unsigned)c);
    pixman_region32_union_rect(&corners, &corners, r->x + r->width - c, r->y + r->height - c, (unsigned)c,
        (unsigned)c);
    pixman_region32_subtract(region, region, &corners);
    pixman_region32_fini(&corners);
}

// La parte certamente opaca di un elemento, in pixel.
static void opaque_region(const struct vela_element *e, pixman_region32_t *out)
{
    pixman_region32_clear(out);
    if (e->opacity < 1.0f || e->shadow_sigma > 0.0f) {
        return;
    }
    if (!e->texture) {
        if (e->color.a >= 1.0f) {
            union_box(out, &e->box);
        }
        subtract_corners(e, out);
        return;
    }
    if (!e->surface) {
        if (vela_texture_is_opaque(e->texture)) {
            union_box(out, &e->box);
        }
        subtract_corners(e, out);
        return;
    }
    // Regione opaca dichiarata dall'app, in coordinate della superficie: la
    // si restringe verso l'interno, per non coprire mai troppo.
    int count = 0;
    const pixman_box32_t *rects = pixman_region32_rectangles(&e->surface->opaque_region, &count);
    for (int i = 0; i < count; ++i) {
        int x1 = (int)ceil(e->origin_x + rects[i].x1 * e->scale_x);
        int y1 = (int)ceil(e->origin_y + rects[i].y1 * e->scale_y);
        int x2 = (int)floor(e->origin_x + rects[i].x2 * e->scale_x);
        int y2 = (int)floor(e->origin_y + rects[i].y2 * e->scale_y);
        if (x2 > x1 && y2 > y1) {
            pixman_region32_union_rect(out, out, x1, y1, (unsigned)(x2 - x1), (unsigned)(y2 - y1));
        }
    }
    pixman_region32_intersect_rect(out, out, e->box.x, e->box.y, (unsigned)e->box.width, (unsigned)e->box.height);
    subtract_corners(e, out);
}

// Scarta ciò che è coperto da superfici opache (dall'alto verso il basso).
static void cull_occluded(struct vela_elements *elements)
{
    pixman_region32_t covered, visible, opaque;
    pixman_region32_init(&covered);
    pixman_region32_init(&opaque);
    for (int i = elements->count - 1; i >= 0; --i) {
        struct vela_element *e = &elements->items[i];
        pixman_region32_init_rect(&visible, e->box.x, e->box.y, (unsigned)e->box.width, (unsigned)e->box.height);
        pixman_region32_subtract(&visible, &visible, &covered);
        e->visible = pixman_region32_not_empty(&visible);
        e->visible_area = e->visible ? region_area(&visible) : 0;
        if (e->visible) {
            opaque_region(e, &opaque);
            pixman_region32_union(&covered, &covered, &opaque);
        }
        pixman_region32_fini(&visible);
    }
    pixman_region32_fini(&covered);
    pixman_region32_fini(&opaque);
}

// Le zone sfocate leggono ciò che sta loro attorno: se il danno ne tocca una
// (col raggio della sfocatura) si ridisegna tutta, raggio compreso.
static void expand_damage_for_blur(const struct vela_elements *elements, pixman_region32_t *damage)
{
    // Più giri: una zona allargata può toccarne un'altra (menu Start sopra la taskbar).
    for (int round = 0; round < 3; ++round) {
        bool grown = false;
        for (int i = 0; i < elements->count; ++i) {
            const struct vela_element *e = &elements->items[i];
            if (!e->visible || wlr_box_empty(&e->blur_box)) {
                continue;
            }
            int reach = vela_blur_reach(blur_strength(e->scale_x));
            const pixman_box32_t zone = { e->blur_box.x - reach, e->blur_box.y - reach,
                e->blur_box.x + e->blur_box.width + reach, e->blur_box.y + e->blur_box.height + reach };
            pixman_region32_t inside;
            pixman_region32_init_rect(&inside, zone.x1, zone.y1, (unsigned)(zone.x2 - zone.x1),
                (unsigned)(zone.y2 - zone.y1));
            pixman_region32_intersect(&inside, &inside, damage);
            bool touched = pixman_region32_not_empty(&inside);
            pixman_region32_fini(&inside);
            if (touched && pixman_region32_contains_rectangle(damage, &zone) != PIXMAN_REGION_IN) {
                pixman_region32_union_rect(damage, damage, zone.x1, zone.y1, (unsigned)(zone.x2 - zone.x1),
                    (unsigned)(zone.y2 - zone.y1));
                grown = true;
            }
        }
        if (!grown) {
            return;
        }
    }
}

// Il contenuto delle superfici cambia con i commit, che portano il loro
// danno preciso: qui conta solo dove e come si disegnano. Per i nodi nostri
// anche la texture.
static bool same_look(const struct vela_element *a, const struct vela_element *b)
{
    bool same_texture = a->surface ? true : a->texture == b->texture;
    return a->surface == b->surface && same_texture && same_box(&a->box, &b->box) && same_fbox(&a->src, &b->src)
        && a->transform == b->transform && a->opacity == b->opacity && a->linear == b->linear
        && same_color(&a->color, &b->color) && same_box(&a->shape_rect, &b->shape_rect)
        && a->shape_radius == b->shape_radius && same_box(&a->shadow_window, &b->shadow_window)
        && a->shadow_sigma == b->shadow_sigma && same_box(&a->blur_box, &b->blur_box);
}

// ---------------------------------------------------------------- disegno --

void vela_draw_elements(struct vela_scene *scene, struct vela_pass *pass, const struct vela_elements *elements,
    const pixman_region32_t *clip, enum wl_output_transform output_transform, int width, int height)
{
    enum wl_output_transform to_buffer = wlr_output_transform_invert(output_transform);
    for (int i = 0; i < elements->count; ++i) {
        const struct vela_element *e = &elements->items[i];
        if (!e->visible) {
            continue;
        }
        struct wlr_box dst;
        wlr_box_transform(&dst, &e->box, to_buffer, width, height);
        struct wlr_box shape = { 0 };
        if (e->shape_radius > 0.0f) {
            wlr_box_transform(&shape, &e->shape_rect, to_buffer, width, height);
        }
        if (e->shadow_sigma > 0.0f) {
            struct wlr_box window;
            wlr_box_transform(&window, &e->shadow_window, to_buffer, width, height);
            // Sotto la finestra l'ombra non si vede: lo shader lavora solo
            // sulla cornice attorno (gli angoli restano, sono arrotondati).
            pixman_region32_t ring;
            pixman_region32_init_rect(&ring, dst.x, dst.y, (unsigned)dst.width, (unsigned)dst.height);
            if (clip) {
                pixman_region32_intersect(&ring, &ring, clip);
            }
            int inset = (int)ceilf(e->shape_radius);
            if (window.width > 2 * inset && window.height > 2 * inset) {
                pixman_region32_t inner;
                pixman_region32_init_rect(&inner, window.x + inset, window.y, (unsigned)(window.width - 2 * inset),
                    (unsigned)window.height);
                pixman_region32_union_rect(&inner, &inner, window.x, window.y + inset, (unsigned)window.width,
                    (unsigned)(window.height - 2 * inset));
                pixman_region32_subtract(&ring, &ring, &inner);
                pixman_region32_fini(&inner);
            }
            vela_pass_add_shadow(pass, &dst, &shape, &window, e->shape_radius, e->shadow_sigma, &e->color, &ring);
            pixman_region32_fini(&ring);
            continue;
        }
        if (!e->texture) {
            vela_pass_add_rect(pass, &dst, &e->color, clip, true, &shape, e->shape_radius);
            continue;
        }
        struct vela_texture_draw draw = {
            .texture = e->texture,
            .src = e->src,
            .dst = dst,
            .transform = wlr_output_transform_compose(e->transform, output_transform),
            .alpha = e->opacity,
            .linear = e->linear,
            .blend = true,
            .clip = clip,
            .shape_rect = shape,
            .shape_radius = e->shape_radius,
        };
        // Sincronizzazione esplicita: il buffer è pronto quando scatta il
        // punto di acquisizione dell'app.
        if (e->surface) {
            struct wlr_linux_drm_syncobj_surface_v1_state *sync
                = wlr_linux_drm_syncobj_v1_get_surface_state(e->surface);
            if (sync && sync->acquire_timeline) {
                draw.wait_timeline = sync->acquire_timeline;
                draw.wait_point = sync->acquire_point;
            }
        }
        // Sotto la superficie, lo sfondo sfocato che ha chiesto (§8.3).
        const pixman_region32_t *blur = e->surface && !wlr_box_empty(&e->blur_box) ? vela_blur_region(e->surface)
                                                                                   : NULL;
        if (blur) {
            pixman_region32_t region;
            pixman_region32_init(&region);
            surface_region_to_pixels(e, blur, &region);
            wlr_region_transform(&region, &region, to_buffer, width, height);
            if (clip) {
                pixman_region32_intersect(&region, &region, clip);
            }
            if (pixman_region32_not_empty(&region)) {
                struct vela_texture_draw panel = draw;
                panel.clip = NULL;
                vela_pass_add_blur(pass, &panel, &region, blur_strength(e->scale_x), &scene->acrylic_tint);
            }
            pixman_region32_fini(&region);
        }
        vela_pass_add_texture(pass, &draw);
    }
}

void vela_add_release_points(struct vela_scene *scene, const struct vela_elements *elements,
    struct vela_renderer *renderer, uint64_t sync_point)
{
    struct wlr_drm_syncobj_timeline *timeline = vela_renderer_sync_timeline(renderer);
    if (!timeline || sync_point == 0 || !scene->event_loop) {
        return;
    }
    for (int i = 0; i < elements->count; ++i) {
        const struct vela_element *e = &elements->items[i];
        if (!e->visible || !e->surface) {
            continue;
        }
        struct wlr_linux_drm_syncobj_surface_v1_state *sync = wlr_linux_drm_syncobj_v1_get_surface_state(e->surface);
        if (sync) {
            wlr_linux_drm_syncobj_v1_state_add_release_point(sync, timeline, sync_point, scene->event_loop);
        }
    }
}

// ---------------------------------------------------------------- schermo --

static void handle_damage(struct wl_listener *listener, void *data)
{
    struct vela_output_frame *frame = wl_container_of(listener, frame, damage);
    const struct wlr_output_event_damage *event = data;
    wlr_damage_ring_add(&frame->ring, event->damage);
    vela_output_schedule_frame(frame->owner);
}

static void handle_needs_frame(struct wl_listener *listener, void *data)
{
    struct vela_output_frame *frame = wl_container_of(listener, frame, needs_frame);
    vela_output_schedule_frame(frame->owner);
}

struct vela_output_frame *vela_output_frame_create(struct vela_scene *scene, struct vela_renderer *renderer,
    struct wlr_output *output, struct vela_output *owner)
{
    struct vela_output_frame *frame = calloc(1, sizeof(*frame));
    frame->scene = scene;
    frame->renderer = renderer;
    frame->output = output;
    frame->owner = owner;
    frame->zoom = 1.0;
    wlr_damage_ring_init(&frame->ring);
    wl_list_insert(scene->frames.prev, &frame->link);

    // Il cursore disegnato da noi (quando non c'è quello hardware) e chi
    // chiede un frame (cursore hardware, catture) passano da qui.
    frame->damage.notify = handle_damage;
    wl_signal_add(&output->events.damage, &frame->damage);
    frame->needs_frame.notify = handle_needs_frame;
    wl_signal_add(&output->events.needs_frame, &frame->needs_frame);

    // Scanout di buffer con sincronizzazione esplicita: serve un backend che
    // sappia aspettare e far scattare timeline (DRM, annidato in un ospite
    // con linux-drm-syncobj). Quello headless dice di saperlo fare, ma poi
    // rifiuta i commit con le timeline.
    if (output->backend->features.timeline && vela_renderer_sync_timeline(renderer)
        && !wlr_output_is_headless(output)) {
        frame->scanout_timeline = wlr_drm_syncobj_timeline_create(vela_renderer_render_fd(renderer));
    }
    return frame;
}

static void send_feedback(struct vela_output_frame *frame, struct wlr_surface *surface, bool scanout);

void vela_output_frame_destroy(struct vela_output_frame *frame)
{
    wl_list_remove(&frame->damage.link);
    wl_list_remove(&frame->needs_frame.link);
    wl_list_remove(&frame->link);
    for (int i = 0; i < frame->last.count; ++i) {
        if (frame->last.items[i].surface) {
            vela_surface_forget(frame->last.items[i].surface, frame->output);
        }
    }
    if (frame->feedback_surface) {
        send_feedback(frame, frame->feedback_surface, false);
    }
    if (frame->night_transform) {
        wlr_color_transform_unref(frame->night_transform);
    }
    if (frame->scanout_timeline) {
        wlr_drm_syncobj_timeline_unref(frame->scanout_timeline);
    }
    wlr_damage_ring_finish(&frame->ring);
    free(frame->last.items);
    free(frame->current.items);
    free(frame->visible_surfaces);
    free(frame);
}

// Prima di ogni frame: se la Luce notturna è cambiata, la prova sulla
// gamma; se il monitor la accetta va nel prossimo commit.
static void prepare_night_light(struct vela_output_frame *frame)
{
    struct vela_scene *scene = frame->scene;
    if (scene->night_version == frame->night_version) {
        return;
    }
    frame->night_version = scene->night_version;
    // Solo gli schermi veri hanno una gamma (non quelli annidati o headless).
    if (frame->gamma_refused || !wlr_output_is_drm(frame->output)) {
        frame->gamma_refused = true;
        frame->night_in_gamma = false;
        return;
    }
    struct wlr_color_transform *transform = scene->night_active ? night_lut(scene->night_gains) : NULL;
    struct wlr_output_state test;
    wlr_output_state_init(&test);
    wlr_output_state_set_color_transform(&test, transform);
    bool ok = wlr_output_test_state(frame->output, &test);
    wlr_output_state_finish(&test);
    if (!ok) {
        wlr_log(WLR_INFO, "%s: the monitor doesn't accept night light in the gamma LUT, rendering applies it",
            frame->output->name);
        if (transform) {
            wlr_color_transform_unref(transform);
        }
        frame->gamma_refused = true;
        frame->night_in_gamma = false;
        return;
    }
    if (frame->night_transform) {
        wlr_color_transform_unref(frame->night_transform);
    }
    frame->night_transform = transform;
    frame->night_commit_pending = true;
    frame->night_in_gamma = scene->night_active;
}

void vela_output_frame_set_magnifier(struct vela_output_frame *frame, double zoom, double x, double y)
{
    zoom = zoom > 1.0 ? zoom : 1.0;
    if (zoom == frame->zoom && (zoom == 1.0 || (x == frame->zoom_x && y == frame->zoom_y))) {
        return;
    }
    frame->zoom = zoom;
    frame->zoom_x = x;
    frame->zoom_y = y;
    // Tutto si sposta: il confronto con il frame precedente trova il danno.
    vela_output_schedule_frame(frame->owner);
}

void vela_output_frame_damage_whole(struct vela_output_frame *frame)
{
    int width = 0;
    int height = 0;
    wlr_output_transformed_resolution(frame->output, &width, &height);
    const struct wlr_box whole = { 0, 0, width, height };
    wlr_damage_ring_add_box(&frame->ring, &whole);
    vela_output_schedule_frame(frame->owner);
}

static struct vela_element *last_element(struct vela_output_frame *frame, const void *key)
{
    for (int i = 0; i < frame->last.count; ++i) {
        if (frame->last.items[i].key == key) {
            return &frame->last.items[i];
        }
    }
    return NULL;
}

bool vela_output_frame_shows(struct vela_output_frame *frame, struct wlr_surface *surface)
{
    return last_element(frame, surface) != NULL;
}

void vela_output_frame_surface_committed(struct vela_output_frame *frame, struct wlr_surface *surface)
{
    const struct vela_element *e = last_element(frame, surface);
    if (!e) {
        return;
    }
    pixman_region32_t region;
    pixman_region32_init(&region);
    wlr_surface_get_effective_damage(surface, &region);
    if (pixman_region32_not_empty(&region)) {
        bool exact = e->scale_x == floor(e->scale_x) && e->scale_y == floor(e->scale_y)
            && e->origin_x == floor(e->origin_x) && e->origin_y == floor(e->origin_y);
        wlr_region_scale_xy(&region, &region, (float)e->scale_x, (float)e->scale_y);
        pixman_region32_translate(&region, (int)floor(e->origin_x), (int)floor(e->origin_y));
        if (!exact) {
            wlr_region_expand(&region, &region, 1); // il filtro tocca anche i vicini
        }
        pixman_region32_intersect_rect(&region, &region, e->box.x, e->box.y, (unsigned)e->box.width,
            (unsigned)e->box.height);
        wlr_damage_ring_add(&frame->ring, &region);
    }
    pixman_region32_fini(&region);
    // Anche senza danno: l'app aspetta il suo frame callback.
    vela_output_schedule_frame(frame->owner);
}

void vela_output_frame_surface_destroyed(struct vela_output_frame *frame, struct wlr_surface *surface)
{
    if (surface == frame->feedback_surface) {
        frame->feedback_surface = NULL;
        frame->feedback_debounce = 0;
    }
    struct vela_element *e = last_element(frame, surface);
    if (!e) {
        return;
    }
    wlr_damage_ring_add_box(&frame->ring, &e->box);
    // Via dal frame precedente (anche gli eventuali doppioni).
    int kept = 0;
    for (int i = 0; i < frame->last.count; ++i) {
        if (frame->last.items[i].key != surface) {
            frame->last.items[kept++] = frame->last.items[i];
        }
    }
    frame->last.count = kept;
    kept = 0;
    for (int i = 0; i < frame->visible_count; ++i) {
        if (frame->visible_surfaces[i] != surface) {
            frame->visible_surfaces[kept++] = frame->visible_surfaces[i];
        }
    }
    frame->visible_count = kept;
    vela_output_schedule_frame(frame->owner);
}

static void update_surfaces(struct vela_output_frame *frame, const struct vela_elements *elements)
{
    frame->visible_count = 0;
    const struct wlr_box bounds = { 0, 0, frame->width, frame->height };
    for (int i = 0; i < elements->count; ++i) {
        const struct vela_element *e = &elements->items[i];
        if (!e->surface) {
            continue;
        }
        struct wlr_box on_output = { 0 };
        wlr_box_intersection(&on_output, &e->box, &bounds);
        vela_surface_report(e->surface, frame->output, (int64_t)on_output.width * on_output.height, e->visible_area);
        if (e->visible) {
            frame->visible_surfaces = vela_grow(frame->visible_surfaces, &frame->visible_capacity,
                frame->visible_count + 1, sizeof(*frame->visible_surfaces));
            frame->visible_surfaces[frame->visible_count++] = e->surface;
        }
    }
}

void vela_output_frame_reset_damage(struct vela_output_frame *frame)
{
    // Tutti i buffer ripartono da capo: il prossimo frame li ridisegna interi.
    wlr_damage_ring_finish(&frame->ring);
    wlr_damage_ring_init(&frame->ring);
    frame->width = 0;
    frame->height = 0;
    vela_output_schedule_frame(frame->owner);
}

// Il danno: ciò che è apparso, sparito, cambiato o spostato rispetto al
// frame precedente (il contenuto delle superfici arriva dai commit). Poi
// `current` diventa `last`.
static void diff_with_last(struct vela_output_frame *frame)
{
    struct vela_elements *current = &frame->current;
    struct vela_elements *last = &frame->last;
    pixman_region32_t changed;
    pixman_region32_init(&changed);
    // Quali elementi di prima si ritrovano ora; la ricerca parte da dove
    // ci si aspetta di trovarli (di solito l'ordine non cambia).
    bool *found = calloc((size_t)(last->count ? last->count : 1), sizeof(*found));
    int highest_old_order = 0;
    for (int i = 0; i < current->count; ++i) {
        struct vela_element *e = &current->items[i];
        e->order = i;
        const struct vela_element *old = NULL;
        for (int k = 0; k < last->count; ++k) {
            int j = (i + k) % last->count;
            if (last->items[j].key == e->key) {
                old = &last->items[j];
                found[j] = true;
                break;
            }
        }
        if (!old) {
            union_box(&changed, &e->box);
            continue;
        }
        if (!same_look(e, old)) {
            union_box(&changed, &old->box);
            union_box(&changed, &e->box);
        } else if (old->order < highest_old_order) {
            union_box(&changed, &e->box); // è salito sopra altri elementi
        }
        if (old->order > highest_old_order) {
            highest_old_order = old->order;
        }
    }
    for (int j = 0; j < last->count; ++j) {
        if (found[j]) {
            continue;
        }
        union_box(&changed, &last->items[j].box);
        if (last->items[j].surface) {
            vela_surface_forget(last->items[j].surface, frame->output);
        }
    }
    free(found);
    wlr_damage_ring_add(&frame->ring, &changed);
    pixman_region32_fini(&changed);

    struct vela_elements swap = *last;
    *last = *current;
    *current = swap;
}

static const struct vela_element *scanout_candidate(struct vela_output_frame *frame,
    const struct vela_elements *elements, enum wl_output_transform transform);
static bool try_scanout(struct vela_output_frame *frame, const struct vela_element *e, struct wlr_output_state *state);
static void update_feedback(struct vela_output_frame *frame, const struct vela_element *candidate);

bool vela_output_frame_render(struct vela_output_frame *frame, double lx, double ly, struct wlr_output_state *pending)
{
    struct wlr_output *output = frame->output;
    struct vela_scene *scene = frame->scene;
    if (!output->enabled) {
        return false;
    }
    // Dimensione e scala del frame: quelle nuove, se il commit cambia modo.
    int width = output->width;
    int height = output->height;
    float scale = output->scale;
    enum wl_output_transform transform = output->transform;
    if (pending && (pending->committed & WLR_OUTPUT_STATE_MODE)) {
        if (pending->mode_type == WLR_OUTPUT_STATE_MODE_FIXED && pending->mode) {
            width = pending->mode->width;
            height = pending->mode->height;
        } else {
            width = pending->custom_mode.width;
            height = pending->custom_mode.height;
        }
    }
    if (pending && (pending->committed & WLR_OUTPUT_STATE_SCALE)) {
        scale = pending->scale;
    }
    if (pending && (pending->committed & WLR_OUTPUT_STATE_TRANSFORM)) {
        transform = pending->transform;
    }
    if (transform & WL_OUTPUT_TRANSFORM_90) {
        int swap = width;
        width = height;
        height = swap;
    }
    const struct wlr_box whole = { 0, 0, width, height };
    if (width != frame->width || height != frame->height || scale != frame->scale) {
        wlr_damage_ring_finish(&frame->ring);
        wlr_damage_ring_init(&frame->ring);
        frame->width = width;
        frame->height = height;
        frame->scale = scale;
        wlr_damage_ring_add_box(&frame->ring, &whole);
    }

    // 1. La scena appiattita, in pixel di questo schermo (con la lente: la
    //    zona ingrandita), senza ciò che è coperto.
    bool magnified = frame->zoom > 1.0;
    const struct vela_build_params params = {
        .origin_x = magnified ? frame->zoom_x : lx,
        .origin_y = magnified ? frame->zoom_y : ly,
        .scale = scale * (magnified ? frame->zoom : 1.0),
        .bounds = whole,
    };
    vela_build_elements(scene, &scene->root->node, &params, &frame->current);
    cull_occluded(&frame->current);

    // 2. Il danno rispetto al frame precedente; da qui gli elementi di
    //    questo frame sono in frame->last.
    diff_with_last(frame);
    const struct vela_elements *elements = &frame->last;
    expand_damage_for_blur(elements, &frame->ring.current);
    // Diagnosi: VELA_DEBUG_DAMAGE=1 ridisegna tutto a ogni frame.
    if (vela_env_one("VELA_DEBUG_DAMAGE") && pixman_region32_not_empty(&frame->ring.current)) {
        wlr_damage_ring_finish(&frame->ring);
        wlr_damage_ring_init(&frame->ring);
        wlr_damage_ring_add_box(&frame->ring, &whole);
    }
    update_surfaces(frame, elements);

    // Il colore: i filtri, e la Luce notturna se la gamma del monitor non la
    // prende, si applicano nel disegno (cursore compreso).
    prepare_night_light(frame);
    bool night_in_drawing = scene->night_active && !frame->night_in_gamma;
    bool filtered = scene->color_filtered || night_in_drawing;
    float filter[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (filtered) {
        const float *f = scene->color_filtered ? scene->color_filter : filter;
        float base[9];
        memcpy(base, f, sizeof(base));
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                filter[row * 3 + column] = (night_in_drawing ? scene->night_gains[row] : 1.0f) * base[row * 3 + column];
            }
        }
    }
    if (filtered != frame->cursor_locked) {
        wlr_output_lock_software_cursors(output, filtered);
        frame->cursor_locked = filtered;
    }

    // Un'app a schermo intero che si può mostrare così com'è (non col
    // filtro nel disegno).
    const struct vela_element *candidate = pending || filtered ? NULL : scanout_candidate(frame, elements, transform);
    update_feedback(frame, candidate);

    bool damaged = pixman_region32_not_empty(&frame->ring.current) || frame->night_commit_pending;
    if (!damaged && !output->needs_frame && !pending) {
        return false;
    }
    frame->delivered = (struct vela_frame_delivered) { 0, -1, false, false };

    // Lo stato del commit: quello del chiamante (cambio di modo, disegnato
    // subito alla nuova dimensione) o uno nostro.
    struct wlr_output_state own;
    wlr_output_state_init(&own);
    struct wlr_output_state *state = pending ? pending : &own;
    if (frame->night_commit_pending) {
        wlr_output_state_set_color_transform(state, frame->night_transform);
    }
    // Anche quando c'è solo da spostare il cursore hardware si consegna un
    // buffer (con danno vuoto il disegno non costa quasi nulla). Con DRM un
    // commit senza buffer è bloccante: fermerebbe il compositor fino al
    // vblank di questo schermo, e intanto gli altri schermi perderebbero i
    // loro (con un 75 Hz accanto, il 180 Hz scendeva sotto i 60 fps).

    // Scanout diretto: niente disegno, il buffer dell'app va sullo schermo.
    if (candidate && try_scanout(frame, candidate, state)) {
        wlr_output_state_finish(&own);
        frame->night_commit_pending = false;
        // Lo schermo è a posto: il danno accumulato non serve più (i nostri
        // buffer invece sono rimasti indietro, vedi sotto).
        pixman_region32_clear(&frame->ring.current);
        if (!frame->scanout) {
            wlr_log(WLR_INFO, "%s: direct scanout on", output->name);
        }
        frame->scanout = true;
        frame->delivered.scanout = true;
        return true;
    }
    if (frame->scanout) {
        // Si torna a comporre: i nostri buffer non hanno visto i frame dello
        // scanout, si ridisegnano interi.
        wlr_log(WLR_INFO, "%s: direct scanout off", output->name);
        frame->scanout = false;
        if (frame->tearing) {
            wlr_log(WLR_INFO, "%s: tearing off", output->name);
            frame->tearing = false;
        }
        wlr_damage_ring_add_box(&frame->ring, &whole);
    }

    if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain)) {
        wlr_output_state_finish(&own);
        return false;
    }
    struct wlr_buffer *buffer = wlr_swapchain_acquire(output->swapchain);
    if (!buffer) {
        wlr_output_state_finish(&own);
        return false;
    }

    // Il danno di questo frame (per il backend) e quello del buffer che
    // stiamo per riusare (per l'età del buffer: ciò che gli manca rispetto
    // a ora).
    pixman_region32_t frame_damage, buffer_damage;
    pixman_region32_init(&frame_damage);
    pixman_region32_init(&buffer_damage);
    pixman_region32_copy(&frame_damage, &frame->ring.current);
    wlr_damage_ring_rotate_buffer(&frame->ring, buffer, &buffer_damage);
    expand_damage_for_blur(elements, &buffer_damage);

    enum wl_output_transform to_buffer = wlr_output_transform_invert(transform);
    wlr_region_transform(&buffer_damage, &buffer_damage, to_buffer, width, height);
    wlr_region_transform(&frame_damage, &frame_damage, to_buffer, width, height);

    bool ok = false;
    struct vela_pass *pass = vela_renderer_begin_pass(frame->renderer, buffer);
    if (pass) {
        if (filtered) {
            vela_pass_set_color_filter(pass, filter);
        }
        // Sotto tutto, il nero (lo sfondo del desktop di solito lo copre).
        const struct wlr_box all = { 0, 0, buffer->width, buffer->height };
        const struct wlr_render_color black = { 0.0f, 0.0f, 0.0f, 1.0f };
        vela_pass_add_rect(pass, &all, &black, &buffer_damage, false, NULL, 0.0f);
        vela_draw_elements(scene, pass, elements, &buffer_damage, transform, width, height);
        wlr_output_add_software_cursors_to_render_pass(output, vela_pass_wlr(pass), &buffer_damage);
        vela_pass_measure(pass);
        struct vela_pass_result result;
        ok = vela_pass_submit(pass, &result);
        if (ok) {
            frame->delivered.point = result.point;
            frame->delivered.timing_slot = result.timing_slot;
            vela_add_release_points(scene, elements, frame->renderer, result.sync_point);
        }
    }

    int buffer_width = buffer->width;
    int buffer_height = buffer->height;
    if (ok) {
        wlr_output_state_set_buffer(state, buffer);
        wlr_output_state_set_damage(state, &frame_damage);
        for (int i = 0; i < frame->visible_count; ++i) {
            struct wlr_surface *surface = frame->visible_surfaces[i];
            const struct vela_surface_state *info = vela_surface_state_get(surface);
            if (!info || info->pacing == output) {
                wlr_presentation_surface_textured_on_output(surface, output);
            }
        }
        ok = wlr_output_commit_state(output, state);
    }
    if (ok) {
        frame->night_commit_pending = false;
    }
    wlr_buffer_unlock(buffer);
    wlr_output_state_finish(&own);
    if (!ok) {
        // Il frame non è arrivato allo schermo: il suo danno va ridisegnato.
        wlr_region_transform(&frame_damage, &frame_damage, transform, buffer_width, buffer_height);
        wlr_damage_ring_add(&frame->ring, &frame_damage);
    }
    pixman_region32_fini(&frame_damage);
    pixman_region32_fini(&buffer_damage);
    return ok;
}

// --------------------------------------------------------- scanout diretto --

// VELA_DEBUG_SCANOUT=1: perché un'app a schermo intero non va in scanout.
static void note_scanout_reason(struct vela_output_frame *frame, const char *reason)
{
    if (vela_env_one("VELA_DEBUG_SCANOUT") && reason != frame->scanout_reason) {
        wlr_log(WLR_INFO, "%s: no direct scanout: %s", frame->output->name, reason ? reason : "(candidata)");
    }
    frame->scanout_reason = reason;
}

// Scanout diretto (§5.3): una sola superficie opaca copre lo schermo, 1:1,
// e il suo buffer va sul piano primario senza disegnare nulla.
static const struct vela_element *scanout_candidate(struct vela_output_frame *frame,
    const struct vela_elements *elements, enum wl_output_transform transform)
{
    // VELA_SCANOUT=0: sempre composizione (diagnosi, confronti).
    if (vela_env_off("VELA_SCANOUT")) {
        return NULL;
    }
    const char *reason = NULL;
    const struct vela_element *found = NULL;
    for (int i = 0; i < elements->count; ++i) {
        if (!elements->items[i].visible) {
            continue;
        }
        if (found) {
            reason = "something else is visible (panels, notifications, our own cursor)";
            found = NULL;
            break;
        }
        found = &elements->items[i];
    }
    if (found) {
        const struct vela_element *e = found;
        const struct wlr_box whole = { 0, 0, frame->width, frame->height };
        if (!e->surface || !e->texture) {
            reason = "not an app surface";
        } else if (e->shape_radius > 0.0f) {
            reason = "has rounded corners";
        } else if (e->opacity < 1.0f) {
            reason = "is translucent";
        } else if (!same_box(&e->box, &whole)) {
            reason = "doesn't cover the output exactly";
        } else if (e->transform != transform) {
            reason = "is rotated relative to the output";
        } else if (e->linear) {
            reason = "is scaled (buffer size differs from the output)";
        } else {
            // Opaca: formato senza alfa, o regione opaca dichiarata su tutto.
            bool opaque = vela_texture_is_opaque(e->texture);
            if (!opaque) {
                pixman_box32_t all = { 0, 0, e->surface->current.width, e->surface->current.height };
                opaque = pixman_region32_contains_rectangle(&e->surface->opaque_region, &all) == PIXMAN_REGION_IN;
            }
            if (!opaque) {
                reason = "has alpha and doesn't declare itself opaque";
            }
        }
        if (reason) {
            found = NULL;
        }
    }
    note_scanout_reason(frame, reason);
    return found;
}

static bool try_scanout(struct vela_output_frame *frame, const struct vela_element *e, struct wlr_output_state *state)
{
    struct wlr_output *output = frame->output;
    struct vela_scene *scene = frame->scene;
    if (!wlr_output_is_direct_scanout_allowed(output)) {
        note_scanout_reason(frame, "our own cursor, or a screen capture in progress");
        return false;
    }
    struct wlr_surface *surface = e->surface;
    struct wlr_client_buffer *client = surface->buffer;
    if (!client) {
        note_scanout_reason(frame, "no buffer");
        return false;
    }
    struct wlr_buffer *buffer = &client->base;
    if (client->source && client->source->n_locks > 0) {
        buffer = client->source;
    }
    struct wlr_dmabuf_attributes dmabuf;
    if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        // Il piano dello schermo legge solo dmabuf.
        note_scanout_reason(frame, "the buffer isn't a dmabuf (shared memory)");
        return false;
    }
    if (e->src.x != 0.0 || e->src.y != 0.0 || e->src.width != (double)buffer->width
        || e->src.height != (double)buffer->height) {
        note_scanout_reason(frame, "the app shows only part of it"); // viewporter
        return false;
    }

    struct wlr_output_state attempt;
    wlr_output_state_init(&attempt);
    if (!wlr_output_state_copy(&attempt, state)) {
        return false;
    }
    wlr_output_state_set_buffer(&attempt, buffer);
    // Tearing: l'app (un gioco) vuole ogni frame sullo schermo subito.
    bool tearing = scene->allow_tearing && scene->tearing_control
        && wlr_tearing_control_manager_v1_surface_hint_from_surface(scene->tearing_control, surface)
            == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
    attempt.tearing_page_flip = tearing;
    struct wlr_linux_drm_syncobj_surface_v1_state *sync = wlr_linux_drm_syncobj_v1_get_surface_state(surface);
    if (sync && sync->acquire_timeline) {
        if (!frame->scanout_timeline) {
            wlr_output_state_finish(&attempt);
            note_scanout_reason(frame, "the app uses explicit sync, which this output can't handle (headless)");
            return false;
        }
        wlr_output_state_set_wait_timeline(&attempt, sync->acquire_timeline, sync->acquire_point);
        wlr_output_state_set_signal_timeline(&attempt, frame->scanout_timeline, frame->scanout_point + 1);
    }
    bool ok = wlr_output_test_state(output, &attempt);
    if (!ok && !tearing) {
        note_scanout_reason(frame, "the monitor (or driver) doesn't accept the buffer on its plane");
    }
    if (!ok && tearing) {
        // Lo schermo (o il driver) non lo permette: col vblank, come sempre.
        tearing = false;
        attempt.tearing_page_flip = false;
        ok = wlr_output_test_state(output, &attempt);
    }
    if (ok) {
        const struct vela_surface_state *info = vela_surface_state_get(surface);
        if (!info || info->pacing == output) {
            wlr_presentation_surface_scanned_out_on_output(surface, output);
        }
        ok = wlr_output_commit_state(output, &attempt);
    }
    if (ok && tearing != frame->tearing) {
        wlr_log(WLR_INFO, "%s: tearing %s", output->name, tearing ? "on (the app asks for it)" : "finito");
        frame->tearing = tearing;
    }
    frame->delivered.tearing = ok && tearing;
    if (ok && sync && sync->acquire_timeline) {
        // Il backend fa scattare il punto quando smette di mostrare il buffer.
        ++frame->scanout_point;
        wlr_linux_drm_syncobj_v1_state_add_release_point(sync, frame->scanout_timeline, frame->scanout_point,
            output->event_loop);
    }
    wlr_output_state_finish(&attempt);
    return ok;
}

// Feedback dmabuf: all'app candidata allo scanout i formati del piano
// primario, dopo qualche frame di conferma; poi di nuovo quelli normali.
// Come wlr_scene: prima di cambiare i formati consigliati si aspettano un
// po' di frame di fila, perché ricreare i buffer costa.
static void update_feedback(struct vela_output_frame *frame, const struct vela_element *candidate)
{
    enum { DEBOUNCE_FRAMES = 30 };
    if (!frame->scene->linux_dmabuf) {
        return;
    }
    struct wlr_surface *target = candidate ? candidate->surface : NULL;
    if (target && target == frame->feedback_surface) {
        frame->feedback_debounce = DEBOUNCE_FRAMES;
        return;
    }
    if (target) {
        if (++frame->feedback_debounce < DEBOUNCE_FRAMES) {
            return;
        }
        if (frame->feedback_surface) {
            send_feedback(frame, frame->feedback_surface, false);
        }
        send_feedback(frame, target, true);
        frame->feedback_surface = target;
        frame->feedback_debounce = DEBOUNCE_FRAMES;
        return;
    }
    if (!frame->feedback_surface) {
        frame->feedback_debounce = 0;
        return;
    }
    if (--frame->feedback_debounce > 0) {
        return;
    }
    send_feedback(frame, frame->feedback_surface, false);
    frame->feedback_surface = NULL;
    frame->feedback_debounce = 0;
}

static void send_feedback(struct vela_output_frame *frame, struct wlr_surface *surface, bool scanout)
{
    struct vela_surface_state *info = vela_surface_state_get(surface);
    if (!scanout) {
        wlr_linux_dmabuf_v1_set_surface_feedback(frame->scene->linux_dmabuf, surface, NULL);
        if (info) {
            info->scanout_feedback = NULL;
        }
        wlr_log(WLR_DEBUG, "%s: default dmabuf feedback to a surface", frame->output->name);
        return;
    }
    const struct wlr_linux_dmabuf_feedback_v1_init_options options = {
        .main_renderer = vela_renderer_wlr(frame->renderer),
        .scanout_primary_output = frame->output,
    };
    struct wlr_linux_dmabuf_feedback_v1 feedback = { 0 };
    if (!wlr_linux_dmabuf_feedback_v1_init_with_options(&feedback, &options)) {
        return; // lo schermo non dice quali formati legge il piano primario
    }
    wlr_linux_dmabuf_v1_set_surface_feedback(frame->scene->linux_dmabuf, surface, &feedback);
    wlr_linux_dmabuf_feedback_v1_finish(&feedback);
    if (info) {
        info->scanout_feedback = frame->output;
    }
    wlr_log(WLR_DEBUG, "%s: dmabuf feedback with the scanout tranche to a surface", frame->output->name);
}

void vela_output_frame_send_frame_done(struct vela_output_frame *frame, const struct timespec *when)
{
    for (int i = 0; i < frame->visible_count; ++i) {
        struct wlr_surface *surface = frame->visible_surfaces[i];
        const struct vela_surface_state *state = vela_surface_state_get(surface);
        if (!state || state->pacing == frame->output) {
            wlr_surface_send_frame_done(surface, when);
        }
    }
}
