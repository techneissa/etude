// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "etude/color.hpp"
#include "etude/hand.hpp"
#include "etude/look.hpp"

namespace etude {

// The colour of a note block, of the label on it and of the label on its key, for every note the
// look can tell apart, made once when the look changes so a frame only looks them up (High
// contrast lifts each colour with a search, too dear to repeat per note per frame). The scheme
// decides what tells two notes apart: the hand (ByHand), the pitch class, the octave, the
// velocity (all 128) or nothing; a black key has its own colours under every scheme. Holds no
// allocation and gives what `note_color` gives for the same look.
class NoteColourTable {
  public:
    NoteColourTable() = default;
    explicit NoteColourTable(const LookSettings& look);

    RGBA colour(int note, std::uint8_t velocity, Hand hand) const {
        return colours_[slot(note, velocity, hand)];
    }
    RGBA label(int note, std::uint8_t velocity, Hand hand) const {
        return labels_[slot(note, velocity, hand)];
    }
    RGBA key_label(int note, std::uint8_t velocity, Hand hand) const {
        return key_labels_[slot(note, velocity, hand)];
    }

  private:
    static constexpr std::size_t kPerKind = 128;
    std::size_t slot(int note, std::uint8_t velocity, Hand hand) const;

    ColorScheme scheme_ = ColorScheme::ByHand;
    std::array<RGBA, 2 * kPerKind> colours_{};
    std::array<RGBA, 2 * kPerKind> labels_{};
    std::array<RGBA, 2 * kPerKind> key_labels_{};
};

} // namespace etude
