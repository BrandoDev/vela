// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_PROCESS_H
#define VELA_PROCESS_H

// Programmi lanciati da Vela: un comando della shell (/bin/sh -c), in una
// sessione sua e con i segnali sbloccati (wlroots ne blocca alcuni per
// gestirli nel suo ciclo).

// Nel processo figlio: esegue `command` e non ritorna mai.
void vela_exec_shell(const char *command) __attribute__((noreturn));

// Lancia `command` staccato da Vela (doppio fork: lo adotta init e non
// resta zombie).
void vela_spawn(const char *command);

#endif
