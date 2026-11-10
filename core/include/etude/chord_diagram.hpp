// SPDX-License-Identifier: GPL-3.0-or-later
// The picture the Learn chord card draws: a chord in one octave of keys with its notes marked, each
// note's name and the fingers of both hands, for a root, a quality and an inversion. The notes are
// stacked as close as they go above the bass, so every chord and inversion fits one octave of white
// keys (eight, as the scale card's diagram).
//
// The card names the chord the player chose: the chosen root and quality, with the slash bass the
// inversion puts below it ("Cm7/Eb", "C6/E", "Csus2/G"), spelled from the root's key by the free-play
// label's own spelling (`spell_chord`, `chord_name_parts`) and with each note named as a tone of
// that chord (a diminished seventh on C has its Bbb). Free play names what it hears, in the key: the
// same notes can read as another chord there (C E G A over E is Am7/E), so the two agree on the
// spelling and the style of the name, not always on which chord the notes are. Pure data, so tests
// hold every quality, root and inversion.
#pragma once

#include "etude/chord.hpp"
#include "etude/chord_label.hpp"
#include "etude/scale_diagram.hpp"

#include <array>
#include <vector>

namespace etude {

// The qualities the card offers, in the order of its buttons: the common chords first (the narrow
// card shows the first four), then the rest of the triads, the sevenths and the other families.
// Every `ChordQuality` but None.
inline constexpr std::array<ChordQuality, 23> kChordCardQualities{
    ChordQuality::Major,           ChordQuality::Minor,         ChordQuality::Dominant7,
    ChordQuality::Major7,          ChordQuality::Minor7,        ChordQuality::Diminished,
    ChordQuality::Augmented,       ChordQuality::Sus2,          ChordQuality::Sus4,
    ChordQuality::HalfDiminished7, ChordQuality::Diminished7,   ChordQuality::MinorMajor7,
    ChordQuality::Major6,          ChordQuality::Minor6,        ChordQuality::Add9,
    ChordQuality::Dominant9,       ChordQuality::Major9,        ChordQuality::Minor9,
    ChordQuality::Power,           ChordQuality::Dominant7Sus4, ChordQuality::Dominant7Flat9,
    ChordQuality::Major69,         ChordQuality::Dominant13};

// Whether the quality reads as a minor chord (minor, diminished, the minor and half-diminished and
// diminished sevenths, minor-major seventh, minor sixth and ninth): the key that spells it is the
// root's minor key, else its major key.
bool chord_is_minor_like(ChordQuality quality);

// How many inversions the card shows: the chord's notes in stacked thirds are the root, the third
// (or the suspended note that stands for it), the fifth and the seventh (or the sixth); each of those
// may be the bass (root position, first, second, third inversion). The ninth, eleventh, thirteenth
// or an added note never is, so an add9 has three, a ninth chord four, and a power chord only its
// root position (the free-play label names a fifth only with its root below).
int chord_inversion_count(ChordQuality quality);

// The tone above the root, in semitones, that is the bass of inversion `inversion` (clamped).
int chord_bass_tone(ChordQuality quality, int inversion);

// The notes (MIDI, ascending) of the chord on `root_pc` (0..11, wrapping) in inversion `inversion`
// (clamped to the chord's), the bass in the octave of middle C (C4 .. B4) and the other notes
// stacked above it as close as they go.
std::vector<int> chord_voicing(int root_pc, ChordQuality quality, int inversion);

struct ChordDiagram {
    // The keys from the white key at or below the bass, eight white keys at least: `in_scale` marks
    // a note of the chord and `root` its root; the names and fingers are of the chord's notes.
    ScaleDiagram picture;
    // The chord chosen (its bass the inversion's) and its name as the label writes it; not found
    // for a quality the card does not offer.
    Chord chord;
    ChordNameParts name;
    // The notes lowest first, as `chord_voicing`.
    std::vector<int> notes;
};

ChordDiagram chord_diagram(int root_pc, ChordQuality quality, int inversion);

} // namespace etude
