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
#include "etude/saved_palettes.hpp"
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
using colour_detail::HoldMenu;
using colour_detail::follow_hold_menu_cell;
using colour_detail::open_hold_menu;
using colour_detail::press_rows;
using colour_detail::screen_rows;
using colour_detail::preview;
using colour_detail::gap;

constexpr float kStripDotR = tok::kS1;          // a palette strip's dots

constexpr float kStripStep = tok::kS1 * 1.5f;   // so they overlap a little

constexpr float kChipIconRise = tok::kS2;       // a chip's icon above its centre

// The chip's picture of a scheme, in the look's own colours: two hands, a ring of note colours,
// octave bars, bars rising soft to hard, one disc. Centred on `c`, about `size` across.
void scheme_icon(const Kit& k, ImDrawList* dl, const LookSettings& look, ColorScheme scheme,
                 ImVec2 c, float size) {
    LookSettings as = look;
    as.color_scheme = scheme;
    const auto colour = [&](int note, int velocity, Hand hand) {
        return vec(note_color(as, note, static_cast<std::uint8_t>(velocity), hand));
    };
    switch (scheme) {
    case ColorScheme::ByHand:
        hand_pair(k, dl, c, size, colour(60, 100, Hand::Left), true, false);
        hand_pair(k, dl, c, size, colour(60, 100, Hand::Right), false, true);
        return;
    case ColorScheme::ByPitchClass: {
        const float ring = size * 0.42f;
        const float dot = size * 0.12f;
        for (int i = 0; i < 12; ++i) {
            const float a = static_cast<float>(i) * 0.5235988f - 1.5707963f;
            dl->AddCircleFilled({c.x + ring * std::cos(a), c.y + ring * std::sin(a)}, dot,
                                u32(colour(60 + i, 100, Hand::Right)), 12);
        }
        return;
    }
    case ColorScheme::ByOctave: {
        const float bw = size * 0.22f;
        const float step = size * 0.34f;
        for (int i = 0; i < 3; ++i) {
            const float x = c.x + step * static_cast<float>(i - 1);
            dl->AddRectFilled({x - bw * 0.5f, c.y - size * 0.45f}, {x + bw * 0.5f, c.y + size * 0.45f},
                              u32(colour(48 + 12 * i, 100, Hand::Right)), bw * 0.5f);
        }
        return;
    }
    case ColorScheme::ByVelocity: {
        const float bw = size * 0.17f;
        const float step = size * 0.26f;
        for (int i = 0; i < 4; ++i) {
            const float x = c.x + step * (static_cast<float>(i) - 1.5f);
            const float h = size * (0.3f + 0.2f * static_cast<float>(i));
            dl->AddRectFilled({x - bw * 0.5f, c.y + size * 0.45f - h},
                              {x + bw * 0.5f, c.y + size * 0.45f},
                              u32(colour(60, 30 + 32 * i, Hand::Right)), bw * 0.5f);
        }
        return;
    }
    case ColorScheme::Single:
        dl->AddCircleFilled(c, size * 0.42f, u32(colour(60, 100, Hand::Right)), 32);
        return;
    }
}

// A row of overlapping dots ending at `right`, centred on `cy`.
void strip_dots(const Kit& k, ImDrawList* dl, const Swatches& s, ColorScheme scheme, float right,
                float cy) {
    const std::span<const int> shown = strip_swatches(scheme);
    const float r = k.s(kStripDotR);
    const float step = k.s(kStripStep);
    float x = right - r - step * static_cast<float>(shown.size() - 1);
    for (const int i : shown) {
        dl->AddCircleFilled({x, cy}, r + 1.0f, u32(col::kSurfaceHi), 16);
        dl->AddCircleFilled({x, cy}, r, u32(vec(s.colors[static_cast<std::size_t>(i)])), 16);
        x += step;
    }
}

float strip_width(const Kit& k, ColorScheme scheme) {
    const auto n = static_cast<float>(strip_swatches(scheme).size());
    return k.s(kStripDotR) * 2.0f + k.s(kStripStep) * (n - 1.0f);
}

// A palette's cell: its face and name at the left and the strip of its colours at the right. An
// empty saved slot has "Save current" and a save icon in the strip's place. The cell is one
// pressable; what it was pressed for comes back.
struct PaletteCell {
    const char* id = "##palette";
    int index = 0;
    ImVec2 a{};
    float width = 0.0f;
    std::string name;
    const Swatches* strip = nullptr; // null: an empty slot
    bool on = false;
};

