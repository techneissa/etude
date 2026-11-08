// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/panel_size.hpp"
#include "etude/roll_view.hpp"
#include "etude/screen_layout.hpp"
#include "etude/sections.hpp"

#include <cstddef>
#include <cstdint>

namespace etude {

// Where the parts of the section editor go, and what a touch on it grabs. All sizes are in the
// screen's own units (ImGui points); `u` is how many of them make one design point, so a denser
// screen gets larger controls. The regions tile the editor's area from top to bottom and never
// overlap, from 800 x 480 up to the widest screens.

inline constexpr float kEditorHit = kMinHitPt; // smallest touch target (12 mm), design points
inline constexpr float kEditorCaptionExtra = 18.0f; // an action button's width past it, for its caption
inline constexpr float kEditorToolbar = kMinHitPt;
inline constexpr float kEditorRuler = 18.0f;
inline constexpr float kEditorBands = kMinHitPt; // the section names, tappable
inline constexpr float kEditorLoupe = 76.0f;
inline constexpr float kEditorLoupeMin = 52.0f;
// The bar's three cut buttons carry a caption under a face (the caption line is 13 pt): the face is
// what is left of the bar, 30 pt in 52 (it was 26 in 48, and the icon is 20).
inline constexpr float kEditorActions = 52.0f;
inline constexpr float kEditorGap = kMinHitGapPt; // between rows: the dead gap between hit areas
inline constexpr float kEditorCloseGap = 6.0f; // extra room between the tools and the close button
inline constexpr float kEditorMinRoll = 72.0f; // the note area, design points
// The room between a destructive action (Delete) and every other action beside it, design points:
// a finger that misses by half a button still lands on nothing. The section screens all keep it.
inline constexpr float kDestructiveGap = 24.0f;
// Half the width of the invisible grab zone around a handle or the cursor, design points (about
// 15 mm across at 96 dpi).
inline constexpr float kEditorGrabReach = 28.0f;

struct EditorLayout {
    RectF area;
    RectF toolbar;
    RectF ruler;   // bar numbers
    RectF roll;    // the note thumbnail; section bands tint it, the cursor crosses it
    RectF bands;   // section names and the boundary handles' feet
    RectF loupe;   // the magnified notes around the cursor or the dragged handle
    RectF actions;
    bool fits = true; // false when `area` was too small to keep every minimum

