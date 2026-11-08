// SPDX-License-Identifier: GPL-3.0-or-later
#include "colour_widgets.hpp"

#include "colour_widgets_internal.hpp"

#include "colours_layout.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "preview.hpp"
#include "row_layout.hpp"
#include "settings_catalogue.hpp"
#include "tap.hpp"

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
using colour_detail::palette_base_name;
using colour_detail::active_palette_name;
using colour_detail::HoldMenu;
using colour_detail::follow_hold_menu_cell;
using colour_detail::open_hold_menu;
using colour_detail::press_rows;
using colour_detail::screen_rows;
using colour_detail::preview;
using colour_detail::gap;

// Sizes in design points.
constexpr float kSwatchInset = tok::kS1 * 1.5f; // a swatch's circle inside its cell

constexpr float kSwatchMaxR = tok::kS4;         // the largest swatch circle's radius

constexpr float kNamedSwatchR = tok::kS3;       // a swatch with its name beside it

void open_picker(const LookSettings& look, int swatch) {
    g_picker.swatch = swatch;
    g_picker.scheme = look.color_scheme;
    g_picker.pick = pick_of(swatch_color(look, look.color_scheme, swatch));
    g_picker.changed = false;
}

// The heading row: what the swatches are, and Reset back to the palette (one tap; a quiet button
// while the colours are the palette's).
void swatch_heading(Kit& k, LookSettings& look, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = k.s(tok::kHit);
    const ActivePalette active = active_palette(look, look.color_scheme);

    const float lsize = k.s(tok::kLabel);
    const float isize = k.s(tok::kIconSm);
    const char* label = "Reset";
    const ImVec2 ls = text_size(k.regular, lsize, label);
    const float bw = std::max(h, k.s(tok::kS3) * 2.0f + isize + k.s(tok::kS2) + ls.x);
    const ImVec2 ba{pos.x + width - bw, pos.y};
    ImGui::SetCursorScreenPos(ba);
    const bool pressed = ImGui::InvisibleButton("##reset-palette", {bw, h});
    const bool enabled = active.custom();
    // Pressable either way (a tap says why it did nothing), so it always has its face; the idle
    // fill is for when it does something.
    face(k, dl, "##reset-palette", {ba.x, ba.y + k.s(kFaceInset)},
         {ba.x + bw, ba.y + h - k.s(kFaceInset)}, ba, {ba.x + bw, ba.y + h}, false,
         enabled ? Idle::Fill : Idle::None);
    const ImVec4 ink = enabled ? col::kText : with_alpha(col::kText3, 0.6f);
    const float ix = ba.x + k.s(tok::kS3) + isize * 0.5f;
    audit::note("##reset-palette", {ix - isize * 0.5f, pos.y + (h - isize) * 0.5f},
                {ix + isize * 0.5f, pos.y + (h + isize) * 0.5f});
    if (!audit::icons_hidden()) {
        glyph(k, dl, icon::kReset, {ix, pos.y + h * 0.5f}, isize, ink);
    }
    label_at(k, dl, k.regular, lsize,
             {ix + isize * 0.5f + k.s(tok::kS2), pos.y, ba.x + bw, pos.y + h}, LabelAlign::Left,
             ink, label);
    if (tapped(pressed)) {
        if (enabled) {
            const std::string base = palette_base_name(look, look.color_scheme);
            reset_to_palette(look);
            k.say("Back to " + base);
        } else {
            k.say("These are the " + active_palette_name(active) + " colours");
        }
    }

    // The heading, kept clear of the button.
    const char* hint = g_picker.swatch >= 0 ? nullptr : "tap one to change it";
    const float size = k.s(tok::kCaption);
    const char* title = scheme_heading(look.color_scheme);
    const ImVec2 ts = text_size(k.bold, size, title);
    label_at(k, dl, k.bold, size, {pos.x + k.s(tok::kS1), pos.y, ba.x, pos.y + h}, LabelAlign::Left,
             col::kText3, title);
    if (hint != nullptr) {
        const float x = pos.x + k.s(tok::kS1) + ts.x + k.s(tok::kS2);
        const FitText shown = fit(k.regular, size, hint, std::max(0.0f, ba.x - x - k.s(tok::kS2)));
        label_at(k, dl, k.regular, size, {x, pos.y, ba.x, pos.y + h}, LabelAlign::Left, col::kText3,
                 shown.c_str());
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, h + k.s(tok::kHitGap)}); // and the dead gap under the button
}

} // namespace

