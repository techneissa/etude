// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "etude/look.hpp"
#include "etude/session_state.hpp"

namespace etude {

// Axis-aligned rectangle in window pixels, y down. `w` and `h` are never negative.
struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool empty() const { return !(w > 0.0f) || !(h > 0.0f); }
};

// Layout rules. All numbers are in pixels unless a name says mm.
inline constexpr float kMinTouchTargetMm = 15.0f;  // the top row's height (a layout choice; hit areas are 12 mm)
// A top-row button's hit area as a share of the row: 12.2 mm of the 15, so two buttons side by side
// are 2.8 mm apart (the share holds as the row grows with the window, as every control does).
inline constexpr float kTopRowHitShare = 12.2f / kMinTouchTargetMm;
inline constexpr float kDefaultPxPerMm = 96.0f / 25.4f; // used when the caller passes a bad value
inline constexpr float kMinNoteAreaShare = 0.40f;       // of the window height, always
inline constexpr float kMaxTopRowShare = 0.30f;         // the top row never takes more than this
inline constexpr float kMaxChordLabelShare = 0.12f;
inline constexpr float kTopRowMarginShare = 0.2f; // of the row height: left margin, gaps
// A row narrower than this many design points has one menu button in place of the library's and
// Learn's buttons and the profile chip: the bar's least form needs the room, and a control never
// shrinks below 12 mm (the 7 inch 800x480 panel is 584 points wide). `compute_layout` is the one
// place that decides: `ScreenLayout::menu` is set on such a row, and the other three are not.
inline constexpr float kMenuBelowRowPt = 640.0f;
inline constexpr float kMinChordFontPx = 14.0f;   // smaller than this, the chord label hides
inline constexpr float kMinLabelFontPx = 9.0f;    // smaller than this, a note label hides
inline constexpr float kLineHeightEm = 1.3f;      // chord label strip height / font size
inline constexpr float kTopRowMinHeightEm = 1.5f; // the top row is at least this many fonts tall
inline constexpr float kLabelCharWidthEm = 0.62f; // widest average advance, for fitting text
inline constexpr float kLabelPaddingPx = 2.0f;    // each side of a label inside its cell
inline constexpr int kChordLabelMaxChars = 14;    // longest chord text the label must fit
inline constexpr float kMinKeyBedPx = 3.0f;            // the key bed is never thinner than this
// The smallest note name that stays legible, at 96 dpi; `min_name_font_px` scales it to the
// screen. A name that would need a smaller font loses its octave number first.
inline constexpr float kMinNameFontPx = 9.0f;
// The smallest size a black key's name may shrink to (letter with its small accidental), at 96
// dpi: the accidental is drawn at 60% of this, so the letter stays the legible part.
// `min_small_name_font_px` scales it to the screen.
inline constexpr float kMinSmallNameFontPx = 7.0f;

// The UI's one scale. Every size of the touch UI is in design points; a design point is
// `ui_scale(...)` pixels:
//
//   ui_scale = max(kUiMinScale x pixel_density,
//                  density x clamp(min(W / kUiDesignWidthPt, H / kUiDesignHeightPt), 1, kUiMaxGrow))
//
// where density is `px_per_mm / kDefaultPxPerMm` (1 on a 96 dpi screen; the app's --ui-scale
// multiplies it, never below 1: `PanelScale::px_per_mm` clamps it, so a control keeps its 12 mm)
// and W x H is the window in points (its pixels over the density). A window up to
// 1366x768 points draws at the density alone (800x480 too, so a 44 pt hit area keeps its physical
// size), a bigger one grows with it (1.41 at 1920x1080), up to 1.6 (2186x1229 and above). The top
// row, the status line, the pinned bar's band and every control use it. The floor `kUiMinScale`
// keeps a control legible on a very sparse display (under 72 dpi) or a small density a caller
// passes. It is in the toolkit's
// points, so it is the same size on every display: `pixel_density` is the pixels per point (2 on a
// retina window, 1 elsewhere), and the floor is `kUiMinScale x pixel_density` pixels. It lives
// here, so the layout in pixels and the kit drawing in points use one value.
inline constexpr float kUiDesignWidthPt = 1366.0f;
inline constexpr float kUiDesignHeightPt = 768.0f;
inline constexpr float kUiMaxGrow = 1.6f;
inline constexpr float kUiMinScale = 0.75f;
// The status line's size in design points: the UI's body text.
inline constexpr float kStatusFontPt = 16.0f;
// How much the window grows the UI: clamp(min(W / 1366, H / 768), 1, 1.6) for a window
// `width_px` x `height_px` at `px_per_mm`. A bad value counts as 96 dpi or a design-size window.
float ui_window_grow(float width_px, float height_px, float px_per_mm);
// Pixels per design point: the density times `ui_window_grow`, never below `kUiMinScale` points
// (`kUiMinScale x pixel_density` pixels; a bad `pixel_density` counts as 1). `grow`, when given
// (a positive value, clamped to 1..kUiMaxGrow), stands in for `ui_window_grow`: the growth a
// window kept while it is being resized (`UiGrowHold`, ui_grow_hold.hpp).
float ui_scale(float width_px, float height_px, float px_per_mm, float pixel_density = 1.0f,
               float grow = 0.0f);

