// SPDX-License-Identifier: GPL-3.0-or-later
// What the three files of song_versions share: the melody comparison (song_versions_melody.cpp),
// the reading of a title (song_versions_title.cpp) and the grouping that uses both
// (song_versions.cpp). Not part of the library's interface.
#pragma once

#include "etude/song_versions.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// ---- The melody's fingerprint (song_versions_melody.cpp)

// Two notes of one song are at most 127 semitones apart, so a key change is within 254 either way.
constexpr int kMaxShift = 2 * 127;

// Buffers a comparison reuses, so comparing thousands of pairs allocates nothing after the first.
struct Scratch {
    std::array<int, 2 * kMaxShift + 1> votes{};
    std::vector<int> h_prev;
    std::vector<int> h_cur;
};

std::vector<int> pitches_of(const MelodyContour& c);

// The similarity of two melodies given as pitches (the first at 0), usable or not.
double similarity_of(const std::vector<int>& pa, const std::vector<int>& pb, Scratch& scratch);

// ---- What the file says (song_versions_title.cpp)

// Words left out of a title's key: they are not what tells two pieces apart.
inline constexpr std::string_view kStopWords[] = {"the", "a", "an", "piano", "solo"};

inline bool in_list(std::string_view word, std::span<const std::string_view> list) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

inline bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
        return static_cast<unsigned char>(c) >= '0' && static_cast<unsigned char>(c) <= '9';
    });
}

inline std::string join(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& w : words) {
        if (!out.empty()) {
            out += ' ';
        }
        out += w;
    }
    return out;
}

// A word of a title as written, with where it sits in the text and its folded form.
struct TitleToken {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string word;
};

std::vector<TitleToken> tokens_of(std::string_view text);

// Which tokens are the title's level words and the words beside them (as facts_of_title takes
// them out).
std::vector<char> level_tokens(const std::vector<TitleToken>& t);

// What a title says: its level, its catalogue number and the words of the piece.
struct TitleFacts {
    VersionLabel label = VersionLabel::None;
    std::string catalogue;
    std::vector<std::string> core; // in order, the level words and numbers taken out
};

TitleFacts facts_of_title(std::string_view title);

} // namespace etude
