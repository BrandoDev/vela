// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SWITCHER_H
#define VELA_SWITCHER_H

// Alt+Tab: the compositor decides the order (most recent first, current
// desktop only, like Windows) and the selection; the shell draws the panel
// with the previews ("switcher-show N id1 id2...", "switcher-select N",
// "switcher-hide"). The choice goes on while Alt is held; releasing it
// switches to the chosen window, Esc cancels.

#include <stdbool.h>

struct vela_server;
struct vela_view;

struct vela_switcher {
    bool active;
    struct vela_view **views; // most recently used first
    int count;
    int capacity;
    int selected;
};

struct vela_switcher *vela_switcher_create(void);
void vela_switcher_destroy(struct vela_switcher *switcher);

// One step forward (+1) or back; the first one opens the panel.
void vela_switcher_step(struct vela_server *server, int direction);
// Closes the panel; activate: switches to the chosen window.
void vela_switcher_finish(struct vela_server *server, bool activate);
bool vela_switcher_active(const struct vela_server *server);
// A click on a preview (from the shell).
void vela_switcher_pick(struct vela_server *server, int index);
// A window that goes away during Alt+Tab closes the switcher.
void vela_switcher_forget(struct vela_server *server, struct vela_view *view);

#endif