// The control bar kept on screen ("Keep the bar shown"), and the section band under it. With a
// song loaded the bar has a band under its buttons: the song's sections, its tools and the playhead
// (the section band). While the bar is pinned the layout gives the band a row of its own
// (`ScreenLayout::bar_under`), so the notes start below it. The band expands on demand into the
// lane (`lane`): a mini piano roll of the song under the band, where cuts are edited; while it is
// open the layout reserves the band and the lane whether the bar is pinned or not.
struct PinnedBar {
    bool pinned = false;
    BarStyle style = BarStyle::Strip;
    bool has_song = false; // the band exists only with a song loaded
    bool lane = false;     // the band is expanded into the lane
};

// The section band's height in design points (a point is `kDefaultPxPerMm` pixels per mm, so on a
// denser display it is more pixels), in its full form and in its slim form (a touch target of 12 mm,
// 46 points, and 4 more: the bar's buttons above it keep their dead gap) that a short or very dense
// window gets, see `BarFit`. 0 without a song. The same in every bar style: the band is a row of its
// own, not part of the bar's buttons.
inline constexpr float kSectionBandPt = 52.0f;
inline constexpr float kSlimSectionBandPt = 50.0f;
float bar_under_row_pt(bool has_song, bool slim = false);

// The lane under the band: its preferred height and the least it is opened at, in design points.
// Its rows (bar numbers, the notes, the section names with the handles' feet) are laid out by the
// section editor (`editor_lane_layout` in editor_layout.hpp).
inline constexpr float kLanePt = 128.0f;
inline constexpr float kLaneMinPt = 112.0f; // two tools a hit tall, a dead gap between and one above

// How the pinned bar fits a window. The band under the top row is never cut: if the band, a
// keyboard of at least `kMinPinnedKeyboardShare` of the height it would have without the bar, and
// the note area's `kMinNoteAreaShare` do not all fit, the band takes its slim form; if they
// still do not, the bar is not pinned at that size (`Fallback`: the layout is the unpinned one, and
// the bar hides after a while as usual).
inline constexpr float kMinPinnedKeyboardShare = 0.40f;
enum class BarFit : std::uint8_t {
    NotPinned, // not asked for
    Full,      // pinned, the band in its full form (also pinned with no song, which has none)
    Slim,      // pinned, the band in its slim form
    Fallback,  // asked for, but it does not fit: laid out unpinned
};

// How the lane fits. It takes the full band and `kLanePt` when the band, the lane and a keyboard of
// at least `kMinPinnedKeyboardShare` of its height fit beside the note area's share; else what is
// left, down to `kLaneMinPt` (`Short`); below that it does not open (`NoRoom`: the band is laid out
// as if the lane was never asked for, and the full section editor is the place to cut).
enum class LaneFit : std::uint8_t {
    Closed, // not asked for, or no song
    Full,
    Short,
    NoRoom,
};

// Where everything goes in one window. Rects are whole pixels, stacked top to bottom in this
// order, full window width, touching but never overlapping, and together exactly as tall as the
// window: top row, section band, lane, chord label, note area, keyboard. The keyboard ends at the
// bottom edge of the window and nothing is below it. A rect that is not shown is empty (zero
// height) at its place in the stack, and the room goes to the note area.
//
// The top row is one touch target (15 mm) tall, grown with the window like the UI
// (`ui_window_grow`), and always there: the status text (keyboard name,
// messages) on the left and the settings button (`gear`, a square) at the right end. The control
// bar has no row of its own: when revealed, its buttons take the right end of the status area
// (see split_top_row), so the status text shrinks and the stack below never moves. While the bar is
// pinned and a song is loaded, the overview under the buttons has a band of its own, `bar_under`,
// so the bar never covers a note; otherwise that rect is empty.
//
// Priority when space runs short: the note area keeps at least `kMinNoteAreaShare` of the window
// height; after that the top row, the section band and the lane, the keyboard and the chord label,
// in that order, take what is left. Text shrinks with its rect and hides (flag false, font 0)
// rather than overflow.
struct ScreenLayout {
    RectF top_row;
    RectF bar_under; // the section band's row, empty unless pinned (or the lane open) with a song
    BarFit bar_fit = BarFit::NotPinned;
    RectF lane; // the lane under the band, empty unless it is open
    LaneFit lane_fit = LaneFit::Closed;
    RectF chord_label;
    RectF note_area;
    RectF keyboard;

