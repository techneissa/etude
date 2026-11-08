// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of song_library.cpp share. Not part of the app's interface.
#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_library.hpp"

#include "hand_split_cache.hpp"

#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <utility>

namespace etude::app {

namespace fs = std::filesystem;

inline std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// The path's text with '/' separators.
inline std::string generic(const fs::path& p) { return p.generic_string(); }

inline HandSplitCache& hand_split_cache() {
    static HandSplitCache cache;
    return cache;
}

// Splits the hands of `song`, taking the answer from an earlier load of the same file when there
// is one.
inline void split_hands_cached(Song& song, std::uint64_t hash, bool abc_chords) {
    const HandSplitCache::Key key = HandSplitCache::key_of(hash, abc_chords, HandSplitOptions{});
    if (const auto known = hand_split_cache().find(key);
        known != nullptr && apply_hand_split(song, *known)) {
        return;
    }
    auto split = std::make_shared<const HandSplit>(compute_hand_split(song));
    const bool applied = apply_hand_split(song, *split, true);
    if (applied) {
        hand_split_cache().put(key, std::move(split));
    }
}

} // namespace etude::app
