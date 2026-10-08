// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// The core of vela-polkit-agent (docs/polkit-agent.md §4, §10) with a fake
// backend: the prompt is a list of received lines, the test drives the PAM
// session. No polkit, no PAM.

#include "../agent/agent.hpp"
#include "../protocol.hpp"

#include <gtest/gtest.h>

#include <map>
#include <optional>

using namespace vela::polkit;

namespace {

// Sessions' and prompts' state stays with the test even after the agent has
// destroyed them: the agent gets only a handle.
struct FakeSession {
    Identity identity;
    std::string cookie;
    AuthSession::Events events;
    std::vector<std::string> answers;
    bool cancelled = false;
    bool destroyed = false;
};

struct SessionHandle : AuthSession {
    std::shared_ptr<FakeSession> state;
    void respond(std::string& answer) override
    {
        state->answers.push_back(answer);
        wipe(answer);
    }
    void cancel() override { state->cancelled = true; }
    ~SessionHandle() override { state->destroyed = true; }
};

struct FakePrompt {
    Prompt::Events events;
    std::vector<std::string> lines; // without newline
    bool closed = false;
};

struct PromptHandle : Prompt {
    std::shared_ptr<FakePrompt> state;
    void send(const std::string& line) override { state->lines.push_back(line.substr(0, line.size() - 1)); }
    ~PromptHandle() override { state->closed = true; }
};

struct FakeBackend : Backend {
    std::vector<std::shared_ptr<FakeSession>> sessions;
    std::vector<std::shared_ptr<FakePrompt>> prompts;
    std::vector<std::function<void()>> deferred;
    bool promptFails = false;

    std::unique_ptr<AuthSession> startSession(
        const Identity& identity, const std::string& cookie, AuthSession::Events events) override
    {
        auto handle = std::make_unique<SessionHandle>();
        handle->state = std::make_shared<FakeSession>();
        handle->state->identity = identity;
        handle->state->cookie = cookie;
        handle->state->events = std::move(events);
        sessions.push_back(handle->state);
        return handle;
    }
    std::unique_ptr<Prompt> startPrompt(Prompt::Events events) override
    {
        if (promptFails) {
            return nullptr;
        }
        auto handle = std::make_unique<PromptHandle>();
        handle->state = std::make_shared<FakePrompt>();
        handle->state->events = std::move(events);
        prompts.push_back(handle->state);
        return handle;
    }
    void defer(std::function<void()> task) override { deferred.push_back(std::move(task)); }

    // The main loop: runs what was deferred.
    void run()
    {
        while (!deferred.empty()) {
            auto tasks = std::move(deferred);
            deferred.clear();
            for (auto& task : tasks) {
                task();
            }
        }
    }
    FakeSession& session() { return *sessions.back(); }
    FakePrompt& prompt() { return *prompts.back(); }
};

Identity user(uint32_t uid, const std::string& login)
{
    return { uid, login, login + " name", {} };
}

Request request(const std::string& cookie, std::vector<Identity> identities = { user(1000, "me") })
{
    Request r;
    r.cookie = cookie;
    r.actionId = "org.example.action";
    r.message = "Authentication is required";
    r.iconName = "dialog-password";
    r.details = { { "polkit.subject-pid", "42" } };
    r.identities = std::move(identities);
    r.appName = "Example";
    r.appIcon = "example";
    return r;
}

class AgentTest : public ::testing::Test {
protected:
    FakeBackend backend;
    Agent agent { backend };
    std::map<std::string, std::vector<Outcome>> outcomes;

    void begin(Request r)
    {
        const std::string cookie = r.cookie;
        agent.begin(std::move(r), [this, cookie](Outcome outcome) { outcomes[cookie].push_back(outcome); });
        backend.run();
    }
    std::optional<Outcome> outcome(const std::string& cookie)
    {
        auto& list = outcomes[cookie];
        EXPECT_LE(list.size(), 1u) << "done called more than once for " << cookie;
        return list.empty() ? std::nullopt : std::optional<Outcome>(list.front());
    }
    void fromPrompt(const std::string& word, const std::string& argument = {})
    {
        std::string line = encode(word, argument);
        line.pop_back();
        backend.prompt().events.line(line);
        backend.run();
    }
};

} // namespace

