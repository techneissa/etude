// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the abc_*test.cpp files (split from abc_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/hand_split.hpp"
#include "etude/library.hpp"
#include "etude/library_state.hpp"
#include "etude/musicxml.hpp"
#include "etude/song_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace abc_test_support {

using namespace etude;

using Bytes = std::vector<std::uint8_t>;

inline ScoreLoadResult abc(const std::string& text, const AbcOptions& options = {}) {
    return load_abc(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), options);
}

// A one-tune file: the header fields, then the music.
inline std::string tune(const std::string& music, const std::string& header = "M:4/4\nL:1/8\nK:C") {
    return "X:1\nT:test\n" + header + "\n" + music + "\n";
}

inline ScoreLoadResult load_music(const std::string& music,
                           const std::string& header = "M:4/4\nL:1/8\nK:C",
                           const AbcOptions& options = {}) {
    return abc(tune(music, header), options);
}

inline Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline std::vector<int> pitches(const Song& song) {
    std::vector<int> out;
    for (const Note& n : song.notes) {
        out.push_back(n.pitch);
    }
    return out;
}

// Start and length of a note in quarter notes.
inline double start_of(const Song& song, const Note& n) {
    return static_cast<double>(n.start_tick) / song.tempo.ticks_per_quarter;
}

inline double length_of(const Song& song, const Note& n) {
    return static_cast<double>(n.end_tick - n.start_tick) / song.tempo.ticks_per_quarter;
}

inline std::vector<double> lengths(const Song& song) {
    std::vector<double> out;
    for (const Note& n : song.notes) {
        out.push_back(length_of(song, n));
    }
    return out;
}

inline std::vector<int> played_bars(const Song& song) {
    std::vector<int> out;
    for (const Measure& m : song.measures) {
        out.push_back(m.written_number());
    }
    return out;
}

inline bool mentions(const std::vector<std::string>& lines, const std::string& word) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& s) { return s.find(word) != std::string::npos; });
}

inline void expect_lengths(const Song& song, const std::vector<double>& want) {
    const std::vector<double> got = lengths(song);
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i);
        CHECK(got[i] == doctest::Approx(want[i]));
    }
}

} // namespace abc_test_support
