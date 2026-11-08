// SPDX-License-Identifier: GPL-3.0-or-later
// The words on the scale card's type buttons, for the fourteen types in `kAllScaleTypes` order: the
// full names in the two rows of chips (`scale_type_name`), the short ones (`kLearnTypeBrief`) in the
// strip that scrolls sideways.
#pragma once

#include "etude/chord_diagram.hpp"
#include "etude/scales.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace etude::app::ui {

inline constexpr std::array<const char*, 14> kLearnTypeBrief{
    "Major",      "Minor",   "Harmonic",    "Melodic",     "Dorian", "Phrygian",  "Lydian",
    "Mixolydian", "Locrian", "Pent. major", "Pent. minor", "Blues",  "Chromatic", "Whole tone"};

// The words on the chord card's quality buttons, in `kChordCardQualities` order: the suffix as the
// free-play label writes it after the root ("m7♭5"), and the triads (Major, Minor, Diminished,
// Augmented, Sus2, Sus4) as words: a short word ending in a g ("Aug") reads as off centre to the
// icon audit, which takes the baseline from the ink's lowest row, and the g's hook counts as one.
inline constexpr int kLearnChordCount = 23;
inline constexpr std::array<const char*, 23> kLearnChordWords{"Major",
                                                              "Minor",
                                                              "7",
                                                              "maj7",
                                                              "m7",
                                                              "Diminished",
                                                              "Augmented",
                                                              "Sus2",
                                                              "Sus4",
                                                              "m7\xE2\x99\xAD"
                                                              "5",
                                                              "dim7",
                                                              "m(maj7)",
                                                              "6",
                                                              "m6",
                                                              "add9",
                                                              "9",
                                                              "maj9",
                                                              "m9",
                                                              "5",
                                                              "7sus4",
                                                              "7\xE2\x99\xAD"
                                                              "9",
                                                              "6/9",
                                                              "13"};

// Names for the audit, two digits each so none is the start of another.
inline constexpr std::array<const char*, 23> kLearnChordIds{
    "##learn-chord-00", "##learn-chord-01", "##learn-chord-02", "##learn-chord-03",
    "##learn-chord-04", "##learn-chord-05", "##learn-chord-06", "##learn-chord-07",
    "##learn-chord-08", "##learn-chord-09", "##learn-chord-10", "##learn-chord-11",
    "##learn-chord-12", "##learn-chord-13", "##learn-chord-14", "##learn-chord-15",
    "##learn-chord-16", "##learn-chord-17", "##learn-chord-18", "##learn-chord-19",
    "##learn-chord-20", "##learn-chord-21", "##learn-chord-22"};

// The place of `quality` in `kChordCardQualities` (0, Major, for a quality the card does not
// offer).
inline int learn_chord_index(ChordQuality quality) {
    for (std::size_t i = 0; i < kChordCardQualities.size(); ++i) {
        if (kChordCardQualities[i] == quality) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

// The word under the root in the key ring's middle on the chord card, from the chord's own label
// (core `ChordNameParts`, the free-play label's source, so the two never disagree): "major" and
// "minor" for the triads whose label has no quality or just "m", else the label's quality as
// written ("7", "maj7", "m7b5"; the screen draws a 'b' as a flat).
inline std::string learn_chord_centre_word(const ChordNameParts& name) {
    if (name.quality.empty()) {
        return "major";
    }
    return name.quality == "m" ? "minor" : name.quality;
}

// The full name of the type at place `i` of the lists (`kAllScaleTypes[i]`).
inline const char* learn_type_words(std::size_t i) { return scale_type_name(kAllScaleTypes[i]); }

} // namespace etude::app::ui
