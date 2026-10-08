// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapshot.h"

#include "motion.h"
#include "scene/scene.h"
#include "server.h"
#include "util.h"
#include "view.h"

#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/util/transform.h>

// A piece of the snapshot: a copy of a buffer or of a rectangle.
struct piece {
    struct vela_node *node; // vela_buffer_node or vela_rect_node
    double x, y; // relative to the frame's center
    double width, height;
};

struct vela_snapshot {
    struct vela_tree *tree;
    struct wlr_box frame;
    struct piece *pieces;
    int count;
    int capacity;
    struct vela_shape shape; // the window's (corners, shadow), scaled with it
};

static void add_piece(struct vela_snapshot *snapshot, struct vela_node *node, double x, double y, double width,
    double height)
{
    snapshot->pieces = vela_grow(snapshot->pieces, &snapshot->capacity, snapshot->count + 1, sizeof(struct piece));
    snapshot->pieces[snapshot->count++] = (struct piece) { node, x, y, width, height };
}

// The surfaces of a surface node, copied as frozen buffers.
struct surface_copy {
    struct vela_snapshot *snapshot;
    double lx, ly; // where the node is, minus the frame's center
};

static void add_surface_copy(struct wlr_surface *surface, int sx, int sy, void *data)
{
    struct surface_copy *context = data;
    // The surface's buffer (already uploaded to a texture): locked by the
    // node, it stays valid whatever the app does.
    struct wlr_client_buffer *buffer = surface->buffer;
    if (!buffer || !buffer->texture) {
        return;
    }
    struct wlr_fbox src = { 0 };
    wlr_surface_get_buffer_source_box(surface, &src);
    double width = surface->current.width;
    double height = surface->current.height;
    struct vela_buffer_node *copy = vela_buffer_node_create(context->snapshot->tree, &buffer->base, buffer->texture,
        &src, wlr_output_transform_invert(surface->current.transform), width, height);
    add_piece(context->snapshot, &copy->node, context->lx + sx, context->ly + sy, width, height);
}

// For surfaces, whether nodes are enabled doesn't matter: on close the surface
// is already unmapped and with the window minimized the tree is off, but the
// buffers stay. A surface really hidden by the app has no buffer and stays
// out. The title bar (the compositor's rectangles and images) instead enters
// only with what is visible: no buttons that aren't highlighted, no bar when
// fullscreen (`hidden`: a disabled tree under the window).
static void collect(struct vela_snapshot *snapshot, struct vela_node *node, double lx, double ly, bool hidden)
{
    lx += node->x;
    ly += node->y;
    double cx = snapshot->frame.x + snapshot->frame.width / 2.0;
    double cy = snapshot->frame.y + snapshot->frame.height / 2.0;

    switch (node->type) {
    case VELA_NODE_TREE: {
        struct vela_tree *tree = (struct vela_tree *)node;
        struct vela_node *child;
        wl_list_for_each (child, &tree->children, link) {
            collect(snapshot, child, lx, ly, hidden || (child->type == VELA_NODE_TREE && !child->enabled));
        }
        return;
    }
    case VELA_NODE_RECT: {
        if (hidden || !node->enabled) {
            return;
        }
        const struct vela_rect_node *rect = (const struct vela_rect_node *)node;
        struct vela_rect_node *copy = vela_rect_node_create(snapshot->tree, rect->width, rect->height, &rect->color);
        add_piece(snapshot, &copy->node, lx - cx, ly - cy, rect->width, rect->height);
        return;
    }
    case VELA_NODE_BUFFER: {
        if (hidden || !node->enabled) {
            return;
        }
        const struct vela_buffer_node *image = (const struct vela_buffer_node *)node;
        struct vela_buffer_node *copy = vela_buffer_node_create(snapshot->tree, image->buffer, image->texture,
            &image->src, image->transform, image->width, image->height);
        add_piece(snapshot, &copy->node, lx - cx, ly - cy, image->width, image->height);
        return;
    }
    case VELA_NODE_SURFACE: {
        struct surface_copy context = { snapshot, lx - cx, ly - cy };
        vela_for_each_surface(((struct vela_surface_node *)node)->surface, add_surface_copy, &context);
        return;
    }
    }
}

