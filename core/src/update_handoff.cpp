// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_handoff.hpp"

#include "etude/semver.hpp"

#include <charconv>
#include <cstddef>

namespace etude {

namespace {

constexpr std::size_t kMaxValue = 300;

bool plain_version(std::string_view v) {
    const std::optional<SemVer> parsed = parse_semver(v);
    return parsed && parsed->prerelease.empty() && v.find('+') == std::string_view::npos;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

std::string format_update_request(std::string_view version) {
    return plain_version(version) ? "version=" + std::string(version) + "\n" : std::string();
}

std::string format_update_ready(std::string_view version) { return format_update_request(version); }

UpdateStatus parse_update_status(std::string_view text) {
    UpdateStatus status;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = trimmed(text.substr(at, end - at));
        at = end + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, eq);
        std::string_view value = line.substr(eq + 1);
        if (value.size() > kMaxValue) {
            value = value.substr(0, kMaxValue);
        }
        if (key == "state") {
            status.state = value == "applying"      ? UpdateState::Applying
                           : value == "ok"          ? UpdateState::Ok
                           : value == "rolled-back" ? UpdateState::RolledBack
                           : value == "failed"      ? UpdateState::Failed
                                                    : UpdateState::None;
        } else if (key == "version") {
            status.version = plain_version(value) ? std::string(value) : std::string();
        } else if (key == "from") {
            status.from = plain_version(value) ? std::string(value) : std::string();
        } else if (key == "reason") {
            status.reason.clear();
            for (const char c : value) {
                status.reason.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
            }
        } else if (key == "data") {
            status.data.clear();
            for (const char c : value) {
                status.data.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
            }
        } else if (key == "time") {
            std::from_chars(value.data(), value.data() + value.size(), status.time_s);
        }
    }
    return status;
}

} // namespace etude
