// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

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
    collect(source, lx, ly, false);
    // Gli angoli e l'ombra della finestra la seguono nell'animazione.
    if (source->type() == scene::Node::Type::Tree) {
        m_shape = static_cast<scene::Tree*>(source)->shape();
    }
    if (source->parent() == parent) {
        m_tree->placeAbove(source);
    }
    wlr_log(WLR_DEBUG, "Istantanea: %zu buffer", m_pieces.size());
}

Snapshot::~Snapshot() = default;

// Per le superfici non si guarda se i nodi sono abilitati: alla chiusura la
// superficie è già "smappata" e con la finestra ridotta a icona l'albero è
// spento, ma i buffer restano. Una superficie davvero nascosta dall'app non
// ha buffer e resta fuori. La barra del titolo (rettangoli e immagini del
// compositor) invece entra solo con ciò che si vede: niente pulsanti non
// evidenziati, niente barra a schermo intero (`hidden`: un albero spento
// sotto la finestra).
void Snapshot::collect(scene::Node* node, double lx, double ly, bool hidden)
{
    lx += node->x();
    ly += node->y();
    const double cx = m_frame.x + m_frame.width / 2.0;
    const double cy = m_frame.y + m_frame.height / 2.0;

    switch (node->type()) {
    case scene::Node::Type::Tree: {
        for (scene::Node* child : static_cast<scene::Tree*>(node)->children()) {
            collect(child, lx, ly, hidden || (child->type() == scene::Node::Type::Tree && !child->enabled()));
        }
        return;
    }
    case scene::Node::Type::Rect: {
        if (hidden || !node->enabled()) {
            return;
        }
        const auto* rect = static_cast<const scene::RectNode*>(node);
        auto copy = std::make_unique<scene::RectNode>(m_tree.get(), rect->width(), rect->height(), rect->color());
        m_pieces.push_back({ std::move(copy), lx - cx, ly - cy, rect->width(), rect->height() });
        return;
    }
    case scene::Node::Type::Buffer: {
        if (hidden || !node->enabled()) {
            return;
        }
        const auto* image = static_cast<const scene::BufferNode*>(node);
        auto copy = std::make_unique<scene::BufferNode>(m_tree.get(), image->buffer(), image->texture(), image->src(),
            image->transform(), image->width(), image->height());
        m_pieces.push_back({ std::move(copy), lx - cx, ly - cy, image->width(), image->height() });
        return;
    }
    case scene::Node::Type::Surface:
        break;
    }

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

void Snapshot::apply(double cx, double cy, double scaleX, double scaleY, float opacity)
{
    for (const Piece& piece : m_pieces) {
        piece.node->setPosition(cx + piece.x * scaleX, cy + piece.y * scaleY);
        if (piece.node->type() == scene::Node::Type::Rect) {
            static_cast<scene::RectNode*>(piece.node.get())->setSize(piece.width * scaleX, piece.height * scaleY);
        } else {
            static_cast<scene::BufferNode*>(piece.node.get())->setSize(piece.width * scaleX, piece.height * scaleY);
        }
    }
    m_tree->setOpacity(opacity);
    if (m_shape.enabled) {
        scene::Shape shape = m_shape;
        shape.width = m_frame.width * scaleX;
        shape.height = m_frame.height * scaleY;
        shape.x = cx - shape.width / 2.0;
        shape.y = cy - shape.height / 2.0;
        shape.radius = m_shape.radius * std::min(scaleX, scaleY);
        m_tree->setShape(shape);
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
        .snapshot = std::make_unique<Snapshot>(toplevel->tree->parent(), toplevel->tree.get(), frame),
        .tween = {},
        .fromX = frame.x + frame.width / 2.0,
        .fromY = frame.y + frame.height / 2.0,
        .toX = 0,
        .toY = 0,
        .fromScale = 1.0,
        .toScale = 1.0,
        .fromScaleY = 1.0,
        .toScaleY = 1.0,
        .fromOpacity = 1.0f,
        .toOpacity = 0.0f,
    };
    animation.toX = animation.fromX;
    animation.toY = animation.fromY;

    switch (kind) {
    case SnapshotKind::Morph:
        return false; // animateMorph
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

    animation.fromScaleY = animation.fromScale;
    animation.toScaleY = animation.toScale;
    animation.snapshot->apply(animation.fromX, animation.fromY, animation.fromScale, animation.fromOpacity);
    m_snapshotAnimations.push_back(std::move(animation));
    scheduleFrames();
    return true;
}

bool Server::animateMorph(Toplevel* toplevel, wlr_box to)
{
    const wlr_box from = toplevel->frameBox();
    if (from.width <= 0 || from.height <= 0 || to.width <= 0 || to.height <= 0) {
        return false;
    }
    cancelSnapshotAnimations(toplevel);
    SnapshotAnimation animation {
        .kind = SnapshotKind::Morph,
        .owner = toplevel,
        .snapshot = std::make_unique<Snapshot>(toplevel->tree->parent(), toplevel->tree.get(), from),
        .tween = Tween(motion::windowMaximizeMs, &motion::decelerate),
        .fromX = from.x + from.width / 2.0,
        .fromY = from.y + from.height / 2.0,
        .toX = to.x + to.width / 2.0,
        .toY = to.y + to.height / 2.0,
        .fromScale = 1.0,
        .toScale = double(to.width) / from.width,
        .fromScaleY = 1.0,
        .toScaleY = double(to.height) / from.height,
        .fromOpacity = 1.0f,
        .toOpacity = 0.0f,
    };
    animation.snapshot->apply(animation.fromX, animation.fromY, 1.0, 1.0, 1.0f);
    // La finestra vera, già al suo posto nuovo, resta invisibile sotto
    // l'istantanea finché l'app non ha ridisegnato.
    toplevel->setOpacity(0.0f);
    m_snapshotAnimations.push_back(std::move(animation));
    scheduleFrames();
    return true;
}

void Server::cancelSnapshotAnimations(Toplevel* toplevel)
{
    m_snapshotAnimations.remove_if([toplevel](const SnapshotAnimation& animation) {
        if (animation.owner != toplevel) {
            return false;
        }
        if (animation.kind == SnapshotKind::Morph) {
            toplevel->setOpacity(1.0f);
        }
        return true;
    });
}

void Server::tickSnapshotAnimations(double nowMs)
{
    for (auto it = m_snapshotAnimations.begin(); it != m_snapshotAnimations.end();) {
        SnapshotAnimation& a = *it;
        const double p = a.tween.progress(nowMs);
        // L'opacità corre più del movimento: chi entra è leggibile subito,
        // chi esce è già sparito prima di arrivare.
        double fade = std::min(1.0, p * 1.4);
        if (a.kind == SnapshotKind::Morph) {
            // Il contenuto di prima resta pieno per metà del viaggio, poi
            // sfuma; la finestra vera compare intanto sotto di lui, già col
            // contenuto ridisegnato alla misura nuova.
            fade = std::clamp((p - 0.5) / 0.5, 0.0, 1.0);
            a.owner->setOpacity(static_cast<float>(std::clamp((p - 0.25) / 0.6, 0.0, 1.0)));
        }
        a.snapshot->apply(lerp(a.fromX, a.toX, p), lerp(a.fromY, a.toY, p), lerp(a.fromScale, a.toScale, p),
            lerp(a.fromScaleY, a.toScaleY, p), static_cast<float>(lerp(a.fromOpacity, a.toOpacity, fade)));

        if (!a.tween.finished(nowMs)) {
            ++it;
            continue;
        }
        Toplevel* owner = a.owner;
        const bool restored = a.kind == SnapshotKind::Restore;
        if (a.kind == SnapshotKind::Morph) {
            owner->setOpacity(1.0f);
        }
        it = m_snapshotAnimations.erase(it);
        if (restored && owner) {
            owner->finishRestore();
        }
    }
}

} // namespace vela