TEST(Protocol, EscapesRoundTrip)
{
    for (const std::string text : { "", "plain", "a\\b", "two\nlines", "back\\nslash-n", "trailing\\", "\r\n" }) {
        const std::string line = encode("word", text);
        ASSERT_EQ(line.back(), '\n');
        EXPECT_EQ(std::count(line.begin(), line.end(), '\n'), 1) << text;
        const auto message = parse(std::string_view(line).substr(0, line.size() - 1));
        ASSERT_TRUE(message);
        EXPECT_EQ(message->word, "word");
        EXPECT_EQ(message->argument, text);
    }
}

TEST(Protocol, ParsesWordsAndSkipsEmptyLines)
{
    EXPECT_FALSE(parse(""));
    EXPECT_EQ(parse("show")->word, "show");
    EXPECT_EQ(parse("show")->argument, "");
    const auto message = parse("detail key=a value with spaces");
    EXPECT_EQ(message->word, "detail");
    EXPECT_EQ(message->argument, "key=a value with spaces");
}

TEST(Protocol, LineBufferSplitsChunks)
{
    LineBuffer buffer;
    std::string line;
    buffer.append("resp");
    EXPECT_FALSE(buffer.next(line));
    buffer.append("onse secret\ncancel\npart");
    ASSERT_TRUE(buffer.next(line));
    EXPECT_EQ(line, "response secret");
    ASSERT_TRUE(buffer.next(line));
    EXPECT_EQ(line, "cancel");
    EXPECT_FALSE(buffer.next(line));
    EXPECT_EQ(buffer.pendingSize(), 4u);
}

TEST(Identities, CurrentUserFirstRootLast)
{
    const auto ordered = orderIdentities({ user(0, "root"), user(1001, "other"), user(1000, "me"), user(1002, "third") }, 1000);
    ASSERT_EQ(ordered.size(), 4u);
    EXPECT_EQ(ordered[0].uid, 1000u);
    EXPECT_EQ(ordered[1].uid, 1001u);
    EXPECT_EQ(ordered[2].uid, 1002u);
    EXPECT_EQ(ordered[3].uid, 0u);
}

TEST(Identities, RootFirstWhenRootAsks)
{
    const auto ordered = orderIdentities({ user(1000, "me"), user(0, "root") }, 0);
    EXPECT_EQ(ordered[0].uid, 0u);
}

TEST(Caller, PkexecProgramWins)
{
    const Details details { { "polkit.subject-pid", "10" }, { "program", "/usr/bin/gparted" } };
    EXPECT_EQ(callerExecutable(details, [](int) { return std::string("/usr/bin/fish"); }), "/usr/bin/gparted");
}

TEST(Caller, SubjectThenCallerPid)
{
    const Details details { { "polkit.subject-pid", "10" }, { "polkit.caller-pid", "20" } };
    auto exe = [](int pid) { return pid == 20 ? std::string("/usr/bin/vela-settings") : std::string(); };
    EXPECT_EQ(callerExecutable(details, exe), "/usr/bin/vela-settings");
    EXPECT_EQ(callerExecutable({ { "polkit.subject-pid", "junk" } }, exe), "");
    EXPECT_EQ(callerExecutable({}, exe), "");
}

TEST_F(AgentTest, ShowsTheRequestAndAuthorizes)
{
    begin(request("c1", { user(1000, "me"), user(0, "root") }));
    ASSERT_EQ(backend.prompts.size(), 1u);
    const auto& lines = backend.prompt().lines;
    const std::vector<std::string> expected {
        "action org.example.action",
        "message Authentication is required",
        "icon dialog-password",
        "app Example",
        "app-icon example",
        "detail polkit.subject-pid=42",
        "identity 1000 me me name",
        "identity 0 root root name",
        "show",
    };
    EXPECT_EQ(lines, expected);
    ASSERT_EQ(backend.sessions.size(), 1u);
    EXPECT_EQ(backend.session().identity.uid, 1000u);
    EXPECT_EQ(backend.session().cookie, "c1");

    backend.session().events.request("Password: ", false);
    EXPECT_EQ(backend.prompt().lines.back(), "request 0 Password: ");
    fromPrompt("response", "s3cret");
    ASSERT_EQ(backend.session().answers.size(), 1u);
    EXPECT_EQ(backend.session().answers[0], "s3cret");
    backend.session().events.completed(true);
    EXPECT_EQ(backend.prompt().lines.back(), "done");
    backend.run();
    EXPECT_EQ(outcome("c1"), Outcome::Authorized);
    EXPECT_FALSE(agent.active());
    // Prompt (stdin closed) and session don't linger.
    EXPECT_TRUE(backend.prompt().closed);
    EXPECT_TRUE(backend.session().destroyed);
}

