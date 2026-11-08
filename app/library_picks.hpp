// SPDX-License-Identifier: GPL-3.0-or-later
// The songs the player picked on the library screen, kept by their names in the library (an index
// does not survive a rescan). A pick stays while the search, the filters or the place change, but
// what is done to the picked songs is done only to those shown: a song out of view is never moved,
// deleted or marked by an action the player could not see it in.
#pragma once

#include "library_catalogue.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace etude::app {

class LibraryPicks {
  public:
    void toggle(const std::string& relative);
    void add(const std::string& relative) { picked_.insert(relative); }
    bool has(const std::string& relative) const {
        return !picked_.empty() && picked_.find(relative) != picked_.end();
    }
    void clear() { picked_.clear(); }
    bool empty() const { return picked_.empty(); }
    std::size_t size() const { return picked_.size(); }
    // Picks of songs the library no longer lists (a rescan, a delete) go.
    void prune(const LibraryCatalogue& cat);
    // The picked songs among `shown` (catalogue indices, in the order the screen shows them).
    void shown(const LibraryCatalogue& cat, std::span<const std::uint32_t> shown,
               std::vector<std::size_t>& out) const;

  private:
    std::unordered_set<std::string> picked_;
};

} // namespace etude::app
