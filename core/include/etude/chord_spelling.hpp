// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/chord.hpp"
#include "etude/key_signature.hpp"

#include <string>

namespace etude {

// Spelling a chord's root and bass the way a musician writes them: F#maj7/E#, not F#maj7/F. Pure.
//
// A note's spelling is its place on the line of fifths: C is 0, G 1, D 2, A 3, E 4, B 5, F# 6,
// and F is -1, Bb -2, Eb -3. Each step along the line is seven semitones, so a pitch class has one
// place on the line for each octave of it: Bb is -2, A# is 10.

// The pitch class (0..11, C = 0) of a place on the line of fifths.
int line_pitch_class(int line);

// "C", "F#", "Bb", "E#", and for the rare double accidental "F##" or "Bbb". The sign is the
// internal "#" or "b"; the display turns it into a glyph.
std::string line_name(int line);

// Where the root and the bass of a chord stand. `bass_line` equals `root_line` when the bass is
// the root.
struct ChordSpelling {
    int root_line = 0;
    int bass_line = 0;

    friend bool operator==(const ChordSpelling&, const ChordSpelling&) = default;
};

// The places on the line of fifths of `chord`'s root and bass. The root takes the place nearest
// the key's own notes (in D major F# is F#, in F major Bb is Bb) that keeps the chord's tones
// out of double accidentals; the bass, when it is a tone of the chord, is spelled from the root
// by its interval (the seventh of F# major seventh is E#, the third of E7 is G#); a bass that is
// not a chord tone takes the place nearest the key. A null `key` is C major.
ChordSpelling spell_chord(const Chord& chord, const KeyChange* key);

// The name of the chord's tone `semitones` above its root ("E", "G#", "Bb": the sign is '#' or 'b'),
// spelled from the root's place by the interval (the seventh of F# major seventh is E#, the third of
// E7 is G#); empty when the chord has no tone there or `chord.found` is false.
std::string chord_tone_name(const Chord& chord, const ChordSpelling& spelling, int semitones);

// A diminished seventh or an augmented chord reads the same from each of its tones; this picks
// the tone whose spelling sits nearest the key as the root (E in C major for C# E G Bb), keeping the
// bass. Any other chord is returned as it is.
Chord respell_symmetric(const Chord& chord, const KeyChange* key);

} // namespace etude
