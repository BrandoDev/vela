// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_PROCESS_H
#define VELA_PROCESS_H

// Programs launched by Vela: a shell command (/bin/sh -c), in a session of its
// own and with signals unblocked (wlroots blocks some to handle them in its
// loop).

// In the child process: runs `command` and never returns.
void vela_exec_shell(const char *command) __attribute__((noreturn));

// Launches `command` detached from Vela (double fork: init adopts it and no
// zombie is left).
void vela_spawn(const char *command);

#endif
