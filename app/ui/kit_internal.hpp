// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of kit.cpp share. Not part of the app's interface.
#pragma once

#include "kit.hpp"

#include "clef_icon.hpp"
#include "font_ink.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "pressed_fade.hpp"
#include "row_layout.hpp"
#include "sections_port.hpp"
#include "etude/accidental_text.hpp"
#include "etude/note_style.hpp"
#include "transport.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace etude::app::ui {

// A row whose control goes under its label keeps the label in a line this tall (design points).
inline constexpr float kStackedLabelH = 26.0f;
// The same for a row whose label has an (i) button: its hit square (`kHit`, reaching up from the
// face) is a line of its own, and a dead gap below it keeps it clear of the track (design points).
inline constexpr float kStackedInfoLabelH = tok::kCell;

// The middle of a stacked row's label line that starts at `top`. A line with an (i) puts its words
// and the (i)'s face at the foot, a dead gap and half the face above the line's end, so the words
// stay close to the track under them; the (i)'s hit square reaches up from its face to fill the line.
inline float stacked_label_cy(const Kit& k, float top, bool info) {
    if (!info) {
        return top + k.s(kStackedLabelH) * 0.5f;
    }
    return top + k.s(kStackedInfoLabelH) - k.s(tok::kHitGap) - k.s(tok::kChip) * 0.5f;
}

inline ImVec2 row_start(float* w) {
    *w = ImGui::GetContentRegionAvail().x;
    return ImGui::GetCursorScreenPos();
}

// Ends a row whose parts were placed by hand: one item covering it, so the cursor moves on.
inline void end_row(ImVec2 pos, ImVec2 size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(size);
}

// The width a row's label may take from `x` to a control starting at `control_x`: what the column's
// label side was measured to need, which the label itself fills exactly, so a pixel of slack keeps
// rounding from cutting it.
inline float label_room(const Kit& k, float control_x, float x) {
    return control_x - k.s(kZoneGap) - x + 1.0f;
}

// A row's label in `room` points: whole on one line, else broken at a space into two lines that
// share the row's height (a row is `tok::kHit` tall, two lines of the body size fit), the first as
// long as the room allows. A label no break can fit (a word longer than the room) is cut with an
// ellipsis as `fit` does, which the audit reports in Settings: rows give such a label more room.
struct RowLabel {
    FitText first;
    FitText second; // empty for a label of one line
    float width = 0.0f; // the wider line's width
    bool two() const { return !second.empty(); }
};
RowLabel row_label(ImFont* font, float size, std::string_view label, float room);
// Draws `label` left-aligned in `slot`, the row's height: one line as `label_at` places it, two as
// a block centred on the slot (`label_stack`).
void draw_row_label(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
                    ImVec4 colour, const RowLabel& label);

// The info button sits `kInfoOffset` right of where its row's label ends, its hit square (`kHit`)
// centred there; a row that has one reserves `kInfoReserve` for it, so the control after it starts a
// dead gap past the button's hit area.
inline constexpr float kInfoOffset = 16.0f;
inline constexpr float kInfoReserve = tok::kHit;
// `hit_up` moves the hit square up from the face (a stacked row's, whose line has room above only).
bool info_button(Kit& k, const char* text, ImVec2 centre, const char* icon_glyph = nullptr,
                 const ImVec4* ink = nullptr, float glyph_size = 0.0f, float hit_up = 0.0f);

// A slider's or a stepper's reset button (reset_to.hpp) for the row `frame` plans, centred on the
// control's line `cy`: its hit area is the plan's `reset_span`, its face a `tok::kChip` square with
// the reset glyph. True when it was tapped. The row always draws it and reports it
// (`row_report_reset`), so its column keeps the gutter: `active` (the value is not the default) makes
// it a pressable item with a face; greyed it is only the dim glyph, no item, so a tap does nothing
// and the touch audit counts no target, while the gutter keeps its 12 mm and 2 mm.
bool reset_button(Kit& k, const RowFrame& frame, float cy, bool active, const char* label);

// The tap area of a settings row. The whole row, or with an info button in it (`info`, its hit
// square) two parts either side of the button and a dead gap clear of it, both of which press the
// row: a row's area never reaches over the button. `state` is the row's: the hover and press of
// whichever part the pointer is on. The first part is named `id` (`id` + "-r" for the second).
struct RowTap {
    bool pressed = false;
    State state;
};
RowTap row_tap(Kit& k, const char* id, const Box& row, const Box* info);

// A compact control at a row's right edge: tells the column probe where it went.
void note_compact(const Kit& k, ImVec2 pos, float w, float natural_w);
// A switch's track and knob, with its top left at `a`, easing to `on` by `id`. `width` and `height`
// (points; 0 for the token's) size a switch the page makes bigger than a row's.
void switch_knob(Kit& k, ImDrawList* dl, ImVec2 a, bool on, ImGuiID id, float width = 0.0f,
                 float height = 0.0f);

inline float chevron_x(const Kit& k, ImVec2 pos, ImVec2 size) {
    return pos.x + size.x - k.s(tok::kS3) - k.s(tok::kIconSm) * 0.5f;
}

} // namespace etude::app::ui
