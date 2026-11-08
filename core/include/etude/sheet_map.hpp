// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "etude/screen_layout.hpp"
#include "etude/song.hpp"

namespace etude {

// Sheet music: what an engraver drew, as plain values, and how it lines up with the song. The
// engraver lays the written score out in systems (lines of music); this module never sees its
// types. It matches the song's notes to the marks on the page by what is written (the written
// bar, the offset in that bar and the pitch), builds the cursor's path in the order the song
// plays (repeats included: a repeat is the same marks reached again), and says what each mark
// shows at a position. The song's clock stays the only authority for time; the engraver's own
// timing is never used.

enum class SheetMarkKind : std::uint8_t {
    Note, // a note head (with its own stem and flag when it is not in a chord)
    Rest,
    Stem, // the stem or flag of a chord: it follows the whole chord
};

// One thing drawn on the page that can change colour or place the cursor.
struct SheetMark {
    SheetMarkKind kind = SheetMarkKind::Note;
    int system = 0;      // index into SheetLayout::systems
    RectF box;           // in the system's picture, pixels
    float head_x = 0.0f; // the note head's (or rest's) centre, in the system's picture
    int bar = 0;         // the written bar, 0-based in the score's order
    double offset_q = 0.0; // quarter notes from the start of the written bar
    int pitch = -1;        // MIDI key, notes only
    int chord = -1;        // shared by a chord's notes and its stem; -1 for none
    bool tied = false;     // a tie ends on this head: it continues the note before it
    int staff = 0;         // the staff in its system, 0 for the top one
    RectF head;            // notes: the note head alone, in the system's picture
};

// A written bar's place: its system and its left and right edge in that system's picture.
struct SheetBar {
    int bar = 0;
    int system = 0;
    float x0 = 0.0f;
    float x1 = 0.0f;
};

// One system's picture.
struct SheetSystem {
    float width = 0.0f;
    float height = 0.0f;
    // The top line of the first staff and the bottom line of the last, in the picture; the cursor
    // spans them.
    float staff_top = 0.0f;
    float staff_bottom = 0.0f;
    int staves = 1;
};

// Something drawn that a note name must stay clear of (a head, a stem, a beam, a ledger line, a
// slur, a dynamic, words), in its system's picture.
struct SheetObstacle {
    int system = 0;
    RectF box;
};

struct SheetLayout {
    std::vector<SheetSystem> systems;
    std::vector<SheetMark> marks;
    std::vector<SheetBar> bars;
    std::vector<SheetObstacle> obstacles; // by system, in order
};

// Sheet music is offered for a song only when at least this share of its practised notes found
// their mark on the page: a song that does not line up would colour the wrong notes.
inline constexpr double kSheetMinMatchShare = 0.98;

// Where the cursor is at a tick: one point of its path.
struct SheetCursorPoint {
    double tick = 0.0;
    int system = 0;
    float x = 0.0f;
};

struct SheetMap {
    // Song note -> its marks (a tied note has a mark for each written head), and mark -> the song
    // notes it shows, in the order they play (a mark inside a repeat shows one note per pass).
    // Compressed rows: the marks of note i are note_marks[note_first[i] .. note_first[i + 1]).
    std::vector<std::uint32_t> note_first;
    std::vector<std::uint32_t> note_marks;
    std::vector<std::uint32_t> mark_first;
    std::vector<std::uint32_t> mark_notes;
    // The note marks of each chord, the same way (index: SheetMark::chord).
    std::vector<std::uint32_t> chord_first;
    std::vector<std::uint32_t> chord_marks;
    // The cursor's path in the order the song plays, by tick (equal ticks keep their order: at a
    // jump the later point wins).
    std::vector<SheetCursorPoint> path;
    // Per played bar (index into Song::measures): its start tick and its reading run. A run is a
    // stretch of played bars read forward through the page; a repeat or a jump back starts the
    // next one.
    std::vector<std::int64_t> bar_start;
    std::vector<int> bar_run;
    std::vector<int> bar_written; // the written bar it reads (SheetBar::bar), per played bar
    int practised = 0; // the song's practised notes (not drums, not accompaniment)
    int matched = 0;   // of those, the ones with a mark

    bool usable() const {
        return practised > 0 &&
               static_cast<double>(matched) >= kSheetMinMatchShare * static_cast<double>(practised);
    }
};

// Matches `song` to `layout`. A practised note matches the note marks of the same written bar
// (Note::written_measure, counted from the song's first bar: 0 for a pickup, else 1), offset in
// that bar (to 1/960 of a quarter, within one tick of the song, the nearest first) and pitch. A tie's
// end left over that sits where a matched note of its pitch is still sounding (in the same played
// bar or a later one) shows that note too. Notes of other parts (drums, accompaniment) are left out.
//
// The path: for each played bar, in order, a point at every onset of its written bar (notes and
// rests, at the median head x of what starts there, on the system of the first), then one at the
// bar's end at its right edge. A written bar with no marks gets its edges only; one the engraver
// did not draw gets nothing (the cursor holds).
SheetMap build_sheet_map(const SheetLayout& layout, const Song& song);

struct SheetCursor {
    int system = -1; // -1: no path
    float x = 0.0f;
};

// The cursor at `tick`: before the first point, at it; inside the path, between the point at or
// before `tick` and the next one when both are on one system and the next is not to its left
// (straight in proportion to the ticks); otherwise held at the point until the next one's tick, so
// a repeat or a new system is a jump. Wait Mode holds the song on a chord's tick, so the cursor
// stands on the chord.
SheetCursor sheet_cursor_at(const SheetMap& map, double tick);

// What a mark shows at `tick` (Rest marks show nothing):
enum class SheetMarkState : std::uint8_t {
    Ahead,  // still to play in this reading of the page: the score's ink
    Played, // its note was hit
    Missed, // its note was missed: a soft mark
};

// The song note whose result a note mark shows at `tick`, or -1 when it shows Ahead: -1 when one
// of its notes is still to come (start after `tick`) in the same reading run as the bar at `tick`,
// else its latest note that started at or before `tick` (-1 when none has).
int sheet_mark_note(const SheetMap& map, const Song& song, std::uint32_t mark, double tick);

// The state for a note's grade (SongController::grade_of: -1 for none yet, else a NoteGrade).
SheetMarkState sheet_state_for_grade(int grade);

// A chord's stem follows its `notes` note marks, `ahead` of them Ahead and `missed` Missed: Ahead
// while any is Ahead (or it has none), Missed when all are Missed, else Played.
SheetMarkState sheet_stem_state(int notes, int ahead, int missed);

} // namespace etude
