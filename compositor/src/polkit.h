// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_POLKIT_H
#define VELA_POLKIT_H

// Vela's polkit agent (docs/polkit-agent.md): the compositor starts it as a
// supervised child, so it is in the compositor's logind session and registers
// for it. Lives in vela_server.polkit_agent.
//
// VELA_POLKIT_AGENT names another command, or "0" for none. Without it, the
// agent starts only in the real (DRM) session: nested or headless, the logind
// session belongs to someone else (Plasma already has its agent, and a test
// must never answer polkit for the user). polkit-agent=no in vela.conf leaves
// the job to another agent.

struct vela_server;

void vela_polkit_agent_start(struct vela_server *server);

#endif
