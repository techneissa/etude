// SPDX-License-Identifier: GPL-3.0-or-later
// What a toast keeps clear of on the practice screen, with no ImGui in it so tests/toast_test.cpp
// checks the real numbers: the section band's height under the bar, and the rects the host hands
// `place_toast` each frame. Sizes in points; `u` is the kit's unit (points per design point).
#pragma once

#include "tokens.hpp"

#include "etude/screen_layout.hpp"
#include "etude/toast.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

// The section band's height under the bar: its touch zone is the whole band (52 pt, 44 pt slim).
inline float band_height_pt(bool slim, float u) { return bar_under_row_pt(true, slim) * u; }

struct ToastRegionsInput {
    float display_w = 0.0f;
    float display_h = 0.0f;
    float row_top = 0.0f; // the top row
    float row_bottom = 0.0f;
    RectF bar;             // the bar's buttons in the top row while it shows, else empty
    bool bar_shown = false; // the bar shows (even part way in): its band counts as the bar's
    bool has_song = false;  // there is a band
    bool slim = false;      // in its slim form
    RectF pinned_band;      // `ScreenLayout::bar_under` in points, empty unless reserved
    RectF lane;             // `ScreenLayout::lane` in points, empty unless the lane is open
    RectF key_label;        // the key label over the notes, in points, empty when not shown
    RectF combo;            // the combo number in the notes' top-right corner, empty when not shown
    RectF sheet_button;     // the sheet-music button in that corner, empty with no song
    std::array<RectF, 2> chords{}; // each hand's chord name and line over the notes, empty if none
    float toast_h = 0.0f;   // the slot's height: the tallest pill, a warning's finger-wide icon
    float gap = 0.0f;       // clear space between the slot and what is above it
    float u = 1.0f;
};

// Fills `r` (which the sheet, editor and popover may already have added to) with the notes and the
// keyboard under the top row, the bar and its band while it shows, the reserved band and the lane,
// and the toast's own slot under all of those (never the top row, which holds the status line).
// False when a rect could not be kept (then `r.invalid`: nothing is placed).
[[nodiscard]] inline bool fill_toast_regions(ToastRegions& r, const ToastRegionsInput& in) {
    bool kept = true;
    r.notes = {0.0f, in.row_bottom, in.display_w, std::max(in.display_h - in.row_bottom, 0.0f)};
    if (in.bar_shown) {
        kept = r.add_avoid(in.bar) && kept;
        if (in.has_song) {
            kept =
                r.add_avoid({0.0f, in.row_bottom, in.display_w, band_height_pt(in.slim, in.u)}) &&
                kept;
        }
    }
    kept = r.add_avoid(in.pinned_band) && kept;
    kept = r.add_avoid(in.lane) && kept;
    kept = r.add_avoid(in.key_label) && kept;
    kept = r.add_avoid(in.combo) && kept;
    kept = r.add_avoid(in.sheet_button) && kept;
    for (const RectF& chord : in.chords) {
        kept = r.add_avoid_beside(chord) && kept; // the title pill slides aside, the name stays
    }
    // The slot: full width, `gap` under the lowest of the top row, the shown bar's band, the
    // reserved band and the lane.
    float top = in.row_bottom;
    if (in.bar_shown && in.has_song) {
        top = std::max(top, in.row_bottom + band_height_pt(in.slim, in.u));
    }
    top = std::max({top, in.pinned_band.bottom(), in.lane.bottom()});
    r.slot = {0.0f, top + in.gap, in.display_w, in.toast_h};
    return kept;
}

} // namespace etude::app::ui
