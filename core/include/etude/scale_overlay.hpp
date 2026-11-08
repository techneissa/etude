// SPDX-License-Identifier: GPL-3.0-or-later
// The free-play scale overlay: which keys of the live keyboard are in the scale, which carry the
// root's name, and which finger the right hand plays each note of the scale with. Worked out once
// when the player's choice changes (`make_scale_overlay`), then only read, so a frame costs a
// lookup and allocates nothing.
#pragma once

#include "etude/scales.hpp"

#include <array>
#include <cstdint>

namespace etude {

struct ScaleOverlay {
    bool on = false;
    int root_pc = 0; // 0..11, C = 0
    ScaleType type = ScaleType::Major;
    bool fingers = false;

    friend bool operator==(const ScaleOverlay&, const ScaleOverlay&) = default;
};

struct ScaleOverlayMask {
    bool on = false;
    int root_pc = 0;
    unsigned pitch_classes = 0; // bit n set when pitch class n is in the scale
    char root_letter = 'C';     // the root as the key is written (Db major: D and -1)
    int root_accidental = 0;
    bool fingers_shown = false; // some note carries a finger
    // The right hand's finger (1 thumb .. 5 little finger) of each MIDI note of two octaves of the
    // scale from the tonic nearest middle C; 0 elsewhere. All zero when fingers are off or the
    // scale type has no fingering table.
    std::array<std::uint8_t, 128> finger{};

    bool in_scale(int note) const {
        return on && note >= 0 && note < 128 && ((pitch_classes >> (note % 12)) & 1u) != 0;
    }
    bool is_root(int note) const { return on && note >= 0 && note < 128 && note % 12 == root_pc; }
};

// True when `type` has a fingering table (the four seven-note scales of exercises.hpp).
bool scale_has_fingering(ScaleType type);

ScaleOverlayMask make_scale_overlay(const ScaleOverlay& overlay);

} // namespace etude
