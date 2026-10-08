// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decoration.h"

#include "icons.h"
#include "render/pixel_buffer.h"
#include "scene/scene.h"
#include "server.h"
#include "text.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/addon.h>

#define HEIGHT VELA_DECORATION_HEIGHT
#define BUTTON_WIDTH VELA_DECORATION_BUTTON_WIDTH
#define GLYPH_SIZE 10 // logici, come i simboli di Windows 11
#define TITLE_MARGIN 12 // senza icona
#define TITLE_AFTER_ICON (VELA_DECORATION_ICON_X + VELA_DECORATION_ICON_SIZE + 8)

// Colori come Windows 11, in tema scuro e chiaro (la modalità delle app).
struct palette {
    struct wlr_render_color active_background;
    struct wlr_render_color inactive_background;
    struct wlr_render_color hover; // premoltiplicato
    uint32_t active_text;
    uint32_t inactive_text;
};

static const struct palette dark_palette = {
    { 0.125f, 0.125f, 0.125f, 1.0f }, // #202020
    { 0.169f, 0.169f, 0.169f, 1.0f }, // #2b2b2b
    { 0.08f, 0.08f, 0.08f, 0.08f }, // bianco all'8%
    0xffffffff,
    0xff9a9a9a,
};

static const struct palette light_palette = {
    { 0.953f, 0.953f, 0.953f, 1.0f }, // #f3f3f3
    { 0.976f, 0.976f, 0.976f, 1.0f }, // #f9f9f9
    { 0.0f, 0.0f, 0.0f, 0.06f }, // nero al 6%
    0xff1a1a1a,
    0xff8f8f8f,
};

static const struct wlr_render_color close_hover_color = { 0.769f, 0.169f, 0.110f, 1.0f }; // #c42b1c
#define CLOSE_HOVER_TEXT 0xffffffffu

static const enum vela_decoration_part button_parts[3] = {
    VELA_DECORATION_MINIMIZE,
    VELA_DECORATION_MAXIMIZE,
    VELA_DECORATION_CLOSE,
};

// Un'immagine della CPU (testo, simbolo) come nodo della scena.
struct image {
    struct vela_buffer_node *node;
    struct wlr_texture *texture;
};

struct vela_decoration {
    struct vela_server *server;
    struct vela_tree *tree;
    struct vela_rect_node *background;
    struct vela_rect_node *hover_rects[3]; // riduci, massimizza, chiudi
    struct image glyphs[3];
    struct image title;
    struct image icon;

    // Ciò che è disegnato adesso.
    int width;
    float scale;
    char *title_text;
    char *app_id;
    bool has_icon;
    uint32_t tint_version; // la tinta dello sfondo con cui è disegnata
    bool active;
    bool maximized;
    bool drawn;
    enum vela_decoration_part hover;
};

static const struct palette *palette(const struct vela_server *server)
{
    return server->light_apps ? &light_palette : &dark_palette;
}

// ---------------------------------------------------------------- simboli --

struct segment {
    double x1, y1, x2, y2;
};

static const struct segment minimize_glyph[] = { { 0.0, 0.5, 1.0, 0.5 } };
static const struct segment maximize_glyph[] = {
    { 0.0, 0.0, 1.0, 0.0 },
    { 1.0, 0.0, 1.0, 1.0 },
    { 1.0, 1.0, 0.0, 1.0 },
    { 0.0, 1.0, 0.0, 0.0 },
};
// "Ripristina": due quadrati sovrapposti.
static const struct segment restore_glyph[] = {
    { 0.0, 0.2, 0.8, 0.2 },
    { 0.8, 0.2, 0.8, 1.0 },
    { 0.8, 1.0, 0.0, 1.0 },
    { 0.0, 1.0, 0.0, 0.2 },
    { 0.2, 0.2, 0.2, 0.0 },
    { 0.2, 0.0, 1.0, 0.0 },
    { 1.0, 0.0, 1.0, 0.8 },
    { 1.0, 0.8, 0.8, 0.8 },
};
static const struct segment close_glyph[] = { { 0.0, 0.0, 1.0, 1.0 }, { 1.0, 0.0, 0.0, 1.0 } };