Press draw_palette_cell(Kit& k, ImDrawList* dl, const PaletteCell& c, ColorScheme scheme) {
    const float h = k.s(tok::kHit);
    const float size = k.s(tok::kLabel);
    ImGui::SetCursorScreenPos(c.a);
    ImGui::PushID(c.index);
    const bool pressed = ImGui::InvisibleButton(c.id, {c.width, h});
    ImGui::PopID();
    const Press press = press_of(pressed);
    const ImVec2 fa{c.a.x, c.a.y + k.s(kFaceInset)};
    const ImVec2 fb{c.a.x + c.width, c.a.y + h - k.s(kFaceInset)};
    face(k, dl, c.id, fa, fb, c.a, {c.a.x + c.width, c.a.y + h}, c.on);
    const float pad = k.s(tok::kS3);
    ImFont* font = c.on ? k.bold : k.regular;
    const float isize = k.s(tok::kIconSm);
    const float sw = c.strip != nullptr ? strip_width(k, scheme) : isize;
    const FitText name = fit(font, size, c.name, std::max(0.0f, c.width - sw - pad * 3.0f));
    const float cy = (fa.y + fb.y) * 0.5f;
    label_at(k, dl, font, size, {fa.x + pad, fa.y, fb.x, fb.y}, LabelAlign::Left,
             c.on ? col::kText : col::kText2, name.c_str());
    if (c.strip != nullptr) {
        audit::note(c.id, c.a, {c.a.x + c.width, c.a.y + h}, InkKind::Target);
        strip_dots(k, dl, *c.strip, scheme, fb.x - pad, cy);
    } else {
        const float ix = fb.x - pad - isize * 0.5f;
        audit::note(c.id, {ix - isize * 0.5f, cy - isize * 0.5f},
                    {ix + isize * 0.5f, cy + isize * 0.5f});
        if (!audit::icons_hidden()) {
            glyph(k, dl, icon::kSave, {ix, cy}, isize, col::kText3);
        }
    }
    return press;
}

// The saved slot `slot` as a cell at `a`, `cw` wide. A tap on an empty slot saves the note colours
// as they are now, a tap on a full one applies them, a hold (or a right click) on a full one opens
// its menu.
void saved_slot(Kit& k, ImDrawList* dl, LookSettings& look, int slot, ImVec2 a, float cw,
                const ActivePalette& active) {
    static constexpr const char* kNames[kSavedPalettes] = {"Saved 1", "Saved 2"};
    static constexpr const char* kIds[kSavedPalettes] = {"##saved-1", "##saved-2"};
    const bool filled = saved_palette_filled(look, slot);
    const Swatches strip = saved_palette_swatches(look, slot);
    PaletteCell cell;
    cell.id = kIds[slot];
    cell.index = slot;
    cell.a = a;
    cell.width = cw;
    cell.name = filled ? kNames[slot] : "Save current";
    cell.strip = filled ? &strip : nullptr;
    cell.on = active.slot == slot;
    const Press press = draw_palette_cell(k, dl, cell, ColorScheme::ByPitchClass);
    follow_hold_menu_cell(HoldMenu::Kind::Slot, slot, a, {a.x + cw, a.y + k.s(tok::kHit)});
    if (press.tap) {
        if (filled) {
            apply_saved_palette(look, slot);
        } else {
            save_palette_slot(look, slot);
            k.say(std::string("Saved the note colours as ") + kNames[slot]);
        }
        g_picker.swatch = -1;
    } else if (press.hold && filled) {
        open_hold_menu(HoldMenu::Kind::Slot, slot, a, {a.x + cw, a.y + k.s(tok::kHit)});
    }
}

} // namespace

