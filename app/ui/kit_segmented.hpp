// SPDX-License-Identifier: GPL-3.0-or-later
// Segmented rows, tab tracks and the sliding pill they share.
#pragma once

#include "face_layout.hpp"
#include "ink_measure.hpp"
#include "kit_core.hpp"
#include "kit_icon.hpp"
#include "kit_state.hpp"

#include <imgui.h>

#include <cstdint>
#include <span>

namespace etude::app::ui {

struct Seg {
    const char* icon = nullptr;  // may be null
    const char* label = nullptr; // may be null when there is an icon
};
// One row: an optional icon and label on the left, the segmented control beside it (or the full
// width when `label` is null). Returns true when the choice changed. A drawn `icon` is expected to
// change with the choice and morphs. Where the control starts is the column's (row_columns.hpp).
// `info` puts an (i) button after the label, as a switch row's, saying what the setting does.
bool segmented(Kit& k, const char* id, Icon icon, const char* label, std::span<const Seg> items,
               int* current, const char* info = nullptr);
// The same with the cells in the bit set `disabled` drawn faint: a tap on one changes nothing and
// sets `*refused` to its index (the caller says why).
bool segmented(Kit& k, const char* id, Icon icon, const char* label, std::span<const Seg> items,
               int* current, std::uint32_t disabled, int* refused, const char* info = nullptr);
// A row of tab cells that share one track, drawn as `segmented` draws its cells: the caller lays the
// cells out side by side (equal or not) with their hit areas' dead gap inside them, calls `tab_track`
// once, then `tab_cell` for each cell and puts the cell's words or icon in the pill it returns.
// The cells have no fill of their own, so the track reads as one smooth bar; the chosen cell is one
// raised pill that slides to the cell tapped, inset evenly (segment_layout.hpp).
// `cells` are the cells in order, `chosen` the index of the selected one (-1 none).
// Returns the cell whose words are bold this frame (-1 none): the one nearest the sliding pill's
// centre, so each word changes face once as the pill passes the middle between two cells.
int tab_track(Kit& k, ImDrawList* dl, const char* id, std::span<const Box> cells, int chosen);
struct TabHit {
    bool tapped = false;
    Box pill;   // the face inside the track: where the cell's icon and words are centred
    State state;
    float weight = 0.0f; // how chosen the cell reads (0..1), fading with the pill's slide
};
// One cell's hit area (the cell less the dead gap), its hover and press, and the audit's face; true
// in `tapped` on a lift inside it, the chosen cell too. `on` is the chosen cell.
TabHit tab_cell(Kit& k, ImDrawList* dl, const char* id, const Box& cell, bool on);
// The chosen pill's slide, shared by every track-and-pill control (`segmented`, `tab_track` and the
// review tabs; the pure steps are in segment_layout.hpp). `key` is the control's ID, `cell` the chosen
// cell, `row` its row; `pill` is where the pill belongs this frame. Returns where to draw it: on a
// change of cell its x and width ease from where it is to the new cell in `tok::kPillSlide`, ending
// exactly on it. No slide on the first draw, for a control not drawn the frame before, with Reduced
// effects, or to another row. The loop keeps drawing while the picture moves and idles once it lands.
Box slide_pill(Kit& k, ImGuiID key, int cell, int row, const Box& pill);
// How chosen cell `cell` of the control `key` reads, 0..1, fading over the same time and curve as
// the pill; `on` is whether it is the chosen cell.
float cell_weight(Kit& k, ImGuiID key, int cell, bool on);
// A cell's words while the pill slides: one copy, in the bold face when `bold` and the regular one
// when not, the ink fading from `ink_off` to `ink_on` with `weight` (a frame never draws two faces
// of one word).
void fade_label(Kit& k, ImDrawList* dl, float size, const Box& slot, LabelAlign align,
                const char* text, bool bold, float weight, ImVec4 ink_off, ImVec4 ink_on);

} // namespace etude::app::ui
