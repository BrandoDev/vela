// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SHELL_H
#define VELA_SHELL_H

// La shell Qt: il comando di avvio (la shell) che il compositor lancia e
// rilancia se si chiude male, e i messaggi che le manda.

#include <stdint.h>
#include <sys/types.h>

struct vela_server;
struct wl_event_source;

// La shell lanciata e sorvegliata (un pidfd nel ciclo degli eventi ci dice
// quando finisce). Sta in vela_server.shell.
struct vela_shell {
    char *command; // NULL: nessuna
    pid_t pid;
    int pidfd;
    struct wl_event_source *source;
    int64_t started_ns;
    int quick_crashes; // chiusure anomale di fila poco dopo l'avvio
};

// Lancia `command` e lo rilancia se si chiude male (non se si chiude bene,
// né se si chiude di continuo appena partito).
void vela_shell_start(struct vela_server *server, const char *command);
// Stiamo chiudendo noi: la shell che se ne va non va rilanciata.
void vela_shell_stop(struct vela_server *server);

// Una riga di testo sul socket Unix della shell
// ($XDG_RUNTIME_DIR/vela-shell-<WAYLAND_DISPLAY>.sock), per esempio
// "toggle-start" o "accessibility {...}". Non blocca mai: se la shell non
// c'è, il messaggio va perso e basta.
void vela_shell_send(struct vela_server *server, const char *line);

#endif
