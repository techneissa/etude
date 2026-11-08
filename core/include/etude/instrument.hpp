// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/song.hpp"

#include <string_view>

namespace etude {

// What kind of instrument a part is, for the question "is this a practice hand?". A song with a
// keyboard part keeps only keyboard parts (and parts of unknown kind) as hands; the other parts
// are accompaniment (see hand_split.hpp).
enum class InstrumentKind : std::uint8_t {
    Unknown,  // nothing says what it is
    Keyboard, // piano, organ, harpsichord, keyboard, ...
    Other,    // voice, strings, winds, guitar, ...
};

// General MIDI programs (0-based) that are keyboards: 0..8 the pianos, harpsichord, clavinet and
// celesta, and 16..20 the organs. The accordions (21, 23) and the harmonica (22) are not.
inline constexpr int kKeyboardProgramRanges[][2] = {{0, 8}, {16, 20}};

// Names are matched as whole words, case-insensitively, against two tables (instrument.cpp):
// keyboard instruments and other instruments (voice, vocals, soprano, violin, cello, flute, sax,
// guitar, strings, choir, accordion, ...). "Chords" is not "choir". A role word (melody, chords,
// bass, lead, right, left, rh, lh, treble, accomp, track N) is in neither table and says nothing
// about the instrument.
//
// The order of the checks in classify_instrument():
// 1. each of `instrument_id`, `instrument_name` and `name`: a clear other-instrument word makes the
//    part Other, whatever its program (a score file names its instrument on purpose, and a
//    "Voice" part that carries program 0 is still a voice);
// 2. the General MIDI program, when the part has one: kKeyboardProgramRanges is Keyboard, any
//    other program Other. So a piano named "Chords" or "Bass" is a keyboard, and a synth lead
//    named "Melody" (program 80) is not;
// 3. a keyboard word in the same three texts, for a part with no program ("Klavier");
// 4. otherwise Unknown, so an unnamed two-track song keeps both tracks as hands.
InstrumentKind instrument_of_text(std::string_view text);
InstrumentKind classify_instrument(const Part& part);

// True for a part that sings: a voice word in its names (voice, vocals, choir, soprano, ...), or,
// when no name says another instrument, a General MIDI voice program (52 choir aahs, 53 voice
// oohs, 54 synth voice). The accompaniment button shows a voice glyph when every accompaniment
// part is one.
bool is_voice_part(const Part& part);

// True when some playable part is a keyboard.
bool has_keyboard_part(const Song& song);

// True for a part that is not a practice hand: it is Other, the song has a keyboard part, and it
// is not a drum part. Without a keyboard part nothing is accompaniment.
bool is_accompaniment_part(const Song& song, const Part& part);
// The same, with the answer of has_keyboard_part(song) given, for a loop over many parts (asking
// the song for every part would search its parts once per part).
bool is_accompaniment_part(const Part& part, bool song_has_keyboard);

} // namespace etude