static const struct segment *glyph(enum vela_decoration_part part, bool maximized, int *count)
{
    switch (part) {
    case VELA_DECORATION_MINIMIZE:
        *count = 1;
        return minimize_glyph;
    case VELA_DECORATION_MAXIMIZE:
        *count = maximized ? 8 : 4;
        return maximized ? restore_glyph : maximize_glyph;
    case VELA_DECORATION_CLOSE:
        *count = 2;
        return close_glyph;
    default:
        *count = 0;
        return NULL;
    }
}

static uint32_t scaled_channel(uint32_t color, int shift, double coverage)
{
    return (uint32_t)lround(((color >> shift) & 0xff) * coverage) << shift;
}

// Un simbolo fatto di segmenti nel quadrato unitario, disegnato a `size`
// pixel con un tratto di `stroke` pixel e bordi antialiasati, premoltiplicato.
static struct vela_image draw_segments(int size, double stroke, uint32_t color, const struct segment *segments,
    int count)
{
    struct vela_image image = { size, size, calloc((size_t)size * (size_t)size, 4) };
    double span = size - stroke; // il tratto resta dentro l'immagine
    double offset = stroke / 2.0;
    double alpha = ((color >> 24) & 0xff) / 255.0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            double px = x + 0.5;
            double py = y + 0.5;
            double distance = 1e9;
            for (int i = 0; i < count; ++i) {
                const struct segment *s = &segments[i];
                double ax = offset + s->x1 * span;
                double ay = offset + s->y1 * span;
                double bx = offset + s->x2 * span;
                double by = offset + s->y2 * span;
                double dx = bx - ax;
                double dy = by - ay;
                double length = dx * dx + dy * dy;
                double t = length > 0 ? vela_clampd(((px - ax) * dx + (py - ay) * dy) / length, 0.0, 1.0) : 0.0;
                distance = fmin(distance, hypot(px - (ax + t * dx), py - (ay + t * dy)));
            }
            double coverage = vela_clampd(stroke / 2.0 + 0.5 - distance, 0.0, 1.0) * alpha;
            if (coverage > 0.0) {
                image.pixels[(size_t)y * (size_t)size + (size_t)x] = (uint32_t)lround(coverage * 255.0) << 24
                    | scaled_channel(color, 16, coverage) | scaled_channel(color, 8, coverage)
                    | scaled_channel(color, 0, coverage);
            }
        }
    }
    return image;
}

static struct vela_image draw_glyph(enum vela_decoration_part part, bool maximized, float scale, uint32_t color)
{
    int size = vela_max(1, (int)lround(GLYPH_SIZE * scale));
    double stroke = fmax(1.0, scale);
    int count = 0;
    const struct segment *segments = glyph(part, maximized, &count);
    return draw_segments(size, stroke, color, segments, count);
}

// ---------------------------------------------------------------- colori --

// I colori della barra con la tinta dello sfondo (Mica, docs/renderer.md
// §9.4): la tinta resa "sicura" per il testo (meno satura, luminosità nella
// fascia scura del tema, o in quella chiara) e mescolata al grigio di
// Windows 11; da inattiva pesa di più. Lo stesso calcolo è in
// shell/src/mica.h, per le app di Vela.
static struct wlr_render_color mica_color(struct wlr_render_color base, const struct vela_server *server,
    float weight)
{
    if (!server->has_wallpaper_tint) {
        return base;
    }
    const float *t = server->wallpaper_tint;
    float luma = 0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2];
    float safe[3];
    for (int i = 0; i < 3; ++i) {
        float desaturated = luma + (t[i] - luma) * 0.5f;
        safe[i] = server->light_apps
            ? vela_clampf(1.0f - (1.0f - desaturated) * (0.10f / fmaxf(1.0f - luma, 0.02f)), 0.82f, 1.0f)
            : vela_clampf(desaturated * (0.16f / fmaxf(luma, 0.02f)), 0.0f, 0.32f);
    }
    return (struct wlr_render_color) {
        base.r + (safe[0] - base.r) * weight,
        base.g + (safe[1] - base.g) * weight,
        base.b + (safe[2] - base.b) * weight,
        1.0f,
    };
}

