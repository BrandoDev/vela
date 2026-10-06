// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace vela {

// vela-compositor --supervise ...: tiene il socket Wayland e riavvia il
// compositor se va in crash (supervisor.cpp).
int runSupervisor(int argc, char* argv[]);

// Il file che dice "lo schermo è bloccato" per il display dato: lo scrive il
// compositor quando blocca, lo legge il supervisore prima di riavviarlo.
std::string lockFlagPath(const char* waylandDisplay);

// vela-session-env (collegamento della sessione a systemd), o vuoto.
std::string sessionHookPath();

} // namespace vela