    // Inside `top_row`: the settings button's square at its right end, and the status text area
    // from a left margin to a gap before the gear. Never overlapping; empty when there is no room.
    // `library` is the song library's button, a square at the row's left end over the start of the
    // status area: the status text is drawn after it, and the bar, which leaves the start of the
    // status area to the text (split_top_row), never reaches it. Empty when the row is too narrow
    // for it and the gear.
    // `learn` is the Learn screen's button, a square beside it, to its right; empty when the row has
    // no room for both buttons and the gear. The status text is drawn after the last button there is.
    // `menu` is the one button of a row narrower than `kMenuBelowRowPt` design points, in the
    // library's place: it opens a popover with the library, Learn and the profile, so `library` and
    // `learn` are empty there (and the host draws no profile chip). It is empty on a wider row.
    RectF gear;
    RectF library;
    RectF learn;
    RectF menu;
    RectF status_text;

    // The keyboard rect split in two: the key bed (the fallboard strip the notes land on) on top
    // and the key area below it. They touch, never overlap, and together are exactly `keyboard`.
    // Keys are drawn only inside `key_area`, so nothing crosses them; both are empty when the
    // keyboard is. The hit line, where notes meet the keyboard, is the top of `key_bed`.
    RectF key_bed;
    RectF key_area;
    float felt_line_h = 0.0f;  // thin felt line along the bottom edge of the bed, in pixels
    float key_shadow_h = 0.0f; // shadow the bed casts onto the top of the keys, in pixels

    bool chord_label_visible = false;
    bool status_visible = false; // the status font is readable in the top row

    // Font sizes in pixels, 0 when the text is hidden. The note label sizes are ceilings: a
    // block or key narrower than its label needs shrinks it further or hides it, see
    // `fit_label_font_px`.
    float chord_label_font_px = 0.0f;
    float status_font_px = 0.0f;
    float note_label_font_px = 0.0f; // labels on falling blocks
    float key_label_font_px = 0.0f;  // labels on keys
    // False when names are off, switched off for blocks/keys (`LookSettings::shows_block_names()`
    // and `shows_key_names()`), or the font would be unreadable.
    bool block_labels_visible = false;
    bool key_labels_visible = false;
    // C markers (`LookSettings::c_markers`) have their own switch and ceiling, the same size as the
    // key labels; false and 0 when off or the font would be unreadable.
    bool c_markers_visible = false;
    float c_marker_font_px = 0.0f;
};

// `px_per_mm` is the screen's pixel density (3.78 for a 96 dpi display) and sets the 15 mm
// touch target. A bad value (zero, negative, NaN) falls back to `kDefaultPxPerMm`. A bad window
// size gives all-empty rects. `look` is clamped first, so out-of-range values are safe.
// A pinned bar takes `bar_under_row_pt` below the top row, after the top row and before the
// keyboard, whole or not at all (see `BarFit`): `bar_fit` says which, and a caller draws the
// band slim for `Slim` and treats the bar as unpinned for `Fallback`. An open lane takes the full
// band and the lane, pinned or not (see `LaneFit`).
// `grow` is `ui_scale`'s.
ScreenLayout compute_layout(float width_px, float height_px, float px_per_mm,
                            const LookSettings& look, const PinnedBar& bar = {},
                            float pixel_density = 1.0f, float grow = 0.0f);

// The top row while the control bar is revealed: the bar and what is left for the status text.
struct TopRowSplit {
    RectF status_text; // empty when the bar left no room
    RectF bar;         // empty when `bar_w` is not positive
};

// Places a bar `bar_w` pixels wide in `layout.status_text`: against its right end (next to the
// gear), or centred in the window when `centred`. Either way it moves right as far as needed to
// leave the status text `keep_status_w` pixels, and never past the status area's right end, so it
// never reaches the gear. A bar wider than the status area is cut to it. The status text keeps
// what is left of the bar, less a gap, so the two never overlap. Bad values count as 0.
TopRowSplit split_top_row(const ScreenLayout& layout, float bar_w, float keep_status_w,
                          bool centred);

// The widest bar split_top_row can place without cutting it, while the status text keeps
// `keep_status_w` pixels: the status area less what the text keeps and the gap to the bar. The whole
// area when nothing is kept; 0 for no status area or a bad value.
float bar_room(const ScreenLayout& layout, float keep_status_w);

