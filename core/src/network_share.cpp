// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/network_share.hpp"

#include <algorithm>
#include <cctype>

namespace etude {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

// The "key = value" of a line (key in lower case), or false for a comment, a section or a blank.
bool key_value(std::string_view line, std::string& key, std::string& value) {
    line = trimmed(line);
    if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') {
        return false;
    }
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
        return false;
    }
    key = lower(trimmed(line.substr(0, eq)));
    value = std::string(trimmed(line.substr(eq + 1)));
    return true;
}

template <typename Fn> void each_line(std::string_view text, Fn&& fn) {
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        fn(text.substr(at, end - at));
        at = end + 1;
    }
}

} // namespace

bool smb_conf_has_share(std::string_view conf, std::string_view name) {
    const std::string wanted = lower(name);
    bool inside = false;
    bool found = false;
    bool off = false;
    each_line(conf, [&](std::string_view raw) {
        const std::string_view line = trimmed(raw);
        if (!line.empty() && line.front() == '[') {
            const std::size_t close = line.find(']');
            inside = close != std::string_view::npos &&
                     lower(trimmed(line.substr(1, close - 1))) == wanted;
            found = found || inside;
            return;
        }
        std::string key;
        std::string value;
        if (inside && key_value(line, key, value) && key == "available") {
            const std::string v = lower(value);
            off = v == "no" || v == "false" || v == "0";
        }
    });
    return found && !off;
}

std::vector<std::string> smb_conf_includes(std::string_view conf) {
    std::vector<std::string> files;
    each_line(conf, [&](std::string_view line) {
        std::string key;
        std::string value;
        if (key_value(line, key, value) && key == "include" && !value.empty() &&
            value.find('%') == std::string::npos) {
            files.push_back(value);
        }
    });
    return files;
}

std::string share_windows_address() { return std::string("\\\\") + kShareHost + "\\" + kShareName; }

std::string share_smb_address() { return std::string("smb://") + kShareHost + "/" + kShareName; }

} // namespace etude
