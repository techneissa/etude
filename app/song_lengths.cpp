// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_lengths.hpp"

#include "song_library.hpp"

#include "etude/library_state.hpp"

#include <set>

namespace etude::app {

SongLengths song_lengths_of(const SongLibrary& library) {
    SongLengths lengths;
    std::set<std::string> shared; // files that hold more than one song
    const std::vector<LibraryItem>& items = library.state().items;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string key = library.path_at(i).lexically_normal().generic_string();
        if (!library.tune_at(i).empty()) {
            shared.insert(key);
            continue;
        }
        if (items[i].state == EntryState::Loaded && items[i].entry.duration_s > 0.0) {
            lengths[key] = items[i].entry.duration_s;
        }
    }
    for (const std::string& key : shared) {
        lengths.erase(key);
    }
    return lengths;
}

} // namespace etude::app
