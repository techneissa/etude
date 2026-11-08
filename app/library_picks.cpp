// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_picks.hpp"

namespace etude::app {

void LibraryPicks::toggle(const std::string& relative) {
    if (const auto it = picked_.find(relative); it != picked_.end()) {
        picked_.erase(it);
    } else {
        picked_.insert(relative);
    }
}

void LibraryPicks::prune(const LibraryCatalogue& cat) {
    for (auto it = picked_.begin(); it != picked_.end();) {
        if (cat.index_of(*it).has_value()) {
            ++it;
        } else {
            it = picked_.erase(it);
        }
    }
}

void LibraryPicks::shown(const LibraryCatalogue& cat, std::span<const std::uint32_t> shown,
                         std::vector<std::size_t>& out) const {
    out.clear();
    if (picked_.empty()) {
        return;
    }
    for (const std::uint32_t i : shown) {
        if (i < cat.size() && has(cat.song(i).relative)) {
            out.push_back(i);
        }
    }
}

} // namespace etude::app
