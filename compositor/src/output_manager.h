// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_OUTPUT_MANAGER_H
#define VELA_OUTPUT_MANAGER_H

// wlr-output-management: layout, mode, scale and rotation of the outputs from
// outside programs (Settings > Display, wlr-randr, kanshi, wdisplays). Every
// change is reported to them; applied choices are remembered in outputs.conf
// (only in the real session). Plus the "test-output" command, which plugs and
// unplugs headless outputs.

struct vela_server;

void vela_output_manager_init(struct vela_server *server);
void vela_output_manager_finish(struct vela_server *server);

// The current output configuration, to the programs.
void vela_output_manager_update(struct vela_server *server);

// "add 1920x1080" or "remove HEADLESS-2": only with the headless backend.
void vela_test_output_command(struct vela_server *server, const char *arguments);

#endif
