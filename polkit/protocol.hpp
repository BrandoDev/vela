// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The protocol between vela-polkit-agent and vela-polkit-prompt
// (docs/polkit-agent.md §5): one message per line, a word, a space, an
// argument. In the argument, "\" and newline become "\\" and "\n". Standard
// library only: both the agent (GLib) and the prompt (Qt) use it.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace vela::polkit {

inline std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}

inline std::string unescape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        const char next = text[++i];
        out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}

// "word argument\n", with the argument escaped. Not "format": with
// std::string arguments, argument-dependent lookup would find std::format.
inline std::string encode(std::string_view word, std::string_view argument = {})
{
    std::string line(word);
    if (!argument.empty()) {
        line += ' ';
        line += escape(argument);
    }
    line += '\n';
    return line;
}

struct Message {
    std::string word;
    std::string argument; // already unescaped
};

// A line without its final newline. Empty lines: no message.
inline std::optional<Message> parse(std::string_view line)
{
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return std::nullopt;
    }
    const size_t space = line.find(' ');
    if (space == std::string_view::npos) {
        return Message { std::string(line), {} };
    }
    return Message { std::string(line.substr(0, space)), unescape(line.substr(space + 1)) };
}

// Collects what comes from a pipe and returns the complete lines.
class LineBuffer {
public:
    void append(std::string_view data) { m_pending.append(data); }

    bool next(std::string& line)
    {
        const size_t end = m_pending.find('\n');
        if (end == std::string::npos) {
            return false;
        }
        line.assign(m_pending, 0, end);
        m_pending.erase(0, end + 1);
        return true;
    }

    // No newline for too long: the writer doesn't follow the protocol.
    size_t pendingSize() const { return m_pending.size(); }

    // The password may have passed through here: wipe it from memory.
    void wipe()
    {
        std::fill(m_pending.begin(), m_pending.end(), '\0');
        m_pending.clear();
    }

private:
    std::string m_pending;
};

// Overwrites a string before freeing it (for passwords; see §9).
inline void wipe(std::string& text)
{
    volatile char* p = text.data();
    for (size_t i = 0; i < text.size(); ++i) {
        p[i] = '\0';
    }
    text.clear();
}

} // namespace vela::polkit
