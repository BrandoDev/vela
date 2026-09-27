#include "server.hpp"

// Snap delle finestre, come su Windows: metà sinistra o destra dello schermo
// con Super+frecce, oppure trascinando la finestra contro un bordo (in alto
// la massimizza). Mentre trascini, un'anteprima mostra dove finirà.

namespace vela {

namespace {

// Quanto vicino al bordo deve arrivare il cursore. Nella sessione vera il
// cursore si ferma sul bordo; dentro KDE può uscire dalla finestra di Vela
// prima di toccarlo, per questo non è zero.
constexpr int edgeThreshold = 6;

// L'accento della shell (Theme.accent, #5b8cff), traslucido.
constexpr float previewColor[3] = { 0.357f, 0.549f, 1.0f };
constexpr float previewAlpha = 0.28f;

uint32_t tiledEdges(Snap side)
{
    const uint32_t vertical = WLR_EDGE_TOP | WLR_EDGE_BOTTOM;
    switch (side) {
    case Snap::Left: return vertical | WLR_EDGE_LEFT;
    case Snap::Right: return vertical | WLR_EDGE_RIGHT;
    case Snap::None: break;
    }
    return WLR_EDGE_NONE;
}

} // namespace

// La metà si prende in pixel fisici: le due metà si toccano senza fessure
// né sovrapposizioni a qualunque scala.
Area snapArea(const Output* out, Snap side)
{
    const wlr_box area = out->physicalUsable();
    const int half = area.width / 2;
    if (side == Snap::Right) {
        return out->fromPhysical({ area.x + half, area.y, area.width - half, area.height });
    }
    return out->fromPhysical({ area.x, area.y, half, area.height });
}

// -------------------------------------------------------------- Toplevel --

void Toplevel::setSnap(Snap side, Output* out)
{
    if (!mapped || fullscreen || !xdg->base->initialized) {
        return;
    }
    finishOpenAnimation();

    if (side == Snap::None) {
        if (snap == Snap::None) {
            return;
        }
        snap = Snap::None;
        wlr_xdg_toplevel_set_tiled(xdg, WLR_EDGE_NONE);
        wlr_xdg_toplevel_set_size(xdg, restore.width, restore.height);
        if (restore.width > 0) {
            const wlr_box& geometry = xdg->base->geometry;
            tree->setPosition(restore.x - geometry.x, restore.y - geometry.y);
        }
        return;
    }

    // La dimensione da ripristinare è quella "libera", non quella di un
    // altro snap o della finestra massimizzata.
    if (snap == Snap::None && !maximized) {
        restore = frameBox();
    }
    if (maximized) {
        maximized = false;
        wlr_xdg_toplevel_set_maximized(xdg, false);
        if (handle) {
            wlr_foreign_toplevel_handle_v1_set_maximized(handle, false);
        }
    }
    snap = side;
    applySnap(out ? out : output());
}

void Toplevel::applySnap(Output* out)
{
    if (!out || snap == Snap::None) {
        return;
    }
    const Area area = snapArea(out, snap);
    // "Tiled" dice all'app di togliere ombre e angoli arrotondati sui lati
    // che toccano i bordi.
    wlr_xdg_toplevel_set_tiled(xdg, tiledEdges(snap));
    const Placement place = out->place(area);
    wlr_xdg_toplevel_set_size(xdg, place.width, place.height);
    const wlr_box& geometry = xdg->base->geometry;
    tree->setPosition(place.x - geometry.x, place.y - geometry.y);
}

// ---------------------------------------------------- anteprima (Server) --

void Server::updateSnapZone()
{
    Output* out = outputAt(cursor->x, cursor->y);
    SnapZone zone = SnapZone::None;
    if (out && grabbed && !grabbed->fullscreen) {
        const wlr_box box = out->box();
        if (cursor->x <= box.x + edgeThreshold) {
            zone = SnapZone::Left;
        } else if (cursor->x >= box.x + box.width - 1 - edgeThreshold) {
            zone = SnapZone::Right;
        } else if (cursor->y <= box.y + edgeThreshold) {
            zone = SnapZone::Maximize;
        }
    }
    if (zone == m_snapPreview.zone && out == m_snapPreview.output) {
        return;
    }
    m_snapPreview.zone = zone;
    m_snapPreview.output = out;

    if (zone == SnapZone::None) {
        m_snapPreview.rect.reset();
        return;
    }

    m_snapPreview.target = zone == SnapZone::Maximize
        ? out->usableArea()
        : snapArea(out, zone == SnapZone::Left ? Snap::Left : Snap::Right);
    if (!m_snapPreview.rect) {
        m_snapPreview.rect = std::make_unique<scene::RectNode>(grabbed->tree->parent(), 0, 0,
            wlr_render_color { 0, 0, 0, 0 });
    }
    // Sotto la finestra trascinata, come su Windows.
    m_snapPreview.rect->placeBelow(grabbed->tree.get());
    m_snapPreview.tween = Tween(motion::snapPreviewMs, &motion::decelerate);
    tickSnapPreview(0.0);
    scheduleFrames();
}

void Server::tickSnapPreview(double nowMs)
{
    if (!m_snapPreview.rect) {
        return;
    }
    // Cresce dal centro dell'area e si accende.
    const double p = nowMs > 0.0 ? m_snapPreview.tween.progress(nowMs) : 0.0;
    const double scale = 0.9 + 0.1 * p;
    const Area& t = m_snapPreview.target;
    const double width = t.width * scale;
    const double height = t.height * scale;
    m_snapPreview.rect->setSize(width, height);
    m_snapPreview.rect->setPosition(t.x + (t.width - width) / 2, t.y + (t.height - height) / 2);

    const float alpha = previewAlpha * static_cast<float>(p);
    m_snapPreview.rect->setColor({ previewColor[0] * alpha, previewColor[1] * alpha, previewColor[2] * alpha, alpha });
}

void Server::endSnapZone(bool apply)
{
    const SnapZone zone = m_snapPreview.zone;
    Output* out = m_snapPreview.output;
    m_snapPreview.rect.reset();
    m_snapPreview.zone = SnapZone::None;
    m_snapPreview.output = nullptr;

    if (!apply || !grabbed || zone == SnapZone::None) {
        return;
    }
    if (zone == SnapZone::Maximize) {
        grabbed->setMaximized(true);
    } else {
        grabbed->setSnap(zone == SnapZone::Left ? Snap::Left : Snap::Right, out);
    }
}

} // namespace vela
