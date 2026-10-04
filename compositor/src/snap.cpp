#include "decoration.hpp"
#include "server.hpp"

#include <cmath>
#include <utility>

// Snap delle finestre, come Windows 11. Una finestra agganciata occupa un
// rettangolo dell'area utile in dodicesimi (Snap): metà e quarti col
// trascinamento contro i bordi e gli angoli (in alto la massimizza) e con
// Win+frecce; metà, terzi e quarti dai layout di snap che la shell mostra
// sotto il pulsante Ingrandisci (Win+Z). Mentre trascini, un'anteprima
// mostra dove finirà; dopo, Snap Assist della shell propone le altre
// finestre per gli spazi rimasti liberi.

namespace vela {

namespace {

// Quanto vicino al bordo deve arrivare il cursore. Nella sessione vera il
// cursore si ferma sul bordo; dentro KDE può uscire dalla finestra di Vela
// prima di toccarlo, per questo non è zero.
constexpr int edgeThreshold = 6;
// Lungo un bordo, così vicino a un angolo si aggancia al quarto.
constexpr int cornerSize = 96;
// Il mouse fermo sul pulsante Ingrandisci: dopo quanto si aprono i layout.
constexpr int snapLayoutsDelayMs = 450;

// L'accento della shell (Theme.accent, #5b8cff), traslucido.
constexpr float previewColor[3] = { 0.357f, 0.549f, 1.0f };
constexpr float previewAlpha = 0.28f;

// "Tiled" dice all'app di togliere ombre e angoli arrotondati sui lati che
// toccano i bordi dello schermo.
uint32_t tiledEdges(const Snap& snap)
{
    uint32_t edges = WLR_EDGE_NONE;
    edges |= snap.x0 == 0 ? WLR_EDGE_LEFT : 0;
    edges |= snap.x1 == 12 ? WLR_EDGE_RIGHT : 0;
    edges |= snap.y0 == 0 ? WLR_EDGE_TOP : 0;
    edges |= snap.y1 == 12 ? WLR_EDGE_BOTTOM : 0;
    return edges;
}

bool isHalfColumn(const Snap& s)
{
    return s == Snap::Left || s == Snap::Right;
}

bool isQuarter(const Snap& s)
{
    return s == Snap::TopLeft || s == Snap::TopRight || s == Snap::BottomLeft || s == Snap::BottomRight;
}

} // namespace

// I bordi si prendono in pixel fisici: le zone vicine si toccano senza
// fessure né sovrapposizioni a qualunque scala.
Area snapArea(const Output* out, Snap snap)
{
    const wlr_box area = out->physicalUsable();
    auto edgeX = [&](int twelfths) { return area.x + int(std::lround(area.width * twelfths / 12.0)); };
    auto edgeY = [&](int twelfths) { return area.y + int(std::lround(area.height * twelfths / 12.0)); };
    const int x0 = edgeX(snap.x0);
    const int y0 = edgeY(snap.y0);
    return out->fromPhysical({ x0, y0, edgeX(snap.x1) - x0, edgeY(snap.y1) - y0 });
}

// -------------------------------------------------------------- Toplevel --

void Toplevel::setSnap(Snap side, Output* out)
{
    if (!mapped || fullscreen || !configurable()) {
        return;
    }
    finishOpenAnimation();
    if (side != snap) {
        leaveSnapGroup(); // spostata altrove: non sta più col suo gruppo
    }

    if (side == Snap::None) {
        if (snap == Snap::None) {
            return;
        }
        snap = Snap::None;
        sendTiled(WLR_EDGE_NONE);
        const wlr_box back = restoreBox();
        configureSize(back.width, back.height);
        if (back.width > 0) {
            const wlr_box geometry = this->geometry();
            tree->setPosition(back.x - geometry.x, back.y - geometry.y);
        }
        return;
    }
    if (!side.valid()) {
        return;
    }

    // La dimensione da ripristinare è quella "libera", non quella di un
    // altro snap o della finestra massimizzata.
    if (snap == Snap::None && !maximized) {
        restore = frameBox();
    }
    if (maximized) {
        maximized = false;
        sendMaximized(false);
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
    sendTiled(tiledEdges(snap));
    const Placement place = out->place(area);
    configureSize(place.width, place.height);
    const wlr_box geometry = this->geometry();
    tree->setPosition(place.x - geometry.x, place.y - geometry.y);
}

void Toplevel::leaveSnapGroup()
{
    const uint32_t group = std::exchange(snapGroup, 0u);
    if (group == 0) {
        return;
    }
    // Un gruppo di una finestra sola non è più un gruppo.
    std::vector<Toplevel*> rest;
    for (Toplevel* other : server.toplevels) {
        if (other->snapGroup == group) {
            rest.push_back(other);
        }
    }
    if (rest.size() < 2) {
        for (Toplevel* other : rest) {
            other->snapGroup = 0;
        }
    }
    server.announceWorkspaces();
}

// ------------------------------------------------- Win+frecce (Server) --

// Come Windows 11: Win+←/→ alla metà (verso il lato opposto si sgancia);
// da una metà, Win+↑/↓ al quarto in alto o in basso; dal quarto in alto
// Win+↑ massimizza, da quello in basso Win+↓ riduce a icona.
void Server::snapWithKeyboard(Toplevel* active, xkb_keysym_t sym)
{
    const Snap s = active->snap;
    const bool leftColumn = s.x0 == 0 && s.x1 == 6;
    const bool rightColumn = s.x0 == 6 && s.x1 == 12;
    if (sym == XKB_KEY_Left || sym == XKB_KEY_Right) {
        const bool toLeft = sym == XKB_KEY_Left;
        if ((toLeft && rightColumn) || (!toLeft && leftColumn)) {
            active->setSnap(Snap::None);
        } else if (leftColumn || rightColumn) {
            return; // già da quella parte
        } else {
            active->setSnap(toLeft ? Snap::Left : Snap::Right);
            offerSnapAssist(active);
        }
        return;
    }
    if (sym == XKB_KEY_Up) {
        if (isHalfColumn(s)) {
            active->setSnap({ s.x0, 0, s.x1, 6 });
            offerSnapAssist(active);
        } else if (isQuarter(s) && s.y0 == 6) {
            active->setSnap({ s.x0, 0, s.x1, 12 });
        } else {
            active->setMaximized(true);
        }
        return;
    }
    // Giù: prima si ripristina, poi si riduce a icona.
    if (active->maximized) {
        active->setMaximized(false);
    } else if (isHalfColumn(s)) {
        active->setSnap({ s.x0, 6, s.x1, 12 });
        offerSnapAssist(active);
    } else if (isQuarter(s) && s.y0 == 0) {
        active->setSnap({ s.x0, 0, s.x1, 12 });
    } else if (s != Snap::None && !(isQuarter(s) && s.y0 == 6)) {
        active->setSnap(Snap::None);
    } else {
        active->setMinimized(true);
    }
}

// ---------------------------------------------------- anteprima (Server) --

void Server::updateSnapZone()
{
    Output* out = outputAt(cursor->x, cursor->y);
    SnapZone zone = SnapZone::None;
    Snap tile;
    if (out && grabbed && !grabbed->fullscreen) {
        const wlr_box box = out->box();
        const bool left = cursor->x <= box.x + edgeThreshold;
        const bool right = cursor->x >= box.x + box.width - 1 - edgeThreshold;
        const bool top = cursor->y <= box.y + edgeThreshold;
        const bool bottom = cursor->y >= box.y + box.height - 1 - edgeThreshold;
        const bool nearTop = cursor->y <= box.y + cornerSize;
        const bool nearBottom = cursor->y >= box.y + box.height - cornerSize;
        const bool nearLeft = cursor->x <= box.x + cornerSize;
        const bool nearRight = cursor->x >= box.x + box.width - cornerSize;
        // Negli angoli il quarto, sui lati la metà, in alto massimizza.
        if ((left && nearTop) || (top && nearLeft)) {
            zone = SnapZone::Tile;
            tile = Snap::TopLeft;
        } else if ((right && nearTop) || (top && nearRight)) {
            zone = SnapZone::Tile;
            tile = Snap::TopRight;
        } else if ((left && nearBottom) || (bottom && nearLeft)) {
            zone = SnapZone::Tile;
            tile = Snap::BottomLeft;
        } else if ((right && nearBottom) || (bottom && nearRight)) {
            zone = SnapZone::Tile;
            tile = Snap::BottomRight;
        } else if (left || right) {
            zone = SnapZone::Tile;
            tile = left ? Snap::Left : Snap::Right;
        } else if (top) {
            zone = SnapZone::Maximize;
        }
    }
    if (zone == m_snapPreview.zone && tile == m_snapPreview.tile && out == m_snapPreview.output) {
        return;
    }
    m_snapPreview.zone = zone;
    m_snapPreview.tile = tile;
    m_snapPreview.output = out;

    if (zone == SnapZone::None) {
        m_snapPreview.rect.reset();
        return;
    }

    m_snapPreview.target = zone == SnapZone::Maximize ? out->usableArea() : snapArea(out, tile);
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
    const Snap tile = m_snapPreview.tile;
    Output* out = m_snapPreview.output;
    m_snapPreview.rect.reset();
    m_snapPreview.zone = SnapZone::None;
    m_snapPreview.tile = Snap::None;
    m_snapPreview.output = nullptr;

    if (!apply || !grabbed || zone == SnapZone::None) {
        return;
    }
    if (zone == SnapZone::Maximize) {
        grabbed->setMaximized(true);
    } else {
        grabbed->setSnap(tile, out);
        offerSnapAssist(grabbed);
    }
}

// ---------------------------------------------------- shell: layout e assist --

void Server::offerSnapAssist(Toplevel* toplevel)
{
    Output* out = toplevel ? toplevel->output() : nullptr;
    if (!out || toplevel->snap == Snap::None || !toplevel->extHandle || !toplevel->extHandle->identifier) {
        return;
    }
    // {"window":id,"output":nome,"tile":[x0,y0,x1,y1],"occupied":[[...]],"candidates":[id...]}
    auto tileJson = [](const Snap& s) {
        return "[" + std::to_string(s.x0) + "," + std::to_string(s.y0) + "," + std::to_string(s.x1) + ","
            + std::to_string(s.y1) + "]";
    };
    std::string occupied;
    std::string candidates;
    for (Toplevel* other : toplevels) {
        if (other == toplevel || !other->mapped || other->minimized || !other->onCurrentWorkspace()
            || !other->extHandle || !other->extHandle->identifier) {
            continue;
        }
        if (other->snap != Snap::None && other->output() == out) {
            occupied += (occupied.empty() ? "" : ",") + tileJson(other->snap);
        } else if (other->snap == Snap::None && !other->fullscreen && other->configurable()) {
            candidates += std::string(candidates.empty() ? "" : ",") + "\"" + other->extHandle->identifier + "\"";
        }
    }
    if (candidates.empty()) {
        return; // nessuna finestra da proporre
    }
    sendShellCommand(std::string("snap-assist {\"window\":\"") + toplevel->extHandle->identifier + "\",\"output\":\""
        + out->wlr->name + "\",\"tile\":" + tileJson(toplevel->snap) + ",\"occupied\":[" + occupied
        + "],\"candidates\":[" + candidates + "]}");
}

void Server::joinSnapGroup(Toplevel* toplevel, Toplevel* origin)
{
    if (!toplevel || !origin || origin == toplevel || !origin->mapped || origin->snap == Snap::None
        || toplevel->snap == Snap::None || origin->output() != toplevel->output()) {
        return;
    }
    if (origin->snapGroup == 0) {
        origin->snapGroup = nextSnapGroup++;
    }
    toplevel->snapGroup = origin->snapGroup;
    announceWorkspaces();
}

void Server::activateSnapGroup(Toplevel* toplevel)
{
    const uint32_t group = toplevel->snapGroup;
    std::vector<Toplevel*> members;
    for (Toplevel* other : toplevels) {
        if (group != 0 && other->snapGroup == group && other != toplevel) {
            members.push_back(other);
        }
    }
    // Prima le altre, poi quella scelta: resta sopra e a fuoco.
    members.push_back(toplevel);
    for (Toplevel* member : members) {
        if (member->minimized) {
            member->setMinimized(false);
        }
        focusToplevel(member);
    }
}

void Server::showSnapLayouts(Toplevel* toplevel, bool keyboard)
{
    Output* out = toplevel ? toplevel->output() : nullptr;
    if (!out || !toplevel->extHandle || !toplevel->extHandle->identifier || toplevel->fullscreen || locked) {
        return;
    }
    // Sotto il pulsante Ingrandisci; senza la barra di Vela, in alto al centro della finestra.
    const wlr_box frame = toplevel->frameBox();
    wlr_box anchor { frame.x + frame.width / 2, frame.y, 0, 0 };
    if (toplevel->decoration) {
        anchor = toplevel->decoration->maximizeBox();
    }
    const wlr_box screen = out->box();
    const int x = anchor.x + anchor.width / 2 - screen.x;
    const int y = anchor.y + anchor.height - screen.y;
    sendShellCommand(std::string("snap-layouts ") + toplevel->extHandle->identifier + " " + out->wlr->name + " "
        + std::to_string(x) + " " + std::to_string(y) + " " + (keyboard ? "1" : "0"));
}

void Server::hoverMaximize(Toplevel* toplevel)
{
    if (toplevel == m_snapLayoutsHover.toplevel) {
        return;
    }
    m_snapLayoutsHover.toplevel = toplevel;
    if (!m_snapLayoutsHover.timer) {
        m_snapLayoutsHover.timer = wl_event_loop_add_timer(
            loop,
            [](void* data) {
                auto* self = static_cast<Server*>(data);
                // Una volta sola: il pannello copre il pulsante, e tornandoci
                // sopra si ricomincia.
                Toplevel* hovered = std::exchange(self->m_snapLayoutsHover.toplevel, nullptr);
                if (hovered && hovered->decoration && self->cursorMode == CursorMode::Passthrough
                    && self->seat->pointer_state.button_count == 0
                    && hovered->decoration->partAt(self->cursor->x, self->cursor->y) == Decoration::Part::Maximize) {
                    self->showSnapLayouts(hovered, false);
                }
                return 0;
            },
            this);
    }
    wl_event_source_timer_update(m_snapLayoutsHover.timer, toplevel ? snapLayoutsDelayMs : 0);
}

} // namespace vela
