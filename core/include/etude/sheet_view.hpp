// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/screen_layout.hpp"
#include "etude/session_state.hpp"

#include <optional>
#include <string_view>

namespace etude {

// The view after `mode`: falling notes, sheet music, both, and round again.
ViewMode next_view_mode(ViewMode mode);
// "Falling notes", "Sheet music", "Sheet music and falling notes".
const char* view_mode_label(ViewMode mode);
// "falling", "sheet" or "both" (the settings file's words); nothing for any other text.
std::optional<ViewMode> parse_view_mode(std::string_view text);

// Where sheet music goes in the note area and how big it is drawn. Pure geometry in pixels; the
// keyboard is never touched (everything stays inside the note area).
//
// - Sheet: the whole note area is the score; the falling notes are not drawn. One or two systems
//   (lines of music) show: the cursor's system on top and the next one below it, as a preview.
//   Two show when both fit at the smallest legible staff size, else one.
// - Both: a strip at the top of the note area holds the cursor's system; the falling notes take
//   the rest. The strip is as tall as one system at the staff size the width asks for, never more
//   than `kSheetStripMaxShare` of the note area. It plans no room for note names: they fit round
//   the system as they can (core sheet_names.hpp).
// - Falling, or no song: no score.

// The smallest staff space drawn (between two lines of a staff), and the largest, in mm on the
// screen. A system across the width is planned at `kSheetWidthSpaces` staff spaces, so the width
// sets the size until the height or these limits do.
inline constexpr float kSheetMinSpaceMm = 1.6f;
inline constexpr float kSheetMaxSpaceMm = 5.0f;
inline constexpr float kSheetWidthSpaces = 110.0f;
// A system's planned height in staff spaces, with room for ledger lines and marks: two staves
// (both hands) or one.
inline constexpr float kGrandStaffSpaces = 24.0f;
inline constexpr float kSingleStaffSpaces = 13.0f;
// The paper's margin round the music, and the gap between the paper and the note area's edge, in
// staff spaces.
inline constexpr float kSheetMarginSpaces = 1.5f;
inline constexpr float kSheetGapSpaces = 1.0f;
// Both: the strip's largest share of the note area.
inline constexpr float kSheetStripMaxShare = 0.5f;
// With note names on the sheet, each system is planned this many staff spaces taller above and
// below (the lane's start past the cursor and the ledger lines, stems and marks sticking out of
// the staves under a stack: a low chord's ledger lines and fermata need this), and taller again by
// its tallest stacks of names (SheetNameRoom): the music is drawn smaller to make the room, so the
// names keep their size.
inline constexpr float kSheetNameLaneSpaces = 4.5f;

// The names' room: the most names stacked above and below (core sheet_name_stacks; 0 and 0: no
// names) and a name's line height: `line_spaces` staff spaces, never under `line_px` pixels nor
// over `line_max_px` (0: no most).
struct SheetNameRoom {
    int above = 0;
    int below = 0;
    float line_px = 0.0f;
    float line_spaces = 0.0f;
    float line_max_px = 0.0f;
};
// A slot is at most this many times its planned system height (the paper ends under the last).
inline constexpr float kSheetSlotSlack = 1.2f;
// How long the page turn slides (the next system moving up to the top), in seconds.
inline constexpr float kSheetTurnS = 0.28f;

// The three buttons beside the paper's top right: the sheet-music button (Off, On, Full) and the
// two reading switches (note names, note colours). One rule places them in every view and at every
// size, never in an L:
//
// - The button sits at the top right of `area`, `corner_px` square (its touch target),
//   `kSheetColumnInsetShare` of that in from the right and the top, whatever the view.
// - Where the room beside the paper is tall enough for three cells, they stand in one column under
//   one another: the button, names, colours.
// - Where it is not (the Both strip), they stand in one row leftwards from the button's spot:
//   names, colours, the button.
// - A cell is `corner_px` wide and tall and cells touch: a control's hit area is its cell less the
//   dead gap (`tok::kHitGap`), centred in it, so the dead gaps between neighbours are all equal.
struct SheetSideButtons {
    RectF view;    // the sheet-music button's cell
    RectF names;   // the note names switch's cell
    RectF colours; // the note colours switch's cell
    RectF toggles; // the two switches' cells together (their bounding box)
    RectF block;   // all three cells together
    bool column = false; // one column (true) or one row
};

struct SheetPlan {
    bool shown = false;
    RectF sheet;          // the score's part of the note area (empty when not shown)
    RectF notes;          // what is left for the falling notes (empty in Sheet)
    RectF paper;          // the paper card inside `sheet`
    float staff_space_px = 0.0f;
    float system_width_px = 0.0f; // the width a system is engraved at
    int systems = 0;              // shown at once: 1 or 2
    float slot_height_px = 0.0f;  // each system's slot on the paper, top to bottom
    RectF corner;                 // the two toggles' cells together (side.toggles), or empty
    SheetSideButtons side;        // the three buttons beside the paper's top right (or empty)
};

// The sheet-music button (Off, On, Full): the first of the three side buttons, at the top right of
// `area` (the score's room: the note area and the chord name's strip), `corner_px` square, inset
// `kSheetColumnInsetShare` of that from the right and the top. It stands there in every view, the
// falling notes included, where no paper or column is drawn. Empty for no area or no size.
inline constexpr float kSheetColumnInsetShare = 0.2f;
RectF sheet_view_button(const RectF& area, float corner_px);

// Where the three side buttons stand (see SheetSideButtons): `area` as for sheet_view_button,
// `bottom` the lowest y they may reach (the sheet's bottom edge; the area's own when no sheet is
// planned). A column when three cells fit between the button's top and `bottom`, else a row. Empty
// for no area or no size.
SheetSideButtons sheet_side_buttons(const RectF& area, float corner_px, float bottom);

// `note_area` is where the score may go: ScreenLayout::note_area with the chord name's strip above
// it (score_area); `two_staves` when the song has notes for both hands (a grand staff);
// `px_per_mm` the screen's density (a bad value counts as 96 dpi). `corner_px` is the side of
// one corner toggle (its touch target): room for the three side buttons is kept beside the paper's
// top right (`SheetPlan::side`, sheet_side_buttons: a column, else a row) and the paper is narrower
// by that much, so no button ever lies on the music, its names or the cursor. `names` is the room
// note names on the sheet take in the Sheet view (none by default; Both ignores it).
SheetPlan plan_sheet(const RectF& note_area, ViewMode mode, bool has_song, bool two_staves,
                     float px_per_mm, float corner_px = 0.0f, const SheetNameRoom& names = {});

// The score's room: the note area and the chord name's strip just above it (the score shows the
// harmony, so the chord name gives way to it). The falling notes in Both keep to the note area.
RectF score_area(const ScreenLayout& layout);

// The page turn: the top slot's system index, as a fraction while it slides. From `from` to `to`
// over kSheetTurnS (`elapsed_s` since it began), eased out; a jump of more than one system, or
// `reduced` effects, goes at once.
float sheet_turn_at(float from, float to, float elapsed_s, bool reduced);

} // namespace etude
