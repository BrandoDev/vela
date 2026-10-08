// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SUPERVISOR_H
#define VELA_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>

// vela-compositor --supervise ...: holds the Wayland socket and restarts the
// compositor if it crashes. Returns the process exit code.
int vela_supervise(int argc, char **argv);

// The file that says "the screen is locked" for the given display: the
// compositor writes it when it locks, the supervisor reads it before
// restarting. false without XDG_RUNTIME_DIR.
bool vela_lock_flag_path(const char *wayland_display, char *out, size_t size);

// vela-session-env (ties the session to systemd): next to the executable
// (build directory) or installed. false if there is none.
bool vela_session_hook_path(char *out, size_t size);

#endif