// --------------------------------------------------------------- immagini --

// La texture di un'immagine della barra vive quanto il suo buffer, non
// quanto l'immagine: un'istantanea (animazioni di finestra) può tenere il
// buffer bloccato dopo che la barra l'ha già sostituito.
struct texture_owner {
    struct wlr_addon addon;
    struct wlr_texture *texture;
};

static void destroy_texture_owner(struct wlr_addon *addon)
{
    struct texture_owner *owner = wl_container_of(addon, owner, addon);
    wlr_texture_destroy(owner->texture);
    wlr_addon_finish(addon);
    free(owner);
}

static const struct wlr_addon_interface texture_owner_impl = {
    .name = "vela-decoration-texture",
    .destroy = destroy_texture_owner,
};

static void clear_image(struct image *image)
{
    // Sblocca il buffer; la texture se ne va con lui (texture_owner).
    if (image->node) {
        vela_node_destroy(&image->node->node);
    }
    image->node = NULL;
    image->texture = NULL;
}

// Prende in carico i pixel di `pixels`.
static void set_image(struct vela_decoration *decoration, struct image *image, struct vela_image *pixels, double x,
    double y, double logical_width, double logical_height)
{
    clear_image(image);
    int width = pixels->width;
    int height = pixels->height;
    struct wlr_buffer *buffer = vela_pixel_buffer_create(pixels);
    image->texture = wlr_texture_from_buffer(decoration->server->wlr_renderer, buffer);
    if (image->texture) {
        struct texture_owner *owner = calloc(1, sizeof(*owner));
        owner->texture = image->texture;
        wlr_addon_init(&owner->addon, &buffer->addons, owner, &texture_owner_impl);
        const struct wlr_fbox src = { 0, 0, width, height };
        image->node = vela_buffer_node_create(decoration->tree, buffer, image->texture, &src,
            WL_OUTPUT_TRANSFORM_NORMAL, logical_width, logical_height);
        vela_node_set_position(&image->node->node, x, y);
        image->node->node.hittable = true;
    }
    wlr_buffer_drop(buffer);
}

// Il testo delle barre, caricato al primo uso (NULL: nessun font).
static struct vela_text *text_engine(struct vela_server *server)
{
    if (!server->text_loaded) {
        server->text_loaded = true;
        server->text = vela_text_create();
    }
    return server->text;
}

static struct vela_icons *icon_loader(struct vela_server *server)
{
    if (!server->icons) {
        server->icons = vela_icons_create();
    }
    return server->icons;
}

static void layout_buttons(struct vela_decoration *decoration)
{
    for (int i = 0; i < 3; ++i) {
        double x = decoration->width - (3 - i) * BUTTON_WIDTH;
        vela_node_set_position(&decoration->hover_rects[i]->node, x, 0);
        if (decoration->glyphs[i].node) {
            vela_node_set_position(&decoration->glyphs[i].node->node, x + (BUTTON_WIDTH - GLYPH_SIZE) / 2.0,
                (HEIGHT - GLYPH_SIZE) / 2.0);
        }
    }
}

