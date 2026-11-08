// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the ABC reader: key, meter, tempo, length and chord-symbol field parsers.
#pragma once

#include "abc_text.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace etude::abc_detail {

// Semitones of shift per letter C D E F G A B.
using KeySig = std::array<int, 7>;

constexpr int kLetterPitchClass[7] = {0, 2, 4, 5, 7, 9, 11};

int letter_index(char c);

// Sharps (positive) or flats (negative) of the key signature: the tonic's place on the circle of
// fifths, moved by the mode.
KeySig key_from_fifths(int fifths);

// The key signature a KeySig writes, as sharps or flats; 0 (none, every accidental written out)
// for a pattern no standard signature has (`exp` and explicit accidentals).
inline int fifths_of(const KeySig& sig) {
    for (int f = -7; f <= 7; ++f) {
        if (key_from_fifths(f) == sig) {
            return f;
        }
    }
    return 0;
}

struct KeyInfo {
    KeySig sig{};
    bool clef_bass = false;
    bool clef_treble = false;
    int shift = 0; // transpose= and octave=, in semitones
    bool has_clef = false;
    bool has_shift = false;
    bool lone_capital_m = false; // the mode was a lone "M": read as major, with a warning
    // The key as a tonic and a mode, when the field names a tonic (not for `none`, `HP` or a field
    // of parameters only): the tonic's place on the line of fifths (C 0, G 1, F -1).
    bool has_tonic = false;
    int tonic_line = 0;
    KeyMode mode = KeyMode::Major;

    bool same_song_key(const KeyInfo& o) const {
        return has_tonic == o.has_tonic && tonic_line == o.tonic_line && mode == o.mode;
    }
};

// Records `k` as the song's key from `offset` ticks into bar `bar`, when it names a tonic that has
// a signature of at most seven accidentals.
inline void note_song_key(score_detail::RawScore& raw, int bar, std::int64_t offset,
                          const KeyInfo& k) {
    if (k.has_tonic) {
        score_detail::add_key(raw, bar, offset, k.tonic_line - mode_line_shift(k.mode), k.mode);
    }
}

// `name=value` or `name="value with spaces"` words of a K: or V: field. Words with no `=` are
// returned with an empty value.
struct Word {
    std::string_view key;
    std::string value;
};

std::vector<Word> split_words(std::string_view s);

bool parse_signed(const std::string& s, int& out);

// The `clef=`, `transpose=` and `octave=` words of a K: or V: field.
void read_voice_words(const std::vector<Word>& words, KeyInfo& info);

// Reads a K: field. Returns false when the tonic or the mode is not understood (C major is used).
bool parse_key(std::string_view value, KeyInfo& out);

// ---- Meter, unit length, tempo

struct Meter {
    int numerator = 4;
    int denominator = 4;
    bool free = false;
};

bool parse_meter(std::string_view value, Meter& out);

bool parse_fraction(std::string_view v, long long& num, long long& den);

// Quarter notes per minute; false when the field gives none.
bool parse_tempo(std::string_view value, double& quarters_per_minute);

// ---- Lengths

struct Frac {
    long long num = 1;
    long long den = 1;
};

Frac read_length(std::string_view s, std::size_t& i);

inline std::int64_t round_div(std::int64_t a, std::int64_t b) {
    return b <= 0 ? 0 : (a + b / 2) / b;
}

// ---- Chord symbols

struct ChordSymbol {
    int bar = 0;
    std::int64_t pos = 0;
    bool none = false; // "N.C.": the previous chord stops
    int root = 0;      // pitch class
    int bass = -1;     // pitch class of a slash chord's bass, -1 for none
    int fifth = 7;     // semitones from the root to the fifth
};

bool parse_chord_symbol(std::string_view text, ChordSymbol& out);

} // namespace etude::abc_detail
