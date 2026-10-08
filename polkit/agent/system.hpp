// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// What the agent reads from the system: users (passwd, AccountsService) and
// apps (/proc, .desktop files).

#include "agent.hpp"

#include <optional>
#include <string>

namespace vela::polkit {

std::optional<Identity> identityForUid(uint32_t uid);

// The process's executable (/proc/<pid>/exe), empty if it can't be read.
std::string executableOf(int pid);

struct AppInfo {
    std::string name;
    std::string icon; // theme name or path
};

// Name and icon of who is asking (docs/polkit-agent.md §4.2): the installed
// app using that executable, otherwise the program's name.
AppInfo describeCaller(const Details& details);

// Vela's language (vela.conf, language=) for polkitd's messages: "it", "en"
// or empty (like the system).
std::string configuredLanguage();

} // namespace vela::polkit