static bool same_text(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

// ------------------------------------------------------------------ barra --

void vela_decoration_update(struct vela_decoration *decoration, const struct vela_decoration_state *state)
{
    struct vela_server *server = decoration->server;
    vela_node_set_enabled(&decoration->tree->node, !state->fullscreen);
    // In cima al riquadro della finestra (le app Wayland possono avere la
    // geometria spostata rispetto all'origine della superficie).
    vela_node_set_position(&decoration->tree->node, state->geometry.x, state->geometry.y);
    int width = state->geometry.width;
    float scale = state->scale;
    bool active = state->active;
    bool maximized = state->maximized;
    bool resized = width != decoration->width;
    bool restyled = !decoration->drawn || scale != decoration->scale || active != decoration->active
        || decoration->tint_version != server->wallpaper_tint_version;
    bool new_app = !same_text(state->app_id, decoration->app_id);
    bool new_title = !same_text(state->title, decoration->title_text);
    decoration->width = width;
    if (resized) {
        vela_rect_node_set_size(decoration->background, width, HEIGHT);
    }
    const struct palette *colors = palette(server);
    uint32_t text_color = active ? colors->active_text : colors->inactive_text;
    if (restyled) {
        struct wlr_render_color background = active ? mica_color(colors->active_background, server, 0.30f)
                                                    : mica_color(colors->inactive_background, server, 0.45f);
        vela_rect_node_set_color(decoration->background, &background);
    }

    // L'icona dell'app, disegnata alla dimensione fisica esatta (§9.6).
    if (restyled || new_app) {
        int pixels = vela_max(1, (int)lround(VELA_DECORATION_ICON_SIZE * scale));
        const struct vela_image *icon = vela_icons_app(icon_loader(server), state->app_id, pixels);
        decoration->has_icon = icon->pixels != NULL;
        if (decoration->has_icon) {
            size_t bytes = (size_t)icon->width * (size_t)icon->height * 4;
            struct vela_image copy = { icon->width, icon->height, malloc(bytes) };
            memcpy(copy.pixels, icon->pixels, bytes);
            set_image(decoration, &decoration->icon, &copy, VELA_DECORATION_ICON_X,
                (HEIGHT - VELA_DECORATION_ICON_SIZE) / 2.0, VELA_DECORATION_ICON_SIZE, VELA_DECORATION_ICON_SIZE);
            vela_node_set_opacity(&decoration->icon.node->node, active ? 1.0f : 0.6f);
        } else {
            clear_image(&decoration->icon);
        }
    }
    int title_x = decoration->has_icon ? TITLE_AFTER_ICON : TITLE_MARGIN;

    // Il titolo, rasterizzato ai pixel fisici dello schermo.
    struct vela_text *text = text_engine(server);
    if (text && (restyled || resized || new_title || new_app)) {
        int max_width = (int)((width - title_x - 3 * BUTTON_WIDTH - 8) * scale);
        if (max_width > 0 && state->title[0]) {
            struct vela_image image = { 0 };
            vela_text_render(text, state->title, vela_text_pixel_size(text) * scale, text_color, max_width, &image);
            double logical_height = image.height / (double)scale;
            double logical_width = image.width / (double)scale;
            set_image(decoration, &decoration->title, &image, title_x, round((HEIGHT - logical_height) / 2.0),
                logical_width, logical_height);
        } else {
            clear_image(&decoration->title);
        }
    }

    // I simboli dei pulsanti.
    if (restyled || maximized != decoration->maximized) {
        for (int i = 0; i < 3; ++i) {
            // Sul rosso di Chiudi il simbolo è sempre bianco.
            bool close_hovered
                = button_parts[i] == VELA_DECORATION_CLOSE && decoration->hover == VELA_DECORATION_CLOSE;
            struct vela_image pixels
                = draw_glyph(button_parts[i], maximized, scale, close_hovered ? CLOSE_HOVER_TEXT : text_color);
            set_image(decoration, &decoration->glyphs[i], &pixels, 0, 0, GLYPH_SIZE, GLYPH_SIZE);
        }
    }
    if (resized || restyled || maximized != decoration->maximized) {
        layout_buttons(decoration);
    }

    decoration->scale = scale;
    decoration->active = active;
    decoration->maximized = maximized;
    if (new_title) {
        free(decoration->title_text);
        decoration->title_text = strdup(state->title);
    }
    if (new_app) {
        free(decoration->app_id);
        decoration->app_id = strdup(state->app_id);
    }
    decoration->tint_version = server->wallpaper_tint_version;
    decoration->drawn = true;
}

struct vela_decoration *vela_decoration_create(struct vela_server *server, struct vela_tree *parent,
    const struct vela_decoration_state *state)
{
    struct vela_decoration *decoration = calloc(1, sizeof(*decoration));
    decoration->server = server;
    decoration->width = -1;
    decoration->tree = vela_tree_create(parent);
    decoration->background = vela_rect_node_create(decoration->tree, 0, HEIGHT, &dark_palette.active_background);
    // Sopra la superficie dell'app, fuori dalla sua area.
    vela_node_set_position(&decoration->tree->node, 0, -HEIGHT);
    decoration->background->node.hittable = true;
    const struct wlr_render_color none = { 0 };
    for (int i = 0; i < 3; ++i) {
        decoration->hover_rects[i] = vela_rect_node_create(decoration->tree, BUTTON_WIDTH, HEIGHT, &none);
        decoration->hover_rects[i]->node.hittable = true;
    }
    vela_decoration_update(decoration, state);
    return decoration;
}

void vela_decoration_destroy(struct vela_decoration *decoration)
{
    if (!decoration) {
        return;
    }
    clear_image(&decoration->icon);
    clear_image(&decoration->title);
    for (int i = 0; i < 3; ++i) {
        clear_image(&decoration->glyphs[i]);
    }
    for (int i = 2; i >= 0; --i) {
        vela_node_destroy(&decoration->hover_rects[i]->node);
    }
    vela_node_destroy(&decoration->background->node);
    vela_node_destroy(&decoration->tree->node);
    free(decoration->title_text);
    free(decoration->app_id);
    free(decoration);
}

enum vela_decoration_part vela_decoration_part_at(const struct vela_decoration *decoration, double lx, double ly)
{
    double ox = 0.0;
    double oy = 0.0;
    vela_node_coords(&decoration->tree->node, &ox, &oy);
    double x = lx - ox;
    double y = ly - oy;
    int width = decoration->width;
    if (y < 0 || y >= HEIGHT || x < 0 || x >= width) {
        return VELA_DECORATION_NONE;
    }
    if (x >= width - BUTTON_WIDTH) {
        return VELA_DECORATION_CLOSE;
    }
    if (x >= width - 2 * BUTTON_WIDTH) {
        return VELA_DECORATION_MAXIMIZE;
    }
    if (x >= width - 3 * BUTTON_WIDTH) {
        return VELA_DECORATION_MINIMIZE;
    }
    if (decoration->has_icon && x >= VELA_DECORATION_ICON_X - 6
        && x < VELA_DECORATION_ICON_X + VELA_DECORATION_ICON_SIZE + 6) {
        return VELA_DECORATION_ICON;
    }
    return VELA_DECORATION_TITLE;
}

struct wlr_box vela_decoration_maximize_box(const struct vela_decoration *decoration)
{
    double ox = 0.0;
    double oy = 0.0;
    vela_node_coords(&decoration->tree->node, &ox, &oy);
    return (struct wlr_box) { (int)lround(ox) + decoration->width - 2 * BUTTON_WIDTH, (int)lround(oy), BUTTON_WIDTH,
        HEIGHT };
}

void vela_decoration_set_hover(struct vela_decoration *decoration, enum vela_decoration_part part)
{
    if (part == decoration->hover) {
        return;
    }
    bool close_changed = (part == VELA_DECORATION_CLOSE) != (decoration->hover == VELA_DECORATION_CLOSE);
    decoration->hover = part;
    const struct palette *colors = palette(decoration->server);
    for (int i = 0; i < 3; ++i) {
        bool hovered = button_parts[i] == part;
        bool close = button_parts[i] == VELA_DECORATION_CLOSE;
        struct wlr_render_color hover = { 0 };
        if (hovered) {
            hover = close ? close_hover_color : colors->hover;
        }
        vela_rect_node_set_color(decoration->hover_rects[i], &hover);
    }
    // Sul rosso il simbolo di Chiudi diventa bianco (in tema scuro lo è già,
    // da attiva).
    if (close_changed && decoration->drawn && (decoration->server->light_apps || !decoration->active)) {
        float scale = decoration->scale > 0.0f ? decoration->scale : 1.0f;
        uint32_t color = part == VELA_DECORATION_CLOSE ? CLOSE_HOVER_TEXT
            : decoration->active                       ? colors->active_text
                                                       : colors->inactive_text;
        struct vela_image pixels = draw_glyph(VELA_DECORATION_CLOSE, decoration->maximized, scale, color);
        set_image(decoration, &decoration->glyphs[2], &pixels, 0, 0, GLYPH_SIZE, GLYPH_SIZE);
        layout_buttons(decoration);
    }
}
