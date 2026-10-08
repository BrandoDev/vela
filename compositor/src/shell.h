// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SHELL_H
#define VELA_SHELL_H

// The Qt shell: the startup command (the shell) that the compositor launches
// and relaunches when it fails, and the messages it sends it.

#include <stdint.h>
#include <sys/types.h>

struct vela_server;
struct wl_event_source;

// The launched and supervised shell (a pidfd in the event loop tells when it
// ends). Lives in vela_server.shell.
struct vela_shell {
    char *command; // NULL: none
    pid_t pid;
    int pidfd;
    struct wl_event_source *source;
    int64_t started_ns;
    int quick_crashes; // abnormal exits in a row shortly after starting
};

// Launches `command` and relaunches it when it fails (not when it exits
// cleanly, nor when it keeps failing right after starting).
void vela_shell_start(struct vela_server *server, const char *command);
// We are shutting down: the shell going away must not be relaunched.
void vela_shell_stop(struct vela_server *server);

// One line of text on the shell's Unix socket
// ($XDG_RUNTIME_DIR/vela-shell-<WAYLAND_DISPLAY>.sock), such as "toggle-start"
// or "accessibility {...}". Never blocks: without a shell the message is
// simply lost.
void vela_shell_send(struct vela_server *server, const char *line);

#endif
