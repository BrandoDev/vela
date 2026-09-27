// Il mouse per i giochi e per le app che lo vogliono tutto per sé.
//
// - relative-pointer: i movimenti "grezzi" del mouse, anche quando il
//   cursore è fermo al bordo dello schermo o bloccato (girare la visuale).
// - pointer-constraints: l'app blocca il puntatore dove si trova (visuale in
//   prima persona) o lo confina in una regione (strategici, disegno).
//   Il vincolo vale finché il puntatore è sulla sua superficie.

#include "server.hpp"

namespace vela {

namespace {

// Un vincolo vivo: quando sparisce non deve restare quello attivo.
struct PointerConstraint {
    PointerConstraint(Server& server, wlr_pointer_constraint_v1* constraint)
        : server(server)
        , wlr(constraint)
    {
        destroy.connect(&wlr->events.destroy, [this](void*) {
            if (this->server.activeConstraint == wlr) {
                this->server.activeConstraint = nullptr;
            }
            delete this;
        });
    }

    Server& server;
    wlr_pointer_constraint_v1* wlr;
    Listener destroy;
};

} // namespace

void Server::initPointerProtocols()
{
    relativePointers = wlr_relative_pointer_manager_v1_create(display);
    pointerConstraints = wlr_pointer_constraints_v1_create(display);
    on(&pointerConstraints->events.new_constraint, [this](void* data) {
        auto* constraint = static_cast<wlr_pointer_constraint_v1*>(data);
        new PointerConstraint(*this, constraint);
        // Il puntatore è già sulla superficie: vale da subito.
        if (seat->pointer_state.focused_surface == constraint->surface) {
            updatePointerConstraint(constraint->surface);
        }
    });

    shortcutsInhibit = wlr_keyboard_shortcuts_inhibit_v1_create(display);
    on(&shortcutsInhibit->events.new_inhibitor, [this](void* data) {
        // Concesso sempre: lo chiedono app che l'utente usa a tutto schermo
        // (macchine virtuali, desktop remoto). Vale solo mentre sono a fuoco.
        wlr_keyboard_shortcuts_inhibitor_v1_activate(static_cast<wlr_keyboard_shortcuts_inhibitor_v1*>(data));
    });
}

void Server::updatePointerConstraint(wlr_surface* focused)
{
    wlr_pointer_constraint_v1* constraint
        = focused ? wlr_pointer_constraints_v1_constraint_for_surface(pointerConstraints, focused, seat) : nullptr;
    if (constraint == activeConstraint) {
        return;
    }
    if (wlr_pointer_constraint_v1* previous = activeConstraint) {
        // Sbloccato: il cursore ricompare dove l'app dice di averlo lasciato.
        if (previous->type == WLR_POINTER_CONSTRAINT_V1_LOCKED && previous->current.cursor_hint.enabled
            && seat->pointer_state.focused_surface == previous->surface) {
            const double originX = cursor->x - seat->pointer_state.sx;
            const double originY = cursor->y - seat->pointer_state.sy;
            wlr_cursor_warp(cursor, nullptr, originX + previous->current.cursor_hint.x,
                originY + previous->current.cursor_hint.y);
        }
        activeConstraint = nullptr;
        wlr_pointer_constraint_v1_send_deactivated(previous); // può distruggerlo
    }
    if (constraint) {
        activeConstraint = constraint;
        wlr_pointer_constraint_v1_send_activated(constraint);
    }
}

bool Server::constrainMotion(double& dx, double& dy)
{
    wlr_pointer_constraint_v1* constraint = activeConstraint;
    if (!constraint || cursorMode != CursorMode::Passthrough
        || seat->pointer_state.focused_surface != constraint->surface) {
        return true;
    }
    if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        return false;
    }
    // Confinato: il movimento si ferma al bordo della regione (coordinate
    // della superficie).
    const double sx = seat->pointer_state.sx;
    const double sy = seat->pointer_state.sy;
    double x = sx + dx;
    double y = sy + dy;
    if (wlr_region_confine(&constraint->region, sx, sy, sx + dx, sy + dy, &x, &y)) {
        dx = x - sx;
        dy = y - sy;
    }
    return true;
}

bool Server::shortcutsInhibited() const
{
    wlr_surface* focused = seat->keyboard_state.focused_surface;
    if (!focused || !shortcutsInhibit) {
        return false;
    }
    wlr_keyboard_shortcuts_inhibitor_v1* inhibitor;
    wl_list_for_each(inhibitor, &shortcutsInhibit->inhibitors, link)
    {
        if (inhibitor->surface == focused && inhibitor->active) {
            return true;
        }
    }
    return false;
}

} // namespace vela
