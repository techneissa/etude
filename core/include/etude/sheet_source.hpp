// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "etude/abc.hpp"
#include "etude/song.hpp"

namespace etude {

// What the sheet-music view engraves for a song: one MusicXML text with the practised parts only,
// laid out as the score is written (repeats as signs, not expanded), whose bars and offsets are the
// ones the song's loader gave its notes, so the engraved page lines up with the song.
//
// - MusicXML (partwise, plain or .mxl): the file itself, with the parts that are not practised left
//   out (accompaniment instruments beside a keyboard part, the rules of instrument.hpp, and parts
//   with no pitched note) and part names hidden.
// - Timewise MusicXML, MuseScore (.mscz, .mscx) and ABC: written from the score the loader read
//   (`write`, below), so the bars and offsets are the loader's own.
// - MIDI: written from the song when its practised notes lie on a clean grid (midi_grid_clean);
//   otherwise no sheet, and the reason says why.

enum class SheetSourceStatus : std::uint8_t {
    Ok,
    MidiOffGrid, // a MIDI song whose notes are not on a clean beat grid
    Unreadable,  // the file did not load or holds no practised notes
};

struct SheetSource {
    SheetSourceStatus status = SheetSourceStatus::Unreadable;
    std::string musicxml;
    std::string reason; // English, for the switch's explanation when not Ok
};

// For a score file's bytes (MusicXML, .mxl, .mscz, .mscx or ABC, told by content). `abc` must be
// the options the song was loaded with (the tune, chords as a left hand).
SheetSource sheet_source_for_score(std::span<const std::uint8_t> bytes, const AbcOptions& abc = {});

// For a MIDI song (hands as the app assigned them: Left on the bass staff, the rest on the treble).
SheetSource sheet_source_for_midi(const Song& song);

// The grid a MIDI song must sit on to be written as a score: every practised note starts within
// one tick of a 32nd note or of a sixteenth-note triplet (in its bar), and the song has bars.
struct MidiGrid {
    bool clean = false;
    int notes = 0;    // practised notes
    int on_grid = 0;  // of those, the ones on the grid
};
MidiGrid midi_grid(const Song& song);

} // namespace etude