// Copies the buffers under `source`, even if it's disabled (as happens on
// close and when minimized). `frame` is the window's frame in global
// coordinates: transformations happen around its center.
static struct vela_snapshot *snapshot_create(struct vela_tree *parent, struct vela_node *source, struct wlr_box frame)
{
    struct vela_snapshot *snapshot = calloc(1, sizeof(*snapshot));
    snapshot->tree = vela_tree_create(parent);
    snapshot->frame = frame;
    double lx = 0.0;
    double ly = 0.0;
    if (source->parent) {
        vela_node_coords(&source->parent->node, &lx, &ly);
    }
    collect(snapshot, source, lx, ly, false);
    // The window's corners and shadow follow it in the animation.
    if (source->type == VELA_NODE_TREE) {
        snapshot->shape = ((struct vela_tree *)source)->shape;
    }
    if (source->parent == parent) {
        vela_node_place_above(&snapshot->tree->node, source);
    }
    wlr_log(WLR_DEBUG, "Snapshot: %d buffers", snapshot->count);
    return snapshot;
}

static void snapshot_destroy(struct vela_snapshot *snapshot)
{
    for (int i = 0; i < snapshot->count; ++i) {
        vela_node_destroy(snapshot->pieces[i].node);
    }
    free(snapshot->pieces);
    vela_node_destroy(&snapshot->tree->node);
    free(snapshot);
}

// Draws the snapshot with the frame's center at (cx, cy), scaled (in width and
// height, to maximize and restore) and faded.
static void snapshot_apply(struct vela_snapshot *snapshot, double cx, double cy, double scale_x, double scale_y,
    float opacity)
{
    for (int i = 0; i < snapshot->count; ++i) {
        const struct piece *piece = &snapshot->pieces[i];
        vela_node_set_position(piece->node, cx + piece->x * scale_x, cy + piece->y * scale_y);
        if (piece->node->type == VELA_NODE_RECT) {
            vela_rect_node_set_size((struct vela_rect_node *)piece->node, piece->width * scale_x,
                piece->height * scale_y);
        } else {
            vela_buffer_node_set_size((struct vela_buffer_node *)piece->node, piece->width * scale_x,
                piece->height * scale_y);
        }
    }
    vela_node_set_opacity(&snapshot->tree->node, opacity);
    if (snapshot->shape.enabled) {
        struct vela_shape shape = snapshot->shape;
        shape.width = snapshot->frame.width * scale_x;
        shape.height = snapshot->frame.height * scale_y;
        shape.x = cx - shape.width / 2.0;
        shape.y = cy - shape.height / 2.0;
        shape.radius = snapshot->shape.radius * fmin(scale_x, scale_y);
        vela_tree_set_shape(snapshot->tree, &shape);
    }
}

// ------------------------------------------------------------ animations --

struct animation {
    struct wl_list link; // vela_server.snapshot_animations
    enum vela_snapshot_kind kind;
    struct vela_view *owner; // the window, while it exists (not for closing)
    struct vela_snapshot *snapshot;
    struct vela_tween tween;
    double from_x, from_y, to_x, to_y; // center
    double from_scale, to_scale;
    double from_scale_y, to_scale_y; // morph: the height scales on its own
    float from_opacity, to_opacity;
};

static double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

static void animation_destroy(struct animation *animation)
{
    wl_list_remove(&animation->link);
    snapshot_destroy(animation->snapshot);
    free(animation);
}

void vela_snapshots_init(struct vela_server *server)
{
    wl_list_init(&server->snapshot_animations);
}

void vela_snapshots_finish(struct vela_server *server)
{
    struct animation *animation, *next;
    wl_list_for_each_safe (animation, next, &server->snapshot_animations, link) {
        animation_destroy(animation);
    }
}

bool vela_snapshots_running(const struct vela_server *server)
{
    return !wl_list_empty(&server->snapshot_animations);
}

void vela_snapshot_cancel(struct vela_server *server, struct vela_view *view)
{
    struct animation *animation, *next;
    wl_list_for_each_safe (animation, next, &server->snapshot_animations, link) {
        if (animation->owner != view) {
            continue;
        }
        if (animation->kind == VELA_SNAPSHOT_MORPH) {
            vela_node_set_opacity(&view->tree->node, 1.0f);
        }
        animation_destroy(animation);
    }
}

static struct animation *animation_create(struct vela_view *view, enum vela_snapshot_kind kind, struct wlr_box frame)
{
    struct vela_tree *tree = view->tree;
    struct animation *animation = calloc(1, sizeof(*animation));
    animation->kind = kind;
    animation->owner = kind == VELA_SNAPSHOT_CLOSE ? NULL : view;
    animation->snapshot = snapshot_create(tree->node.parent, &tree->node, frame);
    animation->from_x = frame.x + frame.width / 2.0;
    animation->from_y = frame.y + frame.height / 2.0;
    animation->to_x = animation->from_x;
    animation->to_y = animation->from_y;
    animation->from_scale = 1.0;
    animation->to_scale = 1.0;
    animation->from_scale_y = 1.0;
    animation->to_scale_y = 1.0;
    animation->from_opacity = 1.0f;
    animation->to_opacity = 0.0f;
    return animation;
}

