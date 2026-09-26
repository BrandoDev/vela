#include "server.hpp"

#include <algorithm>

namespace vela {

// -------------------------------------------------------------- Snapshot --

Snapshot::Snapshot(wlr_scene_tree* parent, wlr_scene_node* source, wlr_box frame)
    : m_tree(wlr_scene_tree_create(parent))
    , m_frame(frame)
{
    int lx = 0;
    int ly = 0;
    if (source->parent) {
        wlr_scene_node_coords(&source->parent->node, &lx, &ly);
    }
    collect(source, lx, ly);
    wlr_scene_node_place_above(&m_tree->node, source);
    wlr_log(WLR_DEBUG, "Istantanea: %zu buffer", m_pieces.size());
}

Snapshot::~Snapshot()
{
    wlr_scene_node_destroy(&m_tree->node);
}

// Non si guarda se i nodi sono abilitati: alla chiusura (e con la finestra
// ridotta a icona) wlroots disabilita tutto l'albero, ma i buffer restano.
// Una superficie davvero nascosta dall'app non ha buffer e resta fuori.
void Snapshot::collect(wlr_scene_node* node, int lx, int ly)
{
    lx += node->x;
    ly += node->y;

    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_node* child;
        wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link)
        {
            collect(child, lx, ly);
        }
        return;
    }
    if (node->type != WLR_SCENE_NODE_BUFFER) {
        return;
    }

    wlr_scene_buffer* source = wlr_scene_buffer_from_node(node);
    if (!source->buffer) {
        return;
    }
    int width = source->dst_width;
    int height = source->dst_height;
    if (width <= 0 || height <= 0) {
        width = source->buffer->width;
        height = source->buffer->height;
        if (source->transform & WL_OUTPUT_TRANSFORM_90) {
            std::swap(width, height);
        }
    }

    // wlr_scene_buffer_create blocca il buffer: resta valido finché esiste
    // la copia, qualunque cosa faccia l'app.
    wlr_scene_buffer* copy = wlr_scene_buffer_create(m_tree, source->buffer);
    if (!copy) {
        return;
    }
    wlr_scene_buffer_set_transform(copy, source->transform);
    if (!wlr_fbox_empty(&source->src_box)) {
        wlr_scene_buffer_set_source_box(copy, &source->src_box);
    }
    wlr_scene_buffer_set_transfer_function(copy, source->transfer_function);
    wlr_scene_buffer_set_primaries(copy, source->primaries);
    // Un'istantanea non riceve clic: passano alla finestra sotto.
    copy->point_accepts_input = [](wlr_scene_buffer*, double*, double*) { return false; };

    const double cx = m_frame.x + m_frame.width / 2.0;
    const double cy = m_frame.y + m_frame.height / 2.0;
    m_pieces.push_back({ copy, lx - cx, ly - cy, width, height });
}

void Snapshot::apply(double cx, double cy, double scale, float opacity)
{
    for (const Piece& piece : m_pieces) {
        wlr_scene_node_set_position(&piece.buffer->node,
            static_cast<int>(std::lround(cx + piece.x * scale)),
            static_cast<int>(std::lround(cy + piece.y * scale)));
        wlr_scene_buffer_set_dest_size(piece.buffer,
            std::max(1, static_cast<int>(std::lround(piece.width * scale))),
            std::max(1, static_cast<int>(std::lround(piece.height * scale))));
        wlr_scene_buffer_set_opacity(piece.buffer, opacity);
    }
}

// ------------------------------------------------------------ animazioni --

namespace {

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

} // namespace

bool Server::animateSnapshot(Toplevel* toplevel, SnapshotKind kind)
{
    const wlr_box frame = toplevel->frameBox();
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    cancelSnapshotAnimations(toplevel);

    SnapshotAnimation animation {
        .kind = kind,
        .owner = kind == SnapshotKind::Close ? nullptr : toplevel,
        .snapshot = std::make_unique<Snapshot>(toplevel->tree->node.parent, &toplevel->tree->node, frame),
        .tween = {},
        .fromX = frame.x + frame.width / 2.0,
        .fromY = frame.y + frame.height / 2.0,
        .toX = 0,
        .toY = 0,
        .fromScale = 1.0,
        .toScale = 1.0,
        .fromOpacity = 1.0f,
        .toOpacity = 0.0f,
    };
    animation.toX = animation.fromX;
    animation.toY = animation.fromY;

    switch (kind) {
    case SnapshotKind::Close:
        animation.tween = Tween(motion::windowCloseMs, &motion::decelerate);
        animation.toScale = motion::windowCloseScale;
        break;
    case SnapshotKind::Minimize:
    case SnapshotKind::Restore: {
        const wlr_box target = toplevel->minimizeTarget();
        animation.tween = Tween(motion::windowMinimizeMs, &motion::decelerate);
        animation.toX = target.x + target.width / 2.0;
        animation.toY = target.y + target.height / 2.0;
        animation.toScale = motion::windowMinimizeScale;
        if (kind == SnapshotKind::Restore) {
            // Lo stesso volo, al contrario.
            std::swap(animation.fromX, animation.toX);
            std::swap(animation.fromY, animation.toY);
            std::swap(animation.fromScale, animation.toScale);
            std::swap(animation.fromOpacity, animation.toOpacity);
        }
        break;
    }
    }

    animation.snapshot->apply(animation.fromX, animation.fromY, animation.fromScale, animation.fromOpacity);
    m_snapshotAnimations.push_back(std::move(animation));
    scheduleFrames();
    return true;
}

void Server::cancelSnapshotAnimations(Toplevel* toplevel)
{
    m_snapshotAnimations.remove_if([toplevel](const SnapshotAnimation& animation) {
        return animation.owner == toplevel;
    });
}

void Server::tickSnapshotAnimations(double nowMs)
{
    for (auto it = m_snapshotAnimations.begin(); it != m_snapshotAnimations.end();) {
        SnapshotAnimation& a = *it;
        const double p = a.tween.progress(nowMs);
        // L'opacità corre più del movimento: chi entra è leggibile subito,
        // chi esce è già sparito prima di arrivare.
        const double fade = std::min(1.0, p * 1.4);
        a.snapshot->apply(lerp(a.fromX, a.toX, p), lerp(a.fromY, a.toY, p),
            lerp(a.fromScale, a.toScale, p),
            static_cast<float>(lerp(a.fromOpacity, a.toOpacity, fade)));

        if (!a.tween.finished(nowMs)) {
            ++it;
            continue;
        }
        Toplevel* owner = a.owner;
        const bool restored = a.kind == SnapshotKind::Restore;
        it = m_snapshotAnimations.erase(it);
        if (restored && owner) {
            owner->finishRestore();
        }
    }
}

} // namespace vela