TEST_F(AgentTest, PamMessagesReachThePrompt)
{
    begin(request("c1"));
    backend.session().events.info("Place your finger on the reader");
    backend.session().events.error("The account is locked");
    backend.session().events.request("Username:", true);
    const auto& lines = backend.prompt().lines;
    EXPECT_EQ(lines[lines.size() - 3], "info Place your finger on the reader");
    EXPECT_EQ(lines[lines.size() - 2], "error The account is locked");
    EXPECT_EQ(lines[lines.size() - 1], "request 1 Username:");
}

TEST_F(AgentTest, WrongPasswordRetriesWithANewSession)
{
    begin(request("c1"));
    FakeSession* first = backend.sessions.back().get();
    first->events.request("Password: ", false);
    fromPrompt("response", "wrong");
    first->events.completed(false);
    EXPECT_EQ(backend.prompt().lines.back(), "retry");
    backend.run();
    ASSERT_EQ(backend.sessions.size(), 2u);
    EXPECT_EQ(backend.session().identity.uid, 1000u);
    EXPECT_FALSE(outcome("c1"));
    // Once more, then right.
    backend.session().events.completed(false);
    backend.run();
    ASSERT_EQ(backend.sessions.size(), 3u);
    backend.session().events.completed(true);
    backend.run();
    EXPECT_EQ(outcome("c1"), Outcome::Authorized);
}

TEST_F(AgentTest, SessionThatFailsByItselfDoesNotLoop)
{
    // The helper doesn't start: the session ends without asking anything.
    begin(request("c1"));
    backend.session().events.completed(false);
    backend.run();
    ASSERT_EQ(backend.sessions.size(), 2u); // a second try, silently
    EXPECT_NE(backend.prompt().lines.back(), "retry");
    backend.session().events.completed(false);
    backend.run();
    EXPECT_EQ(backend.sessions.size(), 2u); // then no more
    EXPECT_EQ(backend.prompt().lines.back(), "failed");
    EXPECT_FALSE(outcome("c1")); // the user decides, with "No"
    fromPrompt("cancel");
    EXPECT_EQ(outcome("c1"), Outcome::Cancelled);
}

TEST_F(AgentTest, AnsweringResetsSilentFailures)
{
    begin(request("c1"));
    backend.session().events.completed(false); // failed by itself
    backend.run();
    fromPrompt("response", "wrong");
    backend.session().events.completed(false); // wrong password
    backend.run();
    EXPECT_EQ(backend.prompt().lines.back(), "retry");
    backend.session().events.completed(false); // by itself again: tried again
    backend.run();
    EXPECT_EQ(backend.sessions.size(), 4u);
}

TEST_F(AgentTest, UserSaysNo)
{
    begin(request("c1"));
    FakeSession& session = backend.session();
    fromPrompt("cancel");
    EXPECT_TRUE(session.cancelled);
    EXPECT_EQ(outcome("c1"), Outcome::Cancelled);
    // The cancelled session reports "completed(false)": no retry.
    session.events.completed(false);
    backend.run();
    EXPECT_EQ(backend.sessions.size(), 1u);
    EXPECT_FALSE(agent.active());
}

TEST_F(AgentTest, PromptCrashCountsAsNo)
{
    begin(request("c1"));
    FakeSession& session = backend.session();
    backend.prompt().events.exited();
    backend.run();
    EXPECT_TRUE(session.cancelled);
    EXPECT_EQ(outcome("c1"), Outcome::Cancelled);
}

TEST_F(AgentTest, PromptThatDoesNotStartFails)
{
    backend.promptFails = true;
    begin(request("c1"));
    EXPECT_EQ(outcome("c1"), Outcome::Failed);
    EXPECT_TRUE(backend.sessions.empty());
}

TEST_F(AgentTest, NoIdentitiesFails)
{
    begin(request("c1", {}));
    EXPECT_EQ(outcome("c1"), Outcome::Failed);
    EXPECT_TRUE(backend.prompts.empty());
}