bool vela_snapshot_animate(struct vela_server *server, struct vela_view *view, enum vela_snapshot_kind kind)
{
    struct wlr_box frame = vela_view_frame_box(view);
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    vela_snapshot_cancel(server, view);
    if (kind == VELA_SNAPSHOT_MORPH) {
        return false; // vela_snapshot_morph
    }
    struct animation *a = animation_create(view, kind, frame);
    if (kind == VELA_SNAPSHOT_CLOSE) {
        vela_tween_start(&a->tween, VELA_WINDOW_CLOSE_MS, &vela_decelerate);
        a->to_scale = VELA_WINDOW_CLOSE_SCALE;
    } else {
        struct wlr_box target = vela_view_minimize_target(view);
        vela_tween_start(&a->tween, VELA_WINDOW_MINIMIZE_MS, &vela_decelerate);
        a->to_x = target.x + target.width / 2.0;
        a->to_y = target.y + target.height / 2.0;
        a->to_scale = VELA_WINDOW_MINIMIZE_SCALE;
        if (kind == VELA_SNAPSHOT_RESTORE) {
            // The same flight, reversed.
            double x = a->from_x, y = a->from_y, scale = a->from_scale;
            float opacity = a->from_opacity;
            a->from_x = a->to_x;
            a->from_y = a->to_y;
            a->from_scale = a->to_scale;
            a->from_opacity = a->to_opacity;
            a->to_x = x;
            a->to_y = y;
            a->to_scale = scale;
            a->to_opacity = opacity;
        }
    }
    a->from_scale_y = a->from_scale;
    a->to_scale_y = a->to_scale;
    snapshot_apply(a->snapshot, a->from_x, a->from_y, a->from_scale, a->from_scale, a->from_opacity);
    wl_list_insert(server->snapshot_animations.prev, &a->link);
    vela_server_schedule_frames(server);
    return true;
}

bool vela_snapshot_morph(struct vela_server *server, struct vela_view *view, struct wlr_box to)
{
    struct wlr_box from = vela_view_frame_box(view);
    if (from.width <= 0 || from.height <= 0 || to.width <= 0 || to.height <= 0) {
        return false;
    }
    vela_snapshot_cancel(server, view);
    struct animation *a = animation_create(view, VELA_SNAPSHOT_MORPH, from);
    a->tween = (struct vela_tween) { -1.0, VELA_WINDOW_MAXIMIZE_MS, &vela_decelerate };
    a->to_x = to.x + to.width / 2.0;
    a->to_y = to.y + to.height / 2.0;
    a->to_scale = (double)to.width / from.width;
    a->to_scale_y = (double)to.height / from.height;
    snapshot_apply(a->snapshot, a->from_x, a->from_y, 1.0, 1.0, 1.0f);
    // The real window, already in its new place, stays invisible under the
    // snapshot until the app has redrawn.
    vela_node_set_opacity(&view->tree->node, 0.0f);
    wl_list_insert(server->snapshot_animations.prev, &a->link);
    vela_server_schedule_frames(server);
    return true;
}

bool vela_snapshots_tick(struct vela_server *server, double now_ms)
{
    struct animation *a, *next;
    wl_list_for_each_safe (a, next, &server->snapshot_animations, link) {
        double p = vela_tween_progress(&a->tween, now_ms);
        // Opacity runs ahead of motion: what comes in is readable at once,
        // what goes out has vanished before arriving.
        double fade = fmin(1.0, p * 1.4);
        if (a->kind == VELA_SNAPSHOT_MORPH) {
            // The old content stays opaque for half the trip, then fades;
            // meanwhile the real window appears under it, already redrawn at
            // the new size.
            fade = vela_clampd((p - 0.5) / 0.5, 0.0, 1.0);
            vela_node_set_opacity(&a->owner->tree->node, (float)vela_clampd((p - 0.25) / 0.6, 0.0, 1.0));
        }
        snapshot_apply(a->snapshot, lerp(a->from_x, a->to_x, p), lerp(a->from_y, a->to_y, p),
            lerp(a->from_scale, a->to_scale, p), lerp(a->from_scale_y, a->to_scale_y, p),
            (float)lerp(a->from_opacity, a->to_opacity, fade));
        if (!vela_tween_finished(&a->tween, now_ms)) {
            continue;
        }
        struct vela_view *owner = a->owner;
        bool restored = a->kind == VELA_SNAPSHOT_RESTORE;
        if (a->kind == VELA_SNAPSHOT_MORPH) {
            vela_node_set_opacity(&owner->tree->node, 1.0f);
        }
        animation_destroy(a);
        if (restored && owner) {
            vela_view_finish_restore(owner);
        }
    }
    return vela_snapshots_running(server);
}
