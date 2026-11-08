// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/chord.hpp"
#include "etude/sheet_map.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace etude {

// Note names on the sheet: where each name of a system goes. Pure geometry in the system's
// picture pixels; the caller measures the names and draws them.
//
// - The names of the notes that start together on a staff make a column, stacked with the highest
//   pitch on top. With chord symbols on, a column that is a recognised chord shows one symbol
//   instead (sheet_name_columns).
// - The top staff's names go above the system, the other staves' below it, in a lane outside the
//   outer staff lines that the cursor and the loop bracket never reach, and further out past
//   anything that sticks out there (a head, a stem, a beam, a ledger line, a slur, a dynamic,
//   words) or a column placed before it.
// - A column that does not fit in the slot straight up (or down) is laid on a gradual slope: each
//   name a step sideways and less than a line out from the one before. Then the same a step
//   smaller, never under `min_size`. Then, at `min_size`, the names nearest the staff give way to
//   a small "…" mark, the outermost ones (a chord's top note above, its bass below) kept longest.
//   A column with no room even for the mark is dropped, the last resort.

struct SheetNameStyle {
    float size = 0.0f;      // a name's height (its line)
    float min_size = 0.0f;  // the smallest a name may step down to (0: never smaller)
    float gap = 0.0f;       // between a name and anything else
    float line_gap = 0.0f;  // between the names of a straight column
    float clear = 0.0f;     // the lane starts this far outside the outer staff lines
    float more_unit_width = 0.75f;  // the "…" mark's width at a size of 1
    float more_unit_height = 0.15f; // and its height (three dots on one line)
    // Names stay between these, in the picture (the system's slot on the paper): no name reaches
    // into the next system's.
    float top_limit = -1e9f;
    float bottom_limit = 1e9f;
};

// How far out each name of a slope is from the one before, in lines of its size: the gentler
// first. The step sideways clears the name before it by `kSheetNameSlopeGap` of a line, so the
// names never read as one word.
inline constexpr float kSheetNameSlopeRises[] = {0.6f, 0.4f};
inline constexpr float kSheetNameSlopeGap = 0.3f;
// A "…" with no room over its column may sit up to this many of its own widths to either side.
inline constexpr int kSheetNameMoreSideSteps = 3;

enum class SheetNameKind : std::uint8_t {
    Note,   // a note's name (`mark`)
    Symbol, // a chord symbol (`symbol`, the caller's; `mark` one of the chord's notes)
    More,   // "…": names left out here
};

struct SheetName {
    SheetNameKind kind = SheetNameKind::Note;
    std::uint32_t mark = 0;   // its note mark (for More, the column's first)
    std::int32_t symbol = -1; // Symbol: the caller's symbol id
    RectF box;                // in the system's picture
    float size = 0.0f;        // its text size (the box's height)
};

struct SheetNames {
    float size = 0.0f; // the names' full size (0: no names)
    int staggered = 0; // names moved out past a column placed before
    int sloped = 0;    // names laid on a slope
    int smaller = 0;   // names stepped smaller
    int symbols = 0;   // chord symbols
    int left_out = 0;  // names left out where a "…" shows
    int marks = 0;     // "…" marks
    int dropped = 0;   // names left out with no mark (no room even for it)
    std::vector<SheetName> names;
};

// One column: the notes starting together on one staff side.
struct SheetNameColumn {
    bool above = true;
    float x = 0.0f;                    // centre of its heads
    std::vector<std::uint32_t> marks;  // highest pitch first
    Chord chord;                       // found: shown as one chord symbol
    std::int32_t symbol = -1;          // the caller's id for the chord's symbol
    float symbol_unit_width = 0.0f;    // its width at a size of 1 (0: names instead)
};

// The columns of `system`'s note marks, left to right. `unit_width[m]` is the width of mark m's
// name at a size of 1; a mark with a width of 0 or less, or that is not a note, gets no name. With
// `chord_symbols`, a column of three or more pitch classes that detect_chord() recognises (not a
// bare fifth) gets its `chord`; the caller sets its symbol and width. `out` is refilled.
void sheet_name_columns(const SheetLayout& layout, int system, std::span<const float> unit_width,
                        bool chord_symbols, std::vector<SheetNameColumn>& out);

// A column read as a chord symbol: three or more pitch classes, recognised, not a bare fifth.
Chord sheet_column_chord(std::span<const int> pitches);

// Places `columns` (sheet_name_columns, in that order) of `system`. `out.names` is refilled.
void place_sheet_names(const SheetLayout& layout, int system, std::span<const SheetNameColumn> columns,
                       std::span<const float> unit_width, const SheetNameStyle& style, SheetNames& out);

// The most names stacked on one side of a staff in the song (its practised notes), above and
// below: with two staves the right hand's go above and the left hand's below; with one, all above.
// With `chord_symbols` a recognised chord counts as one. The name lanes are planned for these
// (SheetNameRoom).
struct SheetNameStacks {
    int above = 0;
    int below = 0;
};
SheetNameStacks sheet_name_stacks(const Song& song, bool two_staves, bool chord_symbols = false);

} // namespace etude
