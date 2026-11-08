// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_conf.hpp"

#include <charconv>
#include <set>

namespace etude {

namespace {

bool path_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-' || c == '+' || c == '@' || c == ':' || c == ',' ||
           c == '/';
}

bool whole_number(std::string_view text, int low, int high, int& out) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || value < low || value > high) {
        return false;
    }
    out = value;
    return true;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// The value of `KEY=value` or `KEY="value"`; false for a quote that does not close.
bool unquoted(std::string_view raw, std::string& out) {
    raw = trimmed(raw);
    if (!raw.empty() && raw.front() == '"') {
        if (raw.size() < 2 || raw.back() != '"') {
            return false;
        }
        raw = raw.substr(1, raw.size() - 2);
    }
    out.assign(raw);
    return true;
}

bool assign(UpdateConf& conf, const std::string& key, const std::string& value) {
    if (key == "PREFIX") {
        conf.prefix = value;
        return plain_absolute_path(value);
    }
    if (key == "STATE_DIR") {
        conf.state_dir = value;
        return plain_absolute_path(value);
    }
    if (key == "SYSTEMCTL") {
        conf.systemctl = value;
        return plain_absolute_path(value) && value != "/";
    }
    if (key == "APP_USER") {
        conf.app_user = value;
        return plain_user_name(value);
    }
    if (key == "SERVICE_TIMEOUT") {
        return whole_number(value, 2, kMaxServiceTimeoutS, conf.service_timeout_s);
    }
    if (key == "READY_TIMEOUT") {
        return whole_number(value, 1, kMaxReadyTimeoutS, conf.ready_timeout_s);
    }
    if (key == "MAX_STARTS") {
        return whole_number(value, 1, kMaxStartsLimit, conf.max_starts);
    }
    return false;
}

bool known_key(const std::string& key) {
    static const std::set<std::string> keys{"PREFIX",        "APP_USER",      "STATE_DIR",
                                            "SYSTEMCTL",     "SERVICE_TIMEOUT", "READY_TIMEOUT",
                                            "MAX_STARTS"};
    return keys.count(key) != 0;
}

ConfResult refuse(ConfError error, int line, std::string key = {}) {
    ConfResult r;
    r.error = error;
    r.line = line;
    r.key = std::move(key);
    return r;
}

} // namespace

bool plain_absolute_path(std::string_view path) {
    if (path.empty() || path.size() > 200 || path.front() != '/') {
        return false;
    }
    for (const char c : path) {
        if (!path_char(c)) {
            return false;
        }
    }
    if (path == "/") {
        return true;
    }
    if (path.back() == '/') {
        return false;
    }
    std::size_t at = 1;
    while (at <= path.size()) {
        std::size_t end = path.find('/', at);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view part = path.substr(at, end - at);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        at = end + 1;
    }
    return true;
}

bool plain_user_name(std::string_view name) {
    if (name.empty() || name.size() > 32) {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool head = (c >= 'a' && c <= 'z') || c == '_';
        const bool tail = head || (c >= '0' && c <= '9') || c == '-';
        if (i == 0 ? !head : !tail) {
            return false;
        }
    }
    return true;
}

ConfResult parse_update_conf(std::string_view text) {
    if (text.size() > kMaxConfBytes) {
        return refuse(ConfError::TooLarge, 0);
    }
    ConfResult result;
    std::set<std::string> seen;
    int number = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = trimmed(text.substr(at, end - at));
        at = end + 1;
        ++number;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            return refuse(ConfError::BadLine, number);
        }
        const std::string key(trimmed(line.substr(0, eq)));
        std::string value;
        if (!unquoted(line.substr(eq + 1), value)) {
            return refuse(ConfError::BadValue, number, key);
        }
        if (!known_key(key)) {
            return refuse(ConfError::UnknownKey, number, key);
        }
        if (!seen.insert(key).second) {
            return refuse(ConfError::Duplicate, number, key);
        }
        if (!assign(result.conf, key, value)) {
            return refuse(ConfError::BadValue, number, key);
        }
    }
    return result;
}

std::string conf_error_text(const ConfResult& r) {
    const std::string where = r.line > 0 ? "line " + std::to_string(r.line) + ": " : "";
    switch (r.error) {
    case ConfError::None:
        return {};
    case ConfError::TooLarge:
        return "the file is too large";
    case ConfError::BadLine:
        return where + "not a KEY=value line";
    case ConfError::UnknownKey:
        return where + r.key + " is not a setting of this file";
    case ConfError::Duplicate:
        return where + r.key + " is set twice";
    case ConfError::BadValue:
        return where + r.key + " has a value that is not allowed";
    }
    return {};
}

} // namespace etude