namespace colour_detail {

void swatch_grid(Kit& k, LookSettings& look, float width) {
    catalogue_sweep::drawn(Setting::SchemeColours);
    const ColorScheme scheme = look.color_scheme;
    if (g_picker.swatch >= 0 && g_picker.scheme != scheme) {
        g_picker.swatch = -1;
    }
    swatch_heading(k, look, width);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const int n = swatch_count(scheme);
    const float hit = k.s(tok::kHit);
    // A few swatches get wide cells with their name beside them; many get square cells, the
    // label inside, in balanced rows.
    const bool named = n <= 3;
    int per_row = named ? n : std::min(n, swatches_per_row(width, k.u));
    const int rows = (n + per_row - 1) / per_row;
    per_row = (n + rows - 1) / rows;
    // A cell is its hit area and the dead gap; the last one's gap is outside the width.
    const float dead = k.s(tok::kHitGap);
    const float pitch_x = (width + dead) / static_cast<float>(per_row);
    const float cw = pitch_x - dead;
    const float pitch_y = hit + dead;
    std::array<char, 16> inside{};
    std::array<char, 32> name{};
    for (int i = 0; i < n; ++i) {
        const ImVec2 a{pos.x + pitch_x * static_cast<float>(i % per_row),
                       pos.y + pitch_y * static_cast<float>(i / per_row)};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##swatch", {cw, hit});
        const Press press = press_of(pressed);
        const ImVec2 cell_end{a.x + cw, a.y + hit};
        audit::note("##swatch", a, cell_end, InkKind::Target); // a target the audit can tap
        follow_hold_menu_cell(HoldMenu::Kind::Swatch, i, a, cell_end);
        face(k, dl, "##swatch", a, cell_end, a, cell_end, false, Idle::None);
        ImGui::PopID();
        swatch_labels(look, scheme, i, inside, name);
        const float r = std::min(std::min(cw, hit) * 0.5f - k.s(kSwatchInset), k.s(kSwatchMaxR));
        const bool chosen = g_picker.swatch == i;
        const RGBA c = swatch_color(look, scheme, i);
        if (named) {
            // A smaller circle and, where the name is too long for the cell, smaller text.
            const float nr = std::min(r, k.s(kNamedSwatchR));
            const ImVec2 centre{a.x + k.s(tok::kS1) + k.s(kRingGap) + k.s(kRingWidth) + nr,
                                a.y + hit * 0.5f};
            draw_swatch(k, dl, centre, nr, c, inside.data(), chosen);
            const float x = centre.x + nr + k.s(kRingGap) + k.s(kRingWidth) + k.s(tok::kS1);
            const float room = std::max(0.0f, a.x + cw - x - k.s(tok::kS1));
            const float size = text_size(k.regular, k.s(tok::kLabel), name.data()).x <= room
                                   ? k.s(tok::kLabel)
                                   : k.s(tok::kCaption);
            const FitText shown = fit(k.regular, size, name.data(), room);
            label_at(k, dl, k.regular, size, {x, a.y, a.x + cw, a.y + hit}, LabelAlign::Left,
                     chosen ? col::kText : col::kText2, shown.c_str());
        } else {
            draw_swatch(k, dl, {a.x + cw * 0.5f, a.y + hit * 0.5f}, r, c, inside.data(), chosen);
        }
        if (press.tap) {
            if (chosen) {
                g_picker.swatch = -1;
            } else {
                open_picker(look, i);
            }
        } else if (press.hold) {
            open_hold_menu(HoldMenu::Kind::Swatch, i, a, cell_end);
        }
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, pitch_y * static_cast<float>(rows) - dead});
}

} // namespace colour_detail

} // namespace etude::app::ui
