// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the catalogue readers: a text helper they share.
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace etude::catalogue_detail {

inline std::vector<std::string_view> split(std::string_view line, char sep) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (true) {
        const std::size_t next = line.find(sep, at);
        out.push_back(line.substr(at, next == std::string_view::npos ? next : next - at));
        if (next == std::string_view::npos) {
            break;
        }
        at = next + 1;
    }
    return out;
}

} // namespace etude::catalogue_detail
