// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// vela-polkit-agent's real Backend: PAM sessions with PolkitAgentSession
// (polkit-agent-helper-1) and the prompt as a child process, all in GLib's
// main loop.

#include "agent.hpp"

#include <string>

namespace vela::polkit {

class GLibBackend : public Backend {
public:
    // promptPath: the vela-polkit-prompt executable.
    // testPassword: if not empty, no real session; the "session" accepts only
    // this password. For tests (VELA_POLKIT_TEST_PASSWORD,
    // docs/polkit-agent.md §10): it authorizes nothing, because only the
    // helper tells polkitd the outcome, and here it doesn't start.
    GLibBackend(std::string promptPath, std::string testPassword);

    std::unique_ptr<AuthSession> startSession(
        const Identity& identity, const std::string& cookie, AuthSession::Events events) override;
    std::unique_ptr<Prompt> startPrompt(Prompt::Events events) override;
    void defer(std::function<void()> task) override;

private:
    std::string m_promptPath;
    std::string m_testPassword;
};

// The prompt to use: VELA_POLKIT_PROMPT, then next to this executable, then
// in libexecdir (§4.4). Empty if there is none.
std::string findPrompt();

} // namespace vela::polkit
