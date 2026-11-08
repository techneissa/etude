// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_state.hpp"

#include "etude/semver.hpp"

#include <charconv>

namespace etude {

namespace {

bool plain_version(std::string_view v) {
    const std::optional<SemVer> parsed = parse_semver(v);
    return v.size() <= 32 && parsed && parsed->prerelease.empty() &&
           v.find('+') == std::string_view::npos;
}

const char* phase_name(UpdatePhase p) {
    switch (p) {
    case UpdatePhase::None:
        return "none";
    case UpdatePhase::Switching:
        return "switching";
    case UpdatePhase::Pending:
        return "pending";
    case UpdatePhase::RollingBack:
        return "rolling-back";
    }
    return "none";
}

bool phase_from(std::string_view name, UpdatePhase& out) {
    for (const UpdatePhase p : {UpdatePhase::None, UpdatePhase::Switching, UpdatePhase::Pending,
                                UpdatePhase::RollingBack}) {
        if (name == phase_name(p)) {
            out = p;
            return true;
        }
    }
    return false;
}

std::string one_line(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7F ? ' ' : c);
    }
    return out.substr(0, 300);
}

PendingParse bad(int line, const std::string& what) {
    PendingParse r;
    r.ok = false;
    r.error = "line " + std::to_string(line) + ": " + what;
    return r;
}

} // namespace

bool known_app_unit(std::string_view unit) {
    return unit == "etude.service" || unit == "etude-labwc.service";
}

std::string crash_screen_unit(std::string_view app_unit) {
    if (app_unit == "etude.service") {
        return "etude-crash-screen.service";
    }
    return app_unit == "etude-labwc.service" ? "etude-labwc-crash-screen.service" : std::string();
}

std::string format_pending_update(const PendingUpdate& s) {
    std::string text = std::string("phase=") + phase_name(s.phase) + "\n";
    if (s.phase == UpdatePhase::None) {
        return text;
    }
    text += "from=" + s.from + "\n";
    text += "to=" + s.to + "\n";
    text += "previous=" + s.previous + "\n";
    text += "unit=" + s.unit + "\n";
    text += "starts=" + std::to_string(s.starts) + "\n";
    text += std::string("backup=") + (s.backed_up ? "yes" : "no") + "\n";
    text += "reason=" + one_line(s.reason) + "\n";
    if (s.restores > 0) {
        text += "restores=" + std::to_string(s.restores) + "\n";
    }
    if (!s.data_error.empty()) {
        text += "data_error=" + one_line(s.data_error) + "\n";
    }
    return text;
}

PendingParse parse_pending_update(std::string_view text) {
    PendingParse result;
    if (text.size() > kMaxStateBytes) {
        return bad(0, "the file is too large");
    }
    PendingUpdate& s = result.state;
    int number = 0;
    bool saw_phase = false;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(at, end - at);
        at = end + 1;
        ++number;
        if (line.empty()) {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            return bad(number, "not a key=value line");
        }
        const std::string_view key = line.substr(0, eq);
        const std::string_view value = line.substr(eq + 1);
        if (key == "phase") {
            if (!phase_from(value, s.phase)) {
                return bad(number, "phase is not known");
            }
            saw_phase = true;
        } else if (key == "from" || key == "to" || key == "previous") {
            if (!value.empty() && !plain_version(value)) {
                return bad(number, std::string(key) + " is not a version");
            }
            (key == "from" ? s.from : key == "to" ? s.to : s.previous).assign(value);
        } else if (key == "unit") {
            if (!value.empty() && !known_app_unit(value)) {
                return bad(number, "unit is not one of the app's services");
            }
            s.unit.assign(value);
        } else if (key == "starts") {
            int n = 0;
            const auto [e, err] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (err != std::errc() || e != value.data() + value.size() || n < 0 || n > 1000) {
                return bad(number, "starts is not a number");
            }
            s.starts = n;
        } else if (key == "backup") {
            if (value != "yes" && value != "no") {
                return bad(number, "backup is not yes or no");
            }
            s.backed_up = value == "yes";
        } else if (key == "restores") {
            int n = 0;
            const auto [e, err] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (err != std::errc() || e != value.data() + value.size() || n < 0 || n > 1000) {
                return bad(number, "restores is not a number");
            }
            s.restores = n;
        } else if (key == "data_error") {
            s.data_error = one_line(value);
        } else if (key == "reason") {
            s.reason = one_line(value);
        } else {
            return bad(number, "unknown key");
        }
    }
    if (!saw_phase) {
        return bad(0, "there is no phase"); // the helper never writes a file without one
    }
    if (s.phase != UpdatePhase::None && (s.from.empty() || s.to.empty())) {
        return bad(0, "an update in progress needs from and to");
    }
    return result;
}

} // namespace etude