namespace colour_detail {

// The twelve notes' names as the look writes them (letters when names are off), made again only
// when the naming changes.
const char* note_name(const LookSettings& look, int pc) {
    struct Cache {
        bool valid = false;
        NameMode mode = NameMode::Letters;
        NameSpelling spelling = NameSpelling::Sharps;
        std::array<std::array<char, 8>, 12> text{};
    };
    static Cache cache;
    const NameMode mode = look.name_mode == NameMode::Off ? NameMode::Letters : look.name_mode;
    // A swatch's caption names the twelve notes, not a key: By key shows them with sharps.
    const NameSpelling spelling =
        look.names_spelling == NameSpelling::Flats ? NameSpelling::Flats : NameSpelling::Sharps;
    if (!cache.valid || cache.mode != mode || cache.spelling != spelling) {
        LookSettings named;
        named.name_mode = mode;
        named.names_spelling = spelling;
        for (int i = 0; i < 12; ++i) {
            const std::string s = accidental_display(note_short_label(60 + i, named));
            auto& out = cache.text[static_cast<std::size_t>(i)];
            std::snprintf(out.data(), out.size(), "%s", s.c_str());
        }
        cache = {true, mode, spelling, cache.text};
    }
    return cache.text[static_cast<std::size_t>(std::clamp(pc, 0, 11))].data();
}

const char* scheme_heading(ColorScheme scheme) {
    switch (scheme) {
    case ColorScheme::ByHand:
        return "Hand colours";
    case ColorScheme::ByPitchClass:
        return "Note colours";
    case ColorScheme::ByOctave:
        return "Octave colours";
    case ColorScheme::ByVelocity:
        return "Soft to hard";
    case ColorScheme::Single:
        return "The colour";
    }
    return "";
}

void scheme_chips(Kit& k, LookSettings& look, float width) {
    struct Chip {
        ColorScheme scheme;
        const char* id;
        const char* caption;
    };
    static constexpr Chip kChips[] = {{ColorScheme::ByHand, "##by-hand", "Hand"},
                                      {ColorScheme::ByPitchClass, "##by-note", "Note"},
                                      {ColorScheme::ByOctave, "##by-octave", "Octave"},
                                      {ColorScheme::ByVelocity, "##by-velocity", "Velocity"},
                                      {ColorScheme::Single, "##one-colour", "One"}};
    catalogue_sweep::drawn(Setting::ColourScheme);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float gap = k.s(tok::kHitGap);
    const float h = k.s(kSchemeChipH);
    const float csize = k.s(tok::kCaption);
    for (int i = 0; i < kSchemeChips; ++i) {
        const Chip& chip = kChips[i];
        const auto [x0, x1] = cell_span(pos.x, width, gap, kSchemeChips, i);
        const float cw = x1 - x0;
        const ImVec2 a{x0, pos.y};
        const ImVec2 b{x1, a.y + h};
        ImGui::SetCursorScreenPos(a);
        const bool pressed = ImGui::InvisibleButton(chip.id, {cw, h});
        const bool on = look.color_scheme == chip.scheme;
        face(k, dl, chip.id, a, b, a, b, on);
        const ImVec2 icon_c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f - k.s(kChipIconRise)};
        const float isize = k.s(tok::kIcon);
        audit::note(chip.id, {icon_c.x - isize * 0.5f, icon_c.y - isize * 0.5f},
                    {icon_c.x + isize * 0.5f, icon_c.y + isize * 0.5f}, InkKind::Centred, false);
        if (!audit::icons_hidden()) {
            scheme_icon(k, dl, look, chip.scheme, icon_c, isize);
        }
        const FitText shown = fit(on ? k.bold : k.regular, csize, chip.caption, cw);
        const ImVec2 ts = text_size(on ? k.bold : k.regular, csize, shown.c_str());
        label_at(k, dl, on ? k.bold : k.regular, csize,
                 {a.x, b.y - ts.y - k.s(tok::kS2), b.x, b.y - k.s(tok::kS2)}, LabelAlign::Centre,
                 on ? col::kAccent : col::kText2, shown.c_str());
        if (tapped(pressed) && !on) {
            look.color_scheme = chip.scheme;
            g_picker.swatch = -1;
        }
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, h});
}

void palette_rows(Kit& k, LookSettings& look, float width) {
    const ColorScheme scheme = look.color_scheme;
    const ActivePalette active = active_palette(look, scheme);
    heading(k, width, "Palette", active.custom() ? "Custom" : nullptr);
    catalogue_sweep::drawn(Setting::Palette);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const int cols = palette_columns(width, k.u);
    const float gap = k.s(kBlockGap);
    const float pitch = k.s(tok::kCell); // a row's hit area and the dead gap
    // The built-in palettes, then (with the notes as the scheme) the two saved ones.
    const int shown = static_cast<int>(kAllPalettes.size()) +
                      (scheme == ColorScheme::ByPitchClass ? kSavedPalettes : 0);
    const auto cell_at = [&](int i) {
        const auto [x0, x1] = cell_span(pos.x, width, gap, cols, i % cols);
        return std::pair{ImVec2{x0, pos.y + pitch * static_cast<float>(i / cols)}, x1 - x0};
    };
    for (std::size_t i = 0; i < kAllPalettes.size(); ++i) {
        const Palette p = kAllPalettes[i];
        const auto [a, cw] = cell_at(static_cast<int>(i));
        const Swatches strip = palette_swatches(p, scheme);
        PaletteCell cell;
        cell.index = static_cast<int>(i);
        cell.a = a;
        cell.width = cw;
        cell.name = std::string(palette_name(p));
        cell.strip = &strip;
        cell.on = active.built_in == p;
        if (draw_palette_cell(k, dl, cell, scheme).tap) {
            apply_palette(look, p);
            g_picker.swatch = -1;
        }
    }
    for (int slot = 0; slot < shown - static_cast<int>(kAllPalettes.size()); ++slot) {
        const auto [a, cw] = cell_at(static_cast<int>(kAllPalettes.size()) + slot);
        saved_slot(k, dl, look, slot, a, cw, active);
    }
    // The room for all the palettes whichever scheme is shown, so the blocks below stay put.
    const int rows = (kPaletteCount + cols - 1) / cols;
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, pitch * static_cast<float>(rows) - k.s(tok::kHitGap)});
}

} // namespace colour_detail

} // namespace etude::app::ui
