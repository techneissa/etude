// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the session_state_*test.cpp files (split from session_state_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/session_state.hpp"
#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace session_state_test_support {

using namespace etude;
using Kind = SettingsIssueKind;

inline bool has_issue(const std::vector<SettingsIssue>& issues, Kind kind, const std::string& key = "") {
    for (const SettingsIssue& i : issues) {
        if (i.kind == kind && (key.empty() || i.key == key)) {
            return true;
        }
    }
    return false;
}

inline SessionState read(const std::string& text, std::vector<SettingsIssue>* issues = nullptr) {
    return read_session(SettingsDocument::parse(text), issues);
}

inline AppSettings read_app(const std::string& text, std::vector<SettingsIssue>* issues = nullptr) {
    return read_app_settings(SettingsDocument::parse(text), issues);
}

} // namespace session_state_test_support