    // The width the ruler, roll and bands share: the time axis runs across it.
    float axis_x0() const { return roll.x; }
    float axis_x1() const { return roll.right(); }
};

// Lays the editor out inside `area`. The note area takes what the other rows leave; the loupe
// shrinks to kEditorLoupeMin before the note area drops under kEditorMinRoll.
EditorLayout editor_layout(const RectF& area, float u);

// ---- The lane: the editor's roll docked under the section band

// The lane is the editor without its toolbar, loupe and action bar: a grid of four tools at its
// left (split at the cursor, play across it, undo, the whole song) and the time axis (bar numbers,
// the notes, the section names with the handles' feet) in the rest. The same touches as the
// editor's strip (editor_hit) work on it. `area` is the lane's rect less its margins; the roll gets
// what the ruler and the band row leave.
inline constexpr float kLaneRuler = 14.0f;
inline constexpr float kLaneGap = kMinHitGapPt;

struct LaneLayout {
    EditorLayout strip; // toolbar, loupe and actions empty
    RectF split;
    RectF across;
    RectF undo;
    RectF fit;
};
LaneLayout lane_layout(const RectF& area, float u);

// The toolbar's buttons, left to right: a title, fit-to-song, zoom out, zoom in, undo, the
// suggestions switch and close. Close is in the top right corner, where the settings sheet's close
// (the gear turned into a cross) is, so every surface closes in the same place. Each button is at
// least kEditorHit wide.
struct ToolbarSlots {
    RectF close;
    RectF title; // may be empty when the toolbar is narrow
    RectF fit;
    RectF zoom_out;
    RectF zoom_in;
    RectF undo;
    RectF suggest;
};
ToolbarSlots toolbar_slots(const RectF& toolbar, float u);

// The action bar: split at the cursor, play (the selected section, else after the cursor), play
// across the cut, then the selected band's own bar (its name, rename, merge, delete). One Play. One
// caption rule for the row: every button carries a caption under its icon when the bar has room for
// all six captioned and the band's name, else none does (a long press names each). Delete keeps
// `kDestructiveGap` from Merge.
struct ActionSlots {
    bool captions = false;
    RectF split;
    RectF play;
    RectF play_across;
    RectF band_chip; // the selected band's name and colour
    RectF band_rename;
    RectF band_merge;
    RectF band_delete;
};
ActionSlots action_slots(const RectF& actions, float u);

// ---- Touches

enum class EditorTarget : std::uint8_t {
    None,
    Cursor,   // the split cursor's line
    Handle,   // a boundary between two sections
    BandName, // the name of a section in the band row: rename
    Band,     // a section's band outside its name: select
    Roll,     // the notes or the ruler outside any band: place the cursor, pan
};

struct EditorHit {
    EditorTarget target = EditorTarget::None;
    std::size_t handle = 0;         // Handle: index into SongSections::markers()
    SectionId section = kNoSection; // BandName, Band, and Roll inside a band
};

// What lies under (x, y). A handle or the cursor whose grab zone holds the touch wins, the nearer
// one when both do (a handle on a tie), then a band name, a band, the roll. `view` and the layout
// give the time axis; the grab zone around the cursor and handles is `reach` wide on each side
// (kEditorGrabReach makes it about 15 mm across, though they are drawn thin).
// `name_w[i]` is the width of band i's name pill, 0 for no pill (the band is too narrow).
// `handle_margin`: a handle no farther than this from the cursor line keeps a touch that holds both
// even when the cursor is nearer (the caller passes the touch slop), since a hold on a cursor cuts
// the section and a hold on a handle only takes it.
EditorHit editor_hit(const EditorLayout& layout, const RollView& view, const SongSections& sections,
                     double cursor_s, bool cursor_visible, float x, float y, float reach,
                     const float* name_w, std::size_t name_count, float handle_margin = 0.0f);

// The x of a song time on the editor's axis, and back.
float editor_x(const EditorLayout& layout, const RollView& view, double seconds);
double editor_time(const EditorLayout& layout, const RollView& view, float x);

// Where a band's name pill starts: just inside its left edge (clear of the boundary handle's
// reach), or the axis start when the band begins off screen.
float band_name_left(const EditorLayout& layout, const RollView& view, double band_start_s,
                     float reach);

// ---- The position readout and the ruler

// The readout pill ("Bar 1 · beat 1") of the cursor or a dragged handle: `text_w` x `text_h` of text
// plus `pad_x` and `pad_y` on each side, centred on `centre_x` (clamped inside [min_x, max_x]) and
// on `centre_y`. The lane's roll can be shorter than the pill, so the pill may stand over the ruler.
RectF readout_pill_rect(float centre_x, float centre_y, float min_x, float max_x, float text_w,
                        float text_h, float pad_x, float pad_y);

// Whether a ruler number in `label` must be left out: it touches the readout `pill` (grown by
// `margin` on every side). The pill is the newer, bigger thing the touch is about, so the number
// gives way and no two labels ever overlap, at any size. An empty pill hides nothing.
bool ruler_label_hidden(const RectF& label, const RectF& pill, float margin);

// ---- The magnifier under a finger

// Where the magnifier floats while a handle or the cursor is dragged: centred over the touch, its
// bottom `lift` above it so the finger and the hand never cover what it shows, kept inside `area`
// with `margin` to spare. With no room above (the touch is near the top) it goes below, and with
// none there either it sits at the top edge.
RectF loupe_bubble_rect(float touch_x, float touch_y, const RectF& area, float width, float height,
                        float lift, float margin);

// What a loupe (the panel under the roll, or the magnifier above a finger) is made of: a header
// band that holds the "before the cut" and "after" labels and the scissors, and below it the note
// area, clipped so no note is ever drawn through the text. The two never intersect. `inset` is the
// margin all round; the header is `header_h` tall but never more than 40% of what is inside the
// inset, so the notes always keep room; `mid_x` is the cut's x, in the middle of both.
struct LoupeRegions {
    RectF header;
    RectF notes;
    float mid_x = 0.0f;
};
LoupeRegions loupe_regions(const RectF& rect, float inset, float header_h);

} // namespace etude
