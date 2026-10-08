// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SUPERVISOR_H
#define VELA_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>

// vela-compositor --supervise ...: tiene il socket Wayland e riavvia il
// compositor se va in crash. Restituisce il codice di uscita del processo.
int vela_supervise(int argc, char **argv);

// Il file che dice "lo schermo è bloccato" per il display dato: lo scrive il
// compositor quando blocca, lo legge il supervisore prima di riavviarlo.
// false se manca XDG_RUNTIME_DIR.
bool vela_lock_flag_path(const char *wayland_display, char *out, size_t size);

// vela-session-env (collegamento della sessione a systemd): accanto
// all'eseguibile (cartella di build) o installato. false se non c'è.
bool vela_session_hook_path(char *out, size_t size);

#endif
