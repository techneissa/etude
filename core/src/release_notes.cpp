// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_feed.hpp"

#include <cstddef>

namespace etude {
namespace {

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

// Lines the generated notes carry that say nothing about the release.
bool generated_filler(std::string_view line) {
    return line == "What's Changed" || line == "What’s Changed" || line == "New Contributors" ||
           starts_with(line, "Full Changelog");
}

// "[text](address)" becomes "text"; the marks of emphasis and code are dropped.
std::string plain(std::string_view line) {
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '*' || c == '`' || (c == '_' && (i == 0 || line[i - 1] == ' ' ||
                                                   i + 1 == line.size() || line[i + 1] == ' '))) {
            continue;
        }
        if (c == '[') {
            const std::size_t close = line.find("](", i);
            const std::size_t end = close == std::string_view::npos ? close : line.find(')', close);
            if (end != std::string_view::npos) {
                out.append(line.substr(i + 1, close - i - 1));
                i = end;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

// A pull request's tail in generated notes: " by @who in https://...".
void drop_pull_request_tail(std::string& line) {
    const std::size_t by = line.find(" by @");
    if (by != std::string::npos && line.find("https://", by) != std::string::npos) {
        line.erase(by);
    }
}

std::size_t code_points(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) {
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

// `line` cut to `max_chars` characters at a word, with an ellipsis.
std::string cut(const std::string& line, std::size_t max_chars) {
    if (code_points(line) <= max_chars) {
        return line;
    }
    std::size_t end = 0;
    std::size_t seen = 0;
    std::size_t last_space = std::string::npos;
    while (end < line.size() && seen < max_chars - 1) {
        ++end;
        while (end < line.size() && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) {
            ++end;
        }
        ++seen;
        if (line[end - 1] == ' ') {
            last_space = end - 1;
        }
    }
    const bool at_word_end = end < line.size() && line[end] == ' ';
    if (!at_word_end && last_space != std::string::npos && last_space > end / 2) {
        end = last_space;
    }
    std::string out = line.substr(0, end);
    while (!out.empty() && (out.back() == ' ' || out.back() == ',' || out.back() == '.')) {
        out.pop_back();
    }
    return out + "…";
}

} // namespace

std::vector<std::string> release_notes_lines(std::string_view notes, std::size_t max_lines,
                                             std::size_t max_chars) {
    std::vector<std::string> lines;
    if (max_chars < 2) {
        return lines;
    }
    bool in_code = false;
    std::size_t at = 0;
    while (at <= notes.size() && lines.size() < max_lines) {
        std::size_t end = notes.find('\n', at);
        if (end == std::string_view::npos) {
            end = notes.size();
        }
        std::string_view raw = trimmed(notes.substr(at, end - at));
        at = end + 1;
        if (starts_with(raw, "```")) {
            in_code = !in_code;
            continue;
        }
        if (in_code || raw.empty() || starts_with(raw, "---") || starts_with(raw, "<!--")) {
            continue;
        }
        while (!raw.empty() && raw.front() == '#') {
            raw.remove_prefix(1);
        }
        if ((starts_with(raw, "- ") || starts_with(raw, "* ") || starts_with(raw, "+ ")) &&
            raw.size() > 2) {
            raw.remove_prefix(2);
        }
        std::string line = plain(trimmed(raw));
        drop_pull_request_tail(line);
        const std::string_view text = trimmed(line);
        if (text.empty() || generated_filler(text)) {
            continue;
        }
        lines.push_back(cut(std::string(text), max_chars));
    }
    return lines;
}

} // namespace etude
