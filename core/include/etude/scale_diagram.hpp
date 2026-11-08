// SPDX-License-Identifier: GPL-3.0-or-later
// The picture the Learn scale card draws: one octave of keys from a scale's tonic to the tonic
// above, with the scale's notes marked, each note's name, and the fingers of both hands. Pure data,
// so a test holds the fingering shown against `scale_fingers` for every key.
#pragma once

#include "etude/scales.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace etude {

struct DiagramKey {
    int note = 60;         // MIDI note number
    bool black = false;
    bool in_scale = false; // a note of the scale (the tonic's octave included)
    bool root = false;     // the tonic, at the bottom and at the top
    int right = 0;         // the right hand's finger (1 thumb .. 5), 0 for none
    int left = 0;          // the left hand's
    int white = 0;         // white keys below this one: its place across (a black key sits on the edge
                           // between the white key before it and the one after, at this many whites)
    // The note's spelling in the scale ("G", "F#", "Bb"), empty for a key that is not in the scale.
    std::array<char, 4> name{};
};

struct ScaleDiagram {
    // Ascending, every key from the white key at or below the tonic to the white key at or above
    // the top tonic, so each end is a whole white key.
    std::vector<DiagramKey> keys;
    int white_keys = 0;
    // The scale has a fingering table (the major, natural, harmonic and melodic minor scales); the
    // other types show none.
    bool fingering = false;
};

// The scale from `root_pc` (0..11, wrapping) in the octave of middle C (the tonic is C4..B4).
ScaleDiagram scale_diagram(int root_pc, ScaleType type);

// "G major", "C# harmonic minor": the card's title, plain spelling ("Bb dorian").
std::string scale_title(int root_pc, ScaleType type);

} // namespace etude
