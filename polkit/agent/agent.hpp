// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The core of vela-polkit-agent (docs/polkit-agent.md §4): the queue of
// requests, the PAM session of the request at the head and the prompt that
// shows it. It knows neither GLib nor polkit: session and prompt come from a
// Backend, the real one (backend.cpp) or the tests' fake one.

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vela::polkit {

struct Identity {
    uint32_t uid = 0;
    std::string login;
    std::string name; // from the GECOS field, or the login
    std::string avatar; // path of an image, or empty
};

using Details = std::vector<std::pair<std::string, std::string>>;

struct Request {
    std::string cookie;
    std::string actionId;
    std::string message; // already translated by polkitd
    std::string iconName;
    Details details;
    std::vector<Identity> identities; // already in order (orderIdentities)
    std::string appName; // who is asking (§4.2)
    std::string appIcon;
};

enum class Outcome {
    Authorized,
    Cancelled, // by the user or by polkit
    Failed, // the prompt doesn't start
};

// A PAM conversation for one identity (PolkitAgentSession). Events come
// from the main loop, never from inside respond() or cancel().
class AuthSession {
public:
    struct Events {
        std::function<void(const std::string& text, bool echo)> request;
        std::function<void(const std::string& text)> error;
        std::function<void(const std::string& text)> info;
        std::function<void(bool authorized)> completed;
    };
    virtual ~AuthSession() = default;
    virtual void respond(std::string& answer) = 0; // wipes it after use
    virtual void cancel() = 0;
};

// The vela-polkit-prompt process, with its two pipes.
class Prompt {
public:
    struct Events {
        std::function<void(const std::string& line)> line;
        std::function<void()> exited; // exited, or closed its output
    };
    virtual ~Prompt() = default; // closes the prompt's stdin
    virtual void send(const std::string& line) = 0;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual std::unique_ptr<AuthSession> startSession(
        const Identity& identity, const std::string& cookie, AuthSession::Events events)
        = 0;
    // nullptr if the prompt can't be started.
    virtual std::unique_ptr<Prompt> startPrompt(Prompt::Events events) = 0;
    // Runs `task` from the main loop, outside the current callback.
    virtual void defer(std::function<void()> task) = 0;
};

// The current user first (no choice to make, like Windows), root last, the
// rest in polkit's order.
std::vector<Identity> orderIdentities(std::vector<Identity> identities, uint32_t currentUid);

// The program whose name and icon to show (§4.2): for pkexec the one to run
// (the "program" detail), otherwise the executable of the asking process
// (polkit.subject-pid, then polkit.caller-pid). `exeOf` reads
// /proc/<pid>/exe; empty if unknown.
std::string callerExecutable(const Details& details, const std::function<std::string(int pid)>& exeOf);

class Agent {
public:
    using Done = std::function<void(Outcome)>;

    explicit Agent(Backend& backend);
    ~Agent();

    // A request from polkitd: queued, then shown. `done` is called exactly
    // once.
    void begin(Request request, Done done);
    // polkitd withdraws the request (CancelAuthentication).
    void cancel(const std::string& cookie);

    size_t queued() const { return m_queue.size(); }
    bool active() const { return m_current != nullptr; }

private:
    struct Entry {
        Request request;
        Done done;
    };
    struct Current {
        Entry entry;
        std::unique_ptr<Prompt> prompt;
        std::unique_ptr<AuthSession> session;
        size_t identity = 0; // index in entry.request.identities
        uint64_t generation = 0; // of the session: events from older ones are ignored
        bool answered = false; // the user answered in this session
        int silentFailures = 0; // sessions failed in a row without answers
    };

    void startNext();
    void startSession();
    void onPromptLine(const std::string& line);
    void finish(Outcome outcome, const char* toPrompt);
    void send(const std::string& line);

    Backend& m_backend;
    std::deque<Entry> m_queue;
    std::unique_ptr<Current> m_current;
    uint64_t m_generation = 0;
    uint64_t m_requestSerial = 0; // to recognize deferred events of a finished request
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace vela::polkit
