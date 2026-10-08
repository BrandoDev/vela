// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_COMMAND_H
#define VELA_COMMAND_H

// Commands from the shell (and anyone in the session) to the compositor, one
// line each on $XDG_RUNTIME_DIR/vela-<WAYLAND_DISPLAY>.sock: "logout", "lock",
// "night-light toggle", "window <id> maximize"... Some lines are questions
// answered on the same connection: "modifiers", "workspaces", "accessibility",
// "window-rects" and "state" (all the visible state, for the functional
// tests).

#include <stdbool.h>

struct vela_buffer;
struct vela_server;

// Opens the socket (once the display has its name).
void vela_commands_listen(struct vela_server *server);
void vela_commands_stop(struct vela_server *server);



#endif