// Font size for a label of `char_count` characters inside a block or key `cell_width_px` wide:
// `max_font_px` when it fits, smaller when it has to shrink, 0 (hide it) when the result would
// fall under `kMinLabelFontPx`. Always a whole number of pixels.
float fit_label_font_px(float max_font_px, float cell_width_px, int char_count);

// `kMinNameFontPx` on a screen of `px_per_mm` pixels per millimetre (96 dpi is 3.78): the same
// physical size on a denser display. A bad value counts as 96 dpi.
float min_name_font_px(float px_per_mm);

// `kMinSmallNameFontPx` on a screen of `px_per_mm` pixels per millimetre, like `min_name_font_px`.
float min_small_name_font_px(float px_per_mm);


// The control bar's forms, richest first. Every bar style holds the same controls in the same
// order and gives them up in the same order as the room shrinks: the look shortcuts first (they
// open their settings pages), then the metronome toggle (which moves into the tempo chip's
// popover), then the pin (its switch is in Settings > View), then the replay and song-sound
// buttons, then the skip buttons, then the Flow/Wait chip (a row of the tempo chip's popover, and
// the Q key). The sheet view has no button on the bar: it is the side button right of the score,
// Settings > View's Show row and the I key. Play, the hand toggles and the tempo chip are on the
// bar in every form.
enum class BarForm : std::uint8_t {
    Full,    // every control: the note style and colours shortcuts, the metronome toggle
    Compact, // the colours shortcut goes
    Tight,   // no look shortcut; the Flow/Wait chip shows its icon alone
    Minimal, // no metronome toggle, a narrow tempo chip; the pin goes when even this is too wide
    Lean,    // no pin, no replay, no song sound
    Bare,    // no skip buttons either: play, the hands, the tempo chip and the Flow/Wait chip
    Least,   // no Flow/Wait chip either (its row is in the tempo popover): play, the hands, the
             // tempo chip
};

// What a bar of each form needs, in the units the caller uses (all share them): the whole bar's
// width. `minimal` is the minimal form with its pin, `minimal_no_pin` without it; `lean`, `bare`
// and `least` never have a pin. A width that is 0 is a form the caller does not offer.
struct BarFormWidths {
    float full = 0.0f;
    float compact = 0.0f;
    float tight = 0.0f;
    float minimal = 0.0f;
    float minimal_no_pin = 0.0f;
    float lean = 0.0f;
    float bare = 0.0f;
    float least = 0.0f;
};

// The richest form that fits. The full form needs `comfy_room_w`, the room that leaves the status
// text its comfortable share of the row; the narrower forms only need `room_w` (at least as
// wide), which leaves the status text its least, so the status text is never cut below that while
// a narrower form remains: the least important control goes first. The minimal form fits with or
// without its pin. Where none fits, the narrowest form offered, cut to the room (split_top_row). A
// form fits at exactly its room. Bad values (NaN, a negative room) choose the minimal form.
BarForm fit_bar_form(float comfy_room_w, float room_w, const BarFormWidths& widths);

// Whether the bar keeps its pin: in every form down to the minimal one, until even the minimal
// form with its pin is wider than the room; never in the lean, bare and least forms. Then the pin
// leaves the bar whole (the setting stays in Settings > View) and the rest is drawn whole, never
// with a control cut or shrunk.
bool bar_keeps_pin(BarForm form, const BarFormWidths& widths, float room_w);

// The bar has its own metronome toggle in this form (else the toggle is in the tempo popover).
constexpr bool bar_has_metronome_toggle(BarForm form) {
    return form == BarForm::Full || form == BarForm::Compact || form == BarForm::Tight;
}
// The tempo chip has room for its speed badge (the narrow chip leaves it to the popover).
constexpr bool bar_chip_has_badge(BarForm form) { return bar_has_metronome_toggle(form); }
// The replay and song-sound buttons are on the bar in this form.
constexpr bool bar_has_replay(BarForm form) {
    return form != BarForm::Lean && form != BarForm::Bare && form != BarForm::Least;
}
// The Flow/Wait chip is on the bar in this form (else its row is in the tempo popover).
constexpr bool bar_has_mode(BarForm form) { return form != BarForm::Least; }
// The skip buttons are on the bar in this form.
constexpr bool bar_has_skips(BarForm form) {
    return form != BarForm::Bare && form != BarForm::Least;
}
// How many look shortcuts the form keeps: note style, then colours.
constexpr int bar_look_shortcuts(BarForm form) {
    return form == BarForm::Full ? 2 : (form == BarForm::Compact ? 1 : 0);
}

} // namespace etude
