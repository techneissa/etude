// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of sheet_source.cpp share: MusicXML text, pitch spelling and note values
// (sheet_source_notation.cpp), used by the writer (sheet_source_writer.cpp) and the MIDI grid
// (sheet_source.cpp). Not part of the library's interface.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

void append_int(std::string& out, long long v);
void append_escaped(std::string& out, std::string_view text);

inline constexpr std::array<int, 7> kNaturalPc{0, 2, 4, 5, 7, 9, 11}; // C D E F G A B
inline constexpr const char* kSteps = "CDEFGAB";

// The alteration the key signature gives each letter.
std::array<int, 7> key_alters(int fifths);

struct Spelled {
    int letter = 0; // index into kSteps
    int alter = 0;
    int octave = 4;
};

// A key's own notes keep the key's spelling; a white key outside the key is its natural; a black
// key outside it is a sharp in a sharp key (and C major), else a flat.
Spelled spell(int pitch, int fifths);

const char* accidental_name(int alter);

struct Value {
    std::int64_t ticks = 0;   // what it lasts
    int type = 0;             // index into kTypes
    int dots = 0;
    int actual = 0, normal = 0; // tuplet ratio; 0 for none
    std::int64_t nominal = 0;   // its undotted written value, in ticks, for tuplet groups (a
                                // tuplet of `actual` such values lasts `normal` of them)
};

inline constexpr const char* kTypes[] = {"whole", "half", "quarter", "eighth", "16th", "32nd", "64th"};
inline constexpr int kTypeCount = 7;

// Values for `length` real ticks: plain, else in a tuplet of 3, 5, 7 or 9 (written longer by
// actual/normal). Falls back to the nearest 64th-note values when nothing is exact.
std::vector<Value> values_for(std::int64_t length, std::int64_t ppq);

} // namespace etude
