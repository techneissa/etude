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
using colour_detail::preview;
using colour_detail::gap;

constexpr float kStroke = 1.5f;                 // a picture tile's lines

constexpr float kTileCaption = tok::kCaption + tok::kS2; // the band under a tile's picture

// A small picture of a hit effect: a block landing on a few keys, and what the press shows.
void hit_picture(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, HitEffect effect, RGBA note) {
    const float r = k.s(tok::kRadiusSm);
    dl->AddRectFilled(a, b, u32(vec(kBackgroundColor)), r);
    dl->PushClipRect(a, b, true);
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    const float line = a.y + h * 0.66f;
    const ImVec4 c = vec(note);
    const float cx = (a.x + b.x) * 0.5f;
    const float block_w = w * 0.16f;
    // Keys under the line.
    dl->AddRectFilled({a.x, line}, b, u32({0.86f, 0.88f, 0.92f, 1.0f}));
    for (int i = 1; i < 5; ++i) {
        const float x = a.x + w * static_cast<float>(i) / 5.0f;
        dl->AddLine({x, line}, {x, b.y}, u32({0.55f, 0.58f, 0.64f, 1.0f}), 1.0f);
    }
    dl->AddRectFilled({cx - block_w * 0.5f, line}, {cx + block_w * 0.5f, b.y},
                      u32(with_alpha(c, 0.85f)));
    const float stroke = k.s(kStroke);
    switch (effect) {
    case HitEffect::None:
        break;
    case HitEffect::Glow:
        for (int i = 4; i >= 1; --i) {
            const float f = static_cast<float>(i) / 4.0f;
            dl->AddEllipseFilled({cx, line}, {w * 0.3f * f, h * 0.32f * f},
                                 u32(with_alpha(c, 0.12f + 0.1f * (1.0f - f))), 0.0f, 32);
        }
        break;
    case HitEffect::Ripple:
        dl->AddEllipse({cx, line}, {w * 0.34f, h * 0.12f}, u32(with_alpha(c, 0.9f)), 0.0f, 32,
                       stroke);
        dl->AddEllipse({cx, line}, {w * 0.2f, h * 0.07f}, u32(with_alpha(c, 0.5f)), 0.0f, 32,
                       stroke);
        break;
    case HitEffect::Sparks:
        for (int i = 0; i < 5; ++i) {
            const float ang = -2.5f + 0.4f * static_cast<float>(i);
            const float d0 = h * 0.12f;
            const float d1 = h * (0.3f + 0.08f * static_cast<float>(i % 2));
            dl->AddLine({cx + d0 * std::cos(ang), line + d0 * std::sin(ang)},
                        {cx + d1 * std::cos(ang), line + d1 * std::sin(ang)},
                        u32(with_alpha(c, 0.95f)), stroke);
        }
        break;
    }
    // The block itself, above the line.
    dl->AddRectFilled({cx - block_w * 0.5f, a.y + h * 0.18f}, {cx + block_w * 0.5f, line}, u32(c),
                      k.s(tok::kS1) * 0.5f);
    dl->PopClipRect();
}

} // namespace

namespace colour_detail {

void hit_tiles(Kit& k, LookSettings& look, float width) {
    heading(k, width, "Hit effect");
    catalogue_sweep::drawn(Setting::HitEffect);
    struct Tile {
        HitEffect effect;
        const char* id;
        const char* caption;
    };
    static constexpr Tile kTiles[] = {{HitEffect::None, "##hit-none", "None"},
                                      {HitEffect::Glow, "##hit-glow", "Glow"},
                                      {HitEffect::Ripple, "##hit-ripple", "Ripple"},
                                      {HitEffect::Sparks, "##hit-sparks", "Sparks"}};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float gap = k.s(tok::kHitGap);
    const float th = k.s(kHitTileH);
    const RGBA note = note_color(look, 64, 100, Hand::Right);
    const float csize = k.s(tok::kCaption);
    for (int i = 0; i < kHitTiles; ++i) {
        const Tile& tile = kTiles[i];
        const auto [x0, x1] = cell_span(pos.x, width, gap, kHitTiles, i);
        const float tw = x1 - x0;
        const ImVec2 a{x0, pos.y};
        const ImVec2 b{x1, a.y + th};
        ImGui::SetCursorScreenPos(a);
        const bool pressed = ImGui::InvisibleButton(tile.id, {tw, th});
        const bool on = look.hit_effect == tile.effect;
        face(k, dl, tile.id, a, b, a, b, on);
        const float inset = k.s(tok::kS1);
        hit_picture(k, dl, {a.x + inset, a.y + inset}, {b.x - inset, b.y - k.s(kTileCaption)},
                    tile.effect, note);
        ImFont* font = on ? k.bold : k.regular;
        label_at(k, dl, font, csize, {a.x, b.y - k.s(kTileCaption), b.x, b.y}, LabelAlign::Centre,
                 on ? col::kAccent : col::kText2, tile.caption);
        if (tapped(pressed) && !on) {
            look.hit_effect = tile.effect;
        }
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, th});
}

// What playing shows: Key flash, Held glow and the combo, under the hit tiles. A dead gap between
// the rows.
void press_rows(Kit& k, LookSettings& look) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, k.s(tok::kHitGap)});
    catalogue_sweep::drawn(Setting::KeyFlash);
    switch_row(k, icon::kFlash, "Key flash", &look.key_flash,
               "A note flashes white and its glow swells for a moment when you press its key.");
    catalogue_sweep::drawn(Setting::HeldGlow);
    switch_row(k, icon::kGlow, "Held glow", &look.held_glow,
               "A note glows brighter and wider for as long as you hold its key.");
    catalogue_sweep::drawn(Setting::ComboNumber);
    switch_row(k, icon::kFlame, "Combo", &look.combo_number,
               "The count of notes in a row, from 10, in the top-right corner.");
    ImGui::PopStyleVar();
}

// How the notes and the screen look: the trail, the rising notes, the background, and the two that
// change everything last, High contrast and Reduced effects.
void screen_rows(Kit& k, LookSettings& look) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, k.s(tok::kHitGap)});
    catalogue_sweep::drawn(Setting::TrailFade);
    switch_row(k, icon::kTrail, "Trail fade", &look.trail_fade,
               "Released notes fade out as they rise.");
    catalogue_sweep::drawn(Setting::RisingInSong);
    switch_row(k, icon::kArrowUp, "Show my notes rising during a song", &look.rising_in_song,
               "Off: a song shows your notes at the keyboard line only.");
    constexpr Seg kBg[] = {{nullptr, "Plain"}, {nullptr, "Gradient"}, {nullptr, "Motion"}};
    int bg = static_cast<int>(look.background);
    catalogue_sweep::drawn(Setting::Background);
    if (segmented(k, "bg", icon::kBackground, nullptr, kBg, &bg)) {
        look.background = static_cast<BackgroundStyle>(bg);
    }
    catalogue_sweep::drawn(Setting::HighContrast);
    switch_row(k, icon::kContrast, "High contrast", &look.high_contrast);
    catalogue_sweep::drawn(Setting::ReducedEffects);
    switch_row(k, icon::kReduced, "Reduced effects", &look.reduced_effects,
               "Turns off background motion and the hit effect, and cuts Key flash to a short whitening with "
               "no bloom.");
    ImGui::PopStyleVar();
}

void preview(Kit& k, const LookSettings& look, float width, float height) {
    if (k.preview != nullptr) {
        k.preview->draw(k, look, width, height, PreviewLoop::Song);
    } else {
        ImGui::Dummy({width, height});
    }
}

} // namespace colour_detail

} // namespace etude::app::ui
