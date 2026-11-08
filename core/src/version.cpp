// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/version.hpp"

#ifndef ETUDE_VERSION_STRING
#error "the build must define ETUDE_VERSION_STRING (core/CMakeLists.txt does)"
#endif

namespace etude {

std::string_view app_version() {
    return ETUDE_VERSION_STRING;
}

std::string version_line() {
    return "etude " + std::string(app_version());
}

} // namespace etude
