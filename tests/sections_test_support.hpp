// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the sections_*test.cpp files (split from sections_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/sections.hpp"
#include "section_fixtures.hpp"
#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace sections_test_support {

using namespace etude;
using namespace etude::testing;

// A 16-bar song is 32 s long at 120 BPM in 4/4: bar n spans [2(n-1), 2n).
inline Song song16() { return bars_song(16); }

inline std::vector<std::string> names(const SongSections& s) {
    std::vector<std::string> out;
    for (const Section& x : s.sections) {
        out.push_back(x.name);
    }
    return out;
}

inline SongSections with_markers(const Song& song, std::initializer_list<double> times) {
    SongSections s;
    s.song_hash = 7;
    for (double t : times) {
        REQUIRE(s.add_marker(song, t).ok);
    }
    REQUIRE(s.valid(song));
    return s;
}

} // namespace sections_test_support
