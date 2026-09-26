#include "server.hpp"

#include <algorithm>

namespace vela {

// -------------------------------------------------------------- Snapshot --

Snapshot::Snapshot(scene::Tree* parent, scene::Node* source, wlr_box frame)
    : m_tree(std::make_unique<scene::Tree>(parent))
    , m_frame(frame)
{
    double lx = 0.0;
    double ly = 0.0;
    if (source->parent()) {
        source->parent()->coords(lx, ly);
    }
    collect(source, lx, ly);
    if (source->parent() == parent) {
        m_tree->placeAbove(source);
    }
    wlr_log(WLR_DEBUG, "Istantanea: %zu buffer", m_pieces.size());
}

Snapshot::~Snapshot() = default;

// Non si guarda se i nodi sono abilitati: alla chiusura la superficie è già
// "smappata" e con la finestra ridotta a icona l'albero è spento, ma i
// buffer restano. Una superficie davvero nascosta dall'app non ha buffer e
// resta fuori.
void Snapshot::collect(scene::Node* node, double lx, double ly)
{
    lx += node->x();
    ly += node->y();

    if (node->type() == scene::Node::Type::Tree) {
        for (scene::Node* child : static_cast<scene::Tree*>(node)->children()) {
            collect(child, lx, ly);
        }
        return;
    }
    if (node->type() != scene::Node::Type::Surface) {
        return;
    }

    const double cx = m_frame.x + m_frame.width / 2.0;
    const double cy = m_frame.y + m_frame.height / 2.0;
    scene::forEachSurface(static_cast<scene::SurfaceNode*>(node)->surface(), [&](wlr_surface* surface, int sx, int sy) {
        // Il buffer della superficie (già caricato in una texture): bloccato
        // dal nodo, resta valido qualunque cosa faccia l'app.
        wlr_client_buffer* buffer = surface->buffer;
        if (!buffer || !buffer->texture) {
            return;
        }
        wlr_fbox src {};
        wlr_surface_get_buffer_source_box(surface, &src);
        const double width = surface->current.width;
        const double height = surface->current.height;
        auto copy = std::make_unique<scene::BufferNode>(m_tree.get(), &buffer->base, buffer->texture, src,
            wlr_output_transform_invert(surface->current.transform), width, height);
        m_pieces.push_back({ std::move(copy), lx + sx - cx, ly + sy - cy, width, height });
    });
}

void Snapshot::apply(double cx, double cy, double scale, float opacity)
{
    for (const Piece& piece : m_pieces) {
        piece.node->setPosition(cx + piece.x * scale, cy + piece.y * scale);
        piece.node->setSize(piece.width * scale, piece.height * scale);
    }
    m_tree->setOpacity(opacity);
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
        .snapshot = std::make_unique<Snapshot>(toplevel->tree->parent(), toplevel->tree.get(), frame),
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