TEST_F(AgentTest, RequestsAreQueuedAndShownInOrder)
{
    begin(request("c1"));
    begin(request("c2"));
    begin(request("c3"));
    EXPECT_EQ(backend.prompts.size(), 1u);
    EXPECT_EQ(agent.queued(), 2u);
    backend.session().events.completed(true);
    backend.run();
    EXPECT_EQ(outcome("c1"), Outcome::Authorized);
    ASSERT_EQ(backend.prompts.size(), 2u);
    EXPECT_EQ(backend.session().cookie, "c2");
    fromPrompt("cancel");
    EXPECT_EQ(outcome("c2"), Outcome::Cancelled);
    ASSERT_EQ(backend.prompts.size(), 3u);
    EXPECT_EQ(backend.session().cookie, "c3");
}

TEST_F(AgentTest, PolkitCancelsTheActiveRequest)
{
    begin(request("c1"));
    begin(request("c2"));
    FakePrompt& prompt = backend.prompt();
    FakeSession& session = backend.session();
    agent.cancel("c1");
    EXPECT_TRUE(session.cancelled);
    EXPECT_EQ(prompt.lines.back(), "cancel");
    EXPECT_EQ(outcome("c1"), Outcome::Cancelled);
    backend.run();
    EXPECT_EQ(backend.session().cookie, "c2"); // the next one starts
}

TEST_F(AgentTest, PolkitCancelsAWaitingRequest)
{
    begin(request("c1"));
    begin(request("c2"));
    begin(request("c3"));
    agent.cancel("c2");
    EXPECT_EQ(outcome("c2"), Outcome::Cancelled);
    EXPECT_FALSE(outcome("c1"));
    EXPECT_EQ(agent.queued(), 1u);
    backend.session().events.completed(true);
    backend.run();
    EXPECT_EQ(backend.session().cookie, "c3");
    agent.cancel("unknown"); // nothing
    EXPECT_FALSE(outcome("c3"));
}

TEST_F(AgentTest, LatePromptExitAfterDoneIsIgnored)
{
    begin(request("c1"));
    begin(request("c2"));
    Prompt::Events first = backend.prompt().events;
    backend.session().events.completed(true);
    backend.run();
    // The first prompt finishes fading and exits while the second is up.
    first.exited();
    backend.run();
    EXPECT_TRUE(agent.active());
    EXPECT_FALSE(outcome("c2"));
}

TEST_F(AgentTest, ChoosingAnotherIdentityRestartsTheSession)
{
    begin(request("c1", { user(1001, "admin"), user(0, "root") }));
    FakeSession& first = backend.session();
    EXPECT_EQ(first.identity.uid, 1001u);
    fromPrompt("identity", "0");
    EXPECT_TRUE(first.cancelled);
    ASSERT_EQ(backend.sessions.size(), 2u);
    EXPECT_EQ(backend.session().identity.uid, 0u);
    // The old session finishing doesn't count.
    first.events.completed(false);
    backend.run();
    EXPECT_EQ(backend.sessions.size(), 2u);
    EXPECT_NE(backend.prompt().lines.back(), "retry");
    // A uid that isn't among the identities is ignored.
    fromPrompt("identity", "4242");
    EXPECT_EQ(backend.sessions.size(), 2u);
    backend.session().events.completed(true);
    backend.run();
    EXPECT_EQ(outcome("c1"), Outcome::Authorized);
}

TEST_F(AgentTest, EventsFromAFinishedRequestAreIgnored)
{
    begin(request("c1"));
    AuthSession::Events old = backend.session().events;
    fromPrompt("cancel");
    begin(request("c2"));
    old.completed(true);
    old.request("Password:", false);
    backend.run();
    EXPECT_EQ(outcome("c1"), Outcome::Cancelled);
    EXPECT_FALSE(outcome("c2"));
    EXPECT_NE(backend.prompt().lines.back(), "done");
}

TEST_F(AgentTest, DetailsAndNamesAreEscaped)
{
    Request r = request("c1");
    r.message = "two\nlines";
    r.details = { { "command_line", "/bin/sh -c 'echo a\\b'" } };
    begin(std::move(r));
    const auto& lines = backend.prompt().lines;
    EXPECT_NE(std::find(lines.begin(), lines.end(), "message two\\nlines"), lines.end());
    EXPECT_NE(std::find(lines.begin(), lines.end(), "detail command_line=/bin/sh -c 'echo a\\\\b'"), lines.end());
}
