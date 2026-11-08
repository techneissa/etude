// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/chord.hpp"
#include "etude/chord_spelling.hpp"
#include "etude/hand.hpp"
#include "etude/harmony.hpp"
#include "etude/key_signature.hpp"
#include "etude/song.hpp"
#include "etude/transpose.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace etude {

// The chord label of the scene: one name for each hand that plays a full chord, made from that
// hand's own notes (harmony.hpp, HarmonyScope::Left and Right), centred over a thin line under the
// keys of the hand's chord notes. Pure; the timelines are built once per song, hands and
// transposition on a worker, never per frame.

// A chord's name as the pieces the scene draws: the root with its sign ("F#", "Bb"), the quality
// as written ("maj7", "m7b5", "7b9", "6/9"), the slash bass ("E#"; empty when it is the root). The
// signs are '#' and 'b' here (a 'b' or '#' in a quality is always a sign); the screen turns them
// into drawn glyphs, and `display` into the real characters.
struct ChordNameParts {
    std::string root;
    std::string quality;
    std::string bass;

    bool empty() const { return root.empty(); }
    // The name for text: "F#maj7/E#".
    std::string plain() const { return root + quality + (bass.empty() ? "" : "/" + bass); }
    // The name for the screen, "F♯maj7/E♯" (accidental_text.hpp).
    std::string display() const;

    friend bool operator==(const ChordNameParts&, const ChordNameParts&) = default;
};

ChordNameParts chord_name_parts(const Chord& chord, const ChordSpelling& spelling);

// What one hand's label shows from `start_s` to `end_s` (song seconds, exclusive end).
struct HandChordLabel {
    double start_s = 0.0;
    double end_s = 0.0;
    ChordNameParts name;
    // The lowest and highest pitch (MIDI, as the file has it: the keys it shows are
    // `chord_label_keys`) of the hand's chord notes in the stretch, the tune left out: the span the
    // label's line covers; -1 for none.
    int low_pitch = -1;
    int high_pitch = -1;

    friend bool operator==(const HandChordLabel&, const HandChordLabel&) = default;
};

// A hand's labels in time order; between two labels the hand has no chord and shows nothing.
using HandChordTimeline = std::vector<HandChordLabel>;

struct HandChords {
    HandChordTimeline left;
    HandChordTimeline right;

    const HandChordTimeline& of(Hand hand) const { return hand == Hand::Left ? left : right; }
    friend bool operator==(const HandChords&, const HandChords&) = default;
};

// A hand's stretches as labels, named `semitones` higher (the song's transposition) and spelled
// from the song's key at that point, played that high too (`transposed_key`, so the names follow
// the key the player sees; `prefer_flats` settles a key with as many flats as sharps); the pitches
// of the span stay the file's. A stretch that names no chord (NoChord) shows nothing; an unclear
// one (a rest, a lone note) keeps the label before it only up to that chord's `hold_end_s`, the end
// of its bar or a beat past it, then the label goes; stretches whose label is the same, the same
// keys included, are one label.
HandChordTimeline hand_chord_timeline(const std::vector<HarmonyStretch>& stretches,
                                      const std::vector<KeyChange>& keys, int semitones,
                                      bool prefer_flats);

// Both hands from a harmony timeline already built (a change of transposition keeps the harmony and
// makes only the labels again).
HandChords hand_chords_of(const HarmonyTimeline& harmony, const std::vector<KeyChange>& keys,
                          int semitones, bool prefer_flats);

// Both hands of a song, from its notes' hands: build_hand_harmony, then the labels.
HandChords build_hand_chords(const Song& song, int semitones, bool prefer_flats);

// The keys the label's line spans, on the keyboard the song is played on: the label's lowest and
// highest pitch as the transposition shows them (shifted, and folded by whole octaves onto the
// keyboard like the blocks), the lower first, then cut to the keys in view
// [first_visible, last_visible]. None when the label has no keys, a pitch lands on no key, or the
// span lies wholly outside the view.
struct ChordKeys {
    int low = 0;
    int high = 0;
    friend bool operator==(const ChordKeys&, const ChordKeys&) = default;
};
std::optional<ChordKeys> chord_label_keys(const HandChordLabel& label, const Transposition& shown,
                                          int first_visible, int last_visible);

// The label holding `seconds`: its index, -1 when the hand shows none there. O(log n).
int chord_label_at(const HandChordTimeline& timeline, double seconds);

} // namespace etude
