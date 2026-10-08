// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SESSION_H
#define VELA_SESSION_H

// La sessione vera (da SDDM o da una console): Vela è il desktop e lo dice
// alle app, e avvisa systemd e D-Bus quando parte e quando finisce
// (session/vela-session-env). Annidati o headless si resta ospiti
// dell'ambiente che c'è.

// Solo i valori che nessuno (SDDM, l'utente) ha già scelto.
void vela_session_set_environment(void);
// "start" o "stop": aspetta che vela-session-env finisca.
void vela_session_run_hook(const char *action);

#endif
