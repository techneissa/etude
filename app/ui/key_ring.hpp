// SPDX-License-Identifier: GPL-3.0-or-later
// The key ring: a key picked from the twelve on a circle in fifths, the key's name and its mode in
// the middle (layout and the rules in key_ring_layout.hpp). A kit control: the Learn screen's scale
// card uses it, and the free-play scale overlay will with larger keys.
#pragma once

#include "key_ring_layout.hpp"
#include "kit.hpp"

#include "etude/scales.hpp"

namespace etude::app::ui {

struct KeyRingOpts {
    float max_key_pt = key_ring_tok::kMaxKey; // the keys grow with the room up to this many points
    bool live = true; // false: drawn but not pressable
    // The middle shows the picked key and the scale type's word by default. A host whose ring picks
    // a chord's root gives the root's name ("Bb", as chord_name_parts spells it) and the quality's
    // word ("major", "minor", "7", "maj7", "m7b5": a 'b' in it is a flat sign) instead.
    const char* centre_root = nullptr;
    const char* centre_word = nullptr;
};

// The key a key ring shows is a pitch class (0..11, C = 0) and a scale type, which decides how the
// keys are spelled (Db major, C# minor) and the word in the middle. Drawn in the square `area` (in
// ImGui points; the ring takes the largest centred square that fits). Returns the pitch class tapped
// this frame, else -1. The press goes to the key nearest where the finger landed, so no tap falls
// between two keys.
int key_ring(Kit& k, const char* id, const Box& area, int root_pc, ScaleType type,
             const KeyRingOpts& opts = {});

// How a key of `type` is spelled ("Bb", "F#"), into `out` (at least 8 bytes); `note_name_at` draws
// the signs as shapes.
void key_ring_key_name(int pc, ScaleType type, char* out, std::size_t size);

} // namespace etude::app::ui
