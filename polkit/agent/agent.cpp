// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "agent.hpp"

#include "../protocol.hpp"

#include <algorithm>
#include <cstdlib>

namespace vela::polkit {

std::vector<Identity> orderIdentities(std::vector<Identity> identities, uint32_t currentUid)
{
    std::stable_partition(identities.begin(), identities.end(),
        [currentUid](const Identity& identity) { return identity.uid == currentUid; });
    std::stable_partition(identities.begin(), identities.end(),
        [](const Identity& identity) { return identity.uid != 0; });
    // If the current user is root, it stays first anyway.
    if (currentUid == 0) {
        std::stable_partition(identities.begin(), identities.end(),
            [](const Identity& identity) { return identity.uid == 0; });
    }
    return identities;
}

std::string callerExecutable(const Details& details, const std::function<std::string(int pid)>& exeOf)
{
    auto detail = [&](const char* key) -> std::string {
        for (const auto& [name, value] : details) {
            if (name == key) {
                return value;
            }
        }
        return {};
    };
    if (std::string program = detail("program"); !program.empty()) {
        return program;
    }
    for (const char* key : { "polkit.subject-pid", "polkit.caller-pid" }) {
        const std::string value = detail(key);
        char* end = nullptr;
        const long pid = std::strtol(value.c_str(), &end, 10);
        if (value.empty() || *end || pid <= 0) {
            continue;
        }
        if (std::string exe = exeOf(int(pid)); !exe.empty()) {
            return exe;
        }
    }
    return {};
}

Agent::Agent(Backend& backend)
    : m_backend(backend)
{
}

Agent::~Agent()
{
    *m_alive = false;
}

void Agent::begin(Request request, Done done)
{
    m_queue.push_back({ std::move(request), std::move(done) });
    if (!m_current) {
        startNext();
    }
}

void Agent::cancel(const std::string& cookie)
{
    if (m_current && m_current->entry.request.cookie == cookie) {
        finish(Outcome::Cancelled, "cancel");
        return;
    }
    const auto it = std::find_if(
        m_queue.begin(), m_queue.end(), [&](const Entry& entry) { return entry.request.cookie == cookie; });
    if (it != m_queue.end()) {
        Done done = std::move(it->done);
        m_queue.erase(it);
        done(Outcome::Cancelled);
    }
}

void Agent::startNext()
{
    while (!m_current && !m_queue.empty()) {
        Entry entry = std::move(m_queue.front());
        m_queue.pop_front();
        if (entry.request.identities.empty()) {
            entry.done(Outcome::Failed); // polkit gives nobody who could answer
            continue;
        }

        auto current = std::make_unique<Current>();
        current->entry = std::move(entry);
        const uint64_t serial = ++m_requestSerial;
        std::weak_ptr<bool> alive = m_alive;
        current->prompt = m_backend.startPrompt({
            [this, alive, serial](const std::string& line) {
                if (!alive.expired() && m_current && m_requestSerial == serial) {
                    onPromptLine(line);
                }
            },
            [this, alive, serial] {
                // The prompt crashed or closed: it counts as "No".
                if (!alive.expired() && m_current && m_requestSerial == serial) {
                    finish(Outcome::Cancelled, nullptr);
                }
            },
        });
        if (!current->prompt) {
            current->entry.done(Outcome::Failed);
            continue;
        }
        m_current = std::move(current);

        const Request& request = m_current->entry.request;
        send(encode("action", request.actionId));
        send(encode("message", request.message));
        send(encode("icon", request.iconName));
        send(encode("app", request.appName));
        send(encode("app-icon", request.appIcon));
        for (const auto& [key, value] : request.details) {
            send(encode("detail", key + "=" + value));
        }
        for (const Identity& identity : request.identities) {
            send(encode("identity", std::to_string(identity.uid) + " " + identity.login + " " + identity.name));
            if (!identity.avatar.empty()) {
                send(encode("avatar", std::to_string(identity.uid) + " " + identity.avatar));
            }
        }
        send(encode("show"));
        startSession();
    }
}

void Agent::startSession()
{
    Current& current = *m_current;
    const uint64_t generation = ++m_generation;
    current.generation = generation;
    current.answered = false;
    auto valid = [this, generation, alive = std::weak_ptr<bool>(m_alive)] {
        return !alive.expired() && m_current && m_current->generation == generation;
    };
    // The old session (if any) goes before another one starts.
    if (current.session) {
        current.session->cancel();
        current.session.reset();
    }
    current.session = m_backend.startSession(current.entry.request.identities[current.identity],
        current.entry.request.cookie,
        {
            [this, valid](const std::string& text, bool echo) {
                if (valid()) {
                    send(encode("request", std::string(echo ? "1 " : "0 ") + text));
                }
            },
            [this, valid](const std::string& text) {
                if (valid()) {
                    send(encode("error", text));
                }
            },
            [this, valid](const std::string& text) {
                if (valid()) {
                    send(encode("info", text));
                }
            },
            [this, valid](bool authorized) {
                if (!valid()) {
                    return;
                }
                if (authorized) {
                    finish(Outcome::Authorized, "done");
                    return;
                }
                // A finished PolkitAgentSession can't be reused: another one
                // starts, outside this callback. If the user had answered,
                // the answer was wrong. If not, the session ended by itself
                // (the helper doesn't start, PAM refuses at once): it is
                // tried once more, then the prompt says authentication isn't
                // available, instead of looping.
                Current& current = *m_current;
                if (current.answered) {
                    current.silentFailures = 0;
                    send(encode("retry"));
                } else if (++current.silentFailures >= 2) {
                    current.generation = 0;
                    send(encode("failed"));
                    return;
                }
                current.generation = 0; // no event from the old one gets through any more
                m_backend.defer([this, valid = std::weak_ptr<bool>(m_alive), serial = m_requestSerial] {
                    if (!valid.expired() && m_current && m_requestSerial == serial) {
                        startSession();
                    }
                });
            },
        });
}

void Agent::onPromptLine(const std::string& line)
{
    const auto message = parse(line);
    if (!message) {
        return;
    }
    Current& current = *m_current;
    if (message->word == "response") {
        std::string answer = message->argument;
        current.answered = true;
        if (current.session) {
            current.session->respond(answer);
        }
        wipe(answer);
    } else if (message->word == "identity") {
        const uint32_t uid = uint32_t(std::strtoul(message->argument.c_str(), nullptr, 10));
        const auto& identities = current.entry.request.identities;
        for (size_t i = 0; i < identities.size(); ++i) {
            if (identities[i].uid == uid && i != current.identity) {
                current.identity = i;
                current.silentFailures = 0;
                startSession();
                break;
            }
        }
    } else if (message->word == "cancel") {
        finish(Outcome::Cancelled, nullptr); // the prompt is already closing
    }
}

void Agent::finish(Outcome outcome, const char* toPrompt)
{
    std::unique_ptr<Current> current = std::move(m_current);
    current->generation = 0;
    if (current->session) {
        current->session->cancel();
    }
    if (toPrompt && current->prompt) {
        current->prompt->send(encode(toPrompt));
    }
    ++m_requestSerial; // deferred events of this request no longer count
    Done done = std::move(current->entry.done);
    // Prompt and session are destroyed outside their callbacks.
    m_backend.defer([current = std::shared_ptr<Current>(std::move(current))] { });
    done(outcome);
    m_backend.defer([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (!alive.expired() && !m_current) {
            startNext();
        }
    });
}

void Agent::send(const std::string& line)
{
    if (m_current && m_current->prompt) {
        m_current->prompt->send(line);
    }
}

} // namespace vela::polkit
