// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_OUTPUT_MANAGER_H
#define VELA_OUTPUT_MANAGER_H

// wlr-output-management: disposizione, modalità, scala e rotazione degli
// schermi da programmi esterni (Impostazioni > Schermo, wlr-randr, kanshi,
// wdisplays). Ogni cambiamento si racconta a loro; le scelte applicate si
// ricordano in outputs.conf (solo nella sessione vera). Più il comando di
// prova "test-output", che collega e scollega schermi headless.

struct vela_server;

void vela_output_manager_init(struct vela_server *server);
void vela_output_manager_finish(struct vela_server *server);

// La configurazione degli schermi di adesso, ai programmi.
void vela_output_manager_update(struct vela_server *server);

// "add 1920x1080" o "remove HEADLESS-2": solo col backend headless.
void vela_test_output_command(struct vela_server *server, const char *arguments);

#endif
