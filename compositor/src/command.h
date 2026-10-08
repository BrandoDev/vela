// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_COMMAND_H
#define VELA_COMMAND_H

// Comandi dalla shell (e da chi sta nella sessione) al compositor, una riga
// per comando su $XDG_RUNTIME_DIR/vela-<WAYLAND_DISPLAY>.sock: "logout",
// "lock", "night-light toggle", "window <id> maximize"... Alcune righe sono
// domande con risposta sulla stessa connessione: "modifiers", "workspaces",
// "accessibility", "window-rects" e "state" (tutto lo stato visibile, per
// le prove funzionali).

#include <stdbool.h>

struct vela_buffer;
struct vela_server;

// Apre il socket (dopo che il display ha il suo nome).
void vela_commands_listen(struct vela_server *server);
void vela_commands_stop(struct vela_server *server);

void vela_command_run(struct vela_server *server, const char *line);

// Le risposte, in JSON.
void vela_state_json(struct vela_server *server, struct vela_buffer *out);
// Le finestre visibili del desktop in uso, dalla più in alto, col loro
// riquadro (barra compresa): per lo Strumento di cattura.
void vela_window_rects_json(struct vela_server *server, struct vela_buffer *out);

#endif
