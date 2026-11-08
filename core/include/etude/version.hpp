// SPDX-License-Identifier: GPL-3.0-or-later
// The version of this build. It comes from the project version in CMakeLists.txt, so a release tag
// `v1.2.3` and the number the app shows are the same one.
#pragma once

#include <string>
#include <string_view>

namespace etude {

// "1.2.3": a plain semantic version (semver.hpp reads it).
std::string_view app_version();

// What `etude --version` prints: "etude 1.2.3".
std::string version_line();

} // namespace etude
