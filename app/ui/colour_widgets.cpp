// SPDX-License-Identifier: GPL-3.0-or-later
#include "colour_widgets.hpp"

#include "colour_widgets_internal.hpp"

#include "colours_layout.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "preview.hpp"
#include "row_layout.hpp"
#include "settings_catalogue.hpp"

#include "etude/accidental_text.hpp"
#include "etude/palette.hpp"
#include "etude/slider_drag.hpp"
#include "etude/square_drag.hpp"
#include "etude/touch_gesture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace etude::app::ui {

namespace {

using colour_detail::kRingGap;
using colour_detail::kRingWidth;
using colour_detail::kFaceInset;
using colour_detail::kHeadingDrop;
using colour_detail::Picker;
using colour_detail::g_picker;
using colour_detail::g_recent;
using colour_detail::vec;
using colour_detail::cell_span;
using colour_detail::face;
using colour_detail::heading;
using colour_detail::note_name;
using colour_detail::swatch_labels;
using colour_detail::scheme_heading;
using colour_detail::scheme_chips;
using colour_detail::palette_rows;
using colour_detail::draw_swatch;
using colour_detail::swatch_grid;
using colour_detail::picker_block;
using colour_detail::quick_picks_block;
using colour_detail::hit_tiles;
using colour_detail::press_rows;
using colour_detail::screen_rows;
using colour_detail::hold_menu;
using colour_detail::preview;
using colour_detail::gap;

enum class Pane { Colours, Effects };

Pane g_pane = Pane::Colours;

// --- Labels --------------------------------------------------------------------------------------

// --- Scheme chips --------------------------------------------------------------------------------

// --- Palettes ------------------------------------------------------------------------------------

// --- Swatches ------------------------------------------------------------------------------------

// --- The colour control -------------------------------------------------------------------------

// --- Effects -------------------------------------------------------------------------------------

// --- Layout --------------------------------------------------------------------------------------

// A column of blocks at `x` from the page's left, `w` wide and the body's height: its own child
// window, so a row's width is the column's and nothing it draws spills out of it.
template <class Draw>
void column(const char* id, ImVec2 origin, float x, float w, float h, Draw draw) {
    ImGui::SetCursorScreenPos({origin.x + x, origin.y});
    ImGui::BeginChild(id, {w, h}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    row_column_offset(x > 0.0f ? 1 : 0); // the rows of the two columns are checked apart
    draw();
    row_column_offset(0);
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// Whether the colour control is open on a swatch of the current scheme (else it is closed).
bool picking(const LookSettings& look) {
    if (g_picker.swatch >= 0 && g_picker.scheme == look.color_scheme &&
        g_picker.swatch < swatch_count(look.color_scheme)) {
        return true;
    }
    g_picker.swatch = -1;
    return false;
}

// The palettes, or the colour control in their place while a swatch is open.
void palettes_or_picker(Kit& k, LookSettings& look, float width, bool pick) {
    if (pick) {
        picker_block(k, look, width);
    } else {
        palette_rows(k, look, width);
    }
}

void colours_half(Kit& k, LookSettings& look, const ColoursLayout& l, ImVec2 origin, ImVec2 body,
                  float y_shift, bool pick) {
    const ImVec2 at{origin.x, origin.y + y_shift};
    if (l.arrangement == ColoursArrangement::SplitStacked) {
        // The preview across the top, where it fits (while picking it may not), then the blocks.
        const float preview_h = pick ? l.stacked_pick_preview_h : l.stacked_preview_h;
        const float below = preview_h > 0.0f ? preview_h + l.gap : 0.0f;
        const ImVec2 under{at.x, at.y + below};
        if (preview_h > 0.0f) {
            column("##colours-preview", at, 0.0f, body.x, preview_h, [&] {
                preview(k, look, body.x, preview_h);
            });
        }
        if (pick) {
            // The control and the quick picks in two columns, nothing else.
            column("##picker-left", under, 0.0f, l.pick_left_w, body.y - below,
                   [&] { picker_block(k, look, l.pick_left_w); });
            column("##picker-right", under, l.pick_left_w + l.gap, l.pick_right_w, body.y - below,
                   [&] { quick_picks_block(k, look, l.pick_right_w); });
            return;
        }
        const float top = l.chip_h + l.gap + palettes_height(body.x, k.u);
        column("##colours", under, 0.0f, body.x, body.y - below, [&] {
            scheme_chips(k, look, body.x);
            gap(l);
            palette_rows(k, look, body.x);
            ImGui::SetCursorScreenPos({under.x, under.y + top + l.gap});
            swatch_grid(k, look, body.x);
        });
        return;
    }
    column("##colours-left", at, 0.0f, l.col_w, body.y, [&] {
        scheme_chips(k, look, l.col_w);
        gap(l);
        palettes_or_picker(k, look, l.col_w, pick);
    });
    column("##colours-right", at, l.col_w + l.gap, l.col_w, body.y, [&] {
        preview(k, look, l.col_w, l.preview_h);
        gap(l);
        if (pick) {
            quick_picks_block(k, look, l.col_w);
        } else {
            swatch_grid(k, look, l.col_w);
        }
    });
}

void effects_half(Kit& k, LookSettings& look, const ColoursLayout& l, ImVec2 origin, ImVec2 body,
                  float y_shift) {
    const ImVec2 at{origin.x, origin.y + y_shift};
    if (l.arrangement == ColoursArrangement::SplitStacked) {
        // The preview across the top, as on the Colours half; under it the hit tiles and the press
        // rows beside the screen rows.
        const float below = l.stacked_preview_h + l.gap;
        const ImVec2 under{at.x, at.y + below};
        column("##effects-preview", at, 0.0f, body.x, l.stacked_preview_h,
               [&] { preview(k, look, body.x, l.stacked_preview_h); });
        column("##effects-left", under, 0.0f, l.col_w, body.y - below, [&] {
            hit_tiles(k, look, l.col_w);
            gap(l);
            press_rows(k, look);
        });
        column("##effects-right", under, l.col_w + l.gap, l.col_w, body.y - below,
               [&] { screen_rows(k, look); });
        return;
    }
    column("##effects-left", at, 0.0f, l.col_w, body.y, [&] {
        preview(k, look, l.col_w, l.preview_h);
        gap(l);
        hit_tiles(k, look, l.col_w);
        gap(l);
        press_rows(k, look);
    });
    column("##effects-right", at, l.col_w + l.gap, l.col_w, body.y, [&] { screen_rows(k, look); });
}

} // namespace

using namespace colour_detail;

void colours_page(Kit& k, UiModel& model, ImVec2 view) {
    LookSettings& look = model.look;
    g_recent = &model.app.recent_colors;
    const ColoursLayout l = colours_layout(view.x, view.y, k.u);
    // Where even the stacked halves are taller than the view (800x480 at UI scale 1.25 and up), the
    // page is as tall as its content and scrolls with the sheet's page, like the other pages.
    const ImVec2 body{view.x, std::max(view.y, l.height)};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pick = picking(look);
    // What the open hold menu covers is not the audit's: it takes the touches there.
    const bool covered = g_menu.card_max.x > g_menu.card_min.x;
    if (covered) {
        audit::set_overlay(g_menu.card_min, g_menu.card_max);
    }
    if (!l.split()) {
        column("##colours-whole", origin, 0.0f, l.col_w, body.y, [&] {
            scheme_chips(k, look, l.col_w);
            // The swatches stay where they are when the control opens: it opens under them.
            ImGui::SetCursorScreenPos({origin.x, origin.y + whole_swatches_top(l, pick)});
            swatch_grid(k, look, l.col_w);
            gap(l);
            palettes_or_picker(k, look, l.col_w, pick);
        });
        column("##effects-whole", origin, l.col_w + l.gap, l.col_w, body.y, [&] {
            preview(k, look, l.col_w, l.preview_h);
            gap(l);
            if (pick) {
                quick_picks_block(k, look, l.col_w);
            } else {
                hit_tiles(k, look, l.col_w);
                gap(l);
                // The press rows leave the dead gap under the last one: the layout counts that, so
                // nothing else goes between the blocks.
                press_rows(k, look);
                screen_rows(k, look);
            }
        });
    } else {
        // One half at a time. The settings check draws both, the hidden one below the body where
        // the page clips it, so every setting of the page is seen.
        const bool both = catalogue_sweep::recording();
        if (g_pane == Pane::Colours || both) {
            colours_half(k, look, l, origin, body, g_pane == Pane::Colours ? 0.0f : body.y, pick);
        }
        if (g_pane == Pane::Effects || both) {
            effects_half(k, look, l, origin, body, g_pane == Pane::Effects ? 0.0f : body.y);
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(body);
    if (covered) {
        audit::clear_overlay();
    }
    hold_menu(k, look);
}

void colours_header(Kit& k, ImVec2 body) {
    const ColoursLayout l = colours_layout(body.x, body.y, k.u);
    if (!l.split()) {
        return;
    }
    constexpr Seg kPanes[] = {{nullptr, "Colours"}, {nullptr, "Hit effects"}};
    const float lsize = k.s(tok::kLabel);
    const float cell = std::max(k.s(tok::kHit),
                                std::max(text_size(k.regular, lsize, "Colours").x,
                                         text_size(k.regular, lsize, "Hit effects").x) +
                                    k.s(tok::kS5) * 2.0f);
    const float w = cell * 2.0f;
    const ImVec2 win = ImGui::GetWindowPos();
    const float h = ImGui::GetWindowSize().y;
    ImGui::SetCursorScreenPos({win.x + body.x - w, win.y + (h - k.s(tok::kHit)) * 0.5f});
    ImGui::BeginChild("##colours-pane", {w, k.s(tok::kHit)}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    int pane = g_pane == Pane::Colours ? 0 : 1;
    // The two halves' switch, for the audit to tap (the segments have no icon of their own).
    const ImVec2 cell_a{win.x + body.x - w, win.y + (h - k.s(tok::kHit)) * 0.5f};
    audit::note("##pane-colours", cell_a, {cell_a.x + cell, cell_a.y + k.s(tok::kHit)},
                InkKind::Target);
    audit::note("##pane-hit", {cell_a.x + cell, cell_a.y},
                {cell_a.x + cell * 2.0f, cell_a.y + k.s(tok::kHit)}, InkKind::Target);
    if (segmented(k, "pane", nullptr, nullptr, kPanes, &pane)) {
        g_pane = pane == 0 ? Pane::Colours : Pane::Effects;
    }
    ImGui::EndChild();
}

} // namespace etude::app::ui
