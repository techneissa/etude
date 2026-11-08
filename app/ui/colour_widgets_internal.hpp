// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of colour_widgets.cpp share. Not part of the app's interface.
#pragma once

#include "colour_widgets.hpp"

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

namespace colour_detail {

inline constexpr float kRingGap = tok::kS1 * 0.75f;    // between a chosen swatch and its ring

inline constexpr float kRingWidth = tok::kS1 * 0.5f;

inline constexpr float kFaceInset = tok::kS1 * 0.5f;   // a row's face inside its hit area, top and bottom

inline constexpr float kHeadingDrop = tok::kS1;        // a heading's text above its row's bottom

// The swatch the colour control is open on (-1 for none) and the colour as the bars hold it, so
// a grey keeps its place on the hue bar and nothing jitters from rounding to bytes.
struct Picker {
    int swatch = -1;
    ColorScheme scheme = ColorScheme::ByHand;
    ColourPick pick;
    bool changed = false; // the square or the bar moved during the touch that is on it
};

inline Picker g_picker;

// The saved list (`AppSettings::recent_colors`), set by `colours_page` each frame it draws.
inline RecentColours* g_recent = nullptr;

// The menu a hold (or a right click) on a swatch or a filled saved palette opens, one small card
// under the cell: a swatch's Reset to default, or a saved palette's Save current colours here and
// Clear. At most one is open.
struct HoldMenu {
    enum class Kind : std::uint8_t { None, Swatch, Slot };
    Kind kind = Kind::None;
    int index = -1;           // the swatch, or the slot
    ImVec2 min{};             // the held cell, in screen points
    ImVec2 max{};
    ImVec2 card_min{};        // the card as drawn last frame (zero while none was)
    ImVec2 card_max{};
    int frame = -10;          // the last frame the page drew it, so a menu left open goes
    OutsideTap outside;
};

inline HoldMenu g_menu;

inline ImVec4 vec(RGBA c, float alpha = 1.0f) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, alpha};
}

// Cell `i` of `n` side by side from `x0`, `width` wide in all with `gap` between them: whole-pixel
// edges, the last ending exactly at `x0 + width`, so rows of cells line up with each other.
inline std::pair<float, float> cell_span(float x0, float width, float gap, int n, int i) {
    const float step = (width + gap) / static_cast<float>(n);
    const float a = std::round(x0 + step * static_cast<float>(i));
    const float b = std::round(x0 + step * static_cast<float>(i + 1) - gap);
    return {a, i == n - 1 ? std::round(x0 + width) : b};
}

// A control's face for the item just drawn: the kit's one state helper (`draw_state`) draws the
// idle fill, hover (mouse only), pressed and selected inside `face`, which stays inside `area`.
inline void face(const Kit& k, ImDrawList* dl, const char* name, ImVec2 a, ImVec2 b, ImVec2 area_a,
          ImVec2 area_b, bool selected, Idle idle = Idle::Fill) {
    draw_state(k, dl, name, {a.x, a.y, b.x, b.y}, {area_a.x, area_a.y, area_b.x, area_b.y},
               item_state(k, selected), idle);
}

// A heading like the kit's section, `kSectionH` tall, with an optional note at its right end.
inline void heading(Kit& k, float width, const char* text, const char* note = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = k.s(kSectionH);
    const float size = k.s(tok::kCaption);
    const ImVec2 ts = text_size(k.bold, size, text);
    const float y = pos.y + h - ts.y - k.s(kHeadingDrop);
    text_at(k, dl, k.bold, size, {pos.x + k.s(tok::kS1), y}, col::kText3, text);
    if (note != nullptr) {
        const ImVec2 ns = text_size(k.regular, size, note);
        text_at(k, dl, k.regular, size, {pos.x + width - k.s(tok::kS1) - ns.x, y}, col::kText2,
                note);
    }
    ImGui::Dummy({width, h});
}

// The twelve notes' names as the look writes them (letters when names are off), made again only
// when the naming changes.
const char* note_name(const LookSettings& look, int pc);

// The short label inside swatch `i` (empty for none) and its longer name, for the colour control's
// heading and for the captions of a scheme with few swatches.
inline void swatch_labels(const LookSettings& look, ColorScheme scheme, int i, std::span<char> inside,
                   std::span<char> name) {
    static constexpr const char* kHands[] = {"Left hand", "Right hand", "Other notes"};
    static constexpr const char* kHandMarks[] = {"L", "R", ""};
    static constexpr const char* kVelocities[] = {"Soft", "Medium", "Hard"};
    const auto put = [](std::span<char> out, const char* text) {
        std::snprintf(out.data(), out.size(), "%s", text);
    };
    switch (scheme) {
    case ColorScheme::ByHand:
        put(inside, kHandMarks[std::clamp(i, 0, 2)]);
        put(name, kHands[std::clamp(i, 0, 2)]);
        return;
    case ColorScheme::ByPitchClass:
        put(inside, note_name(look, i));
        std::snprintf(name.data(), name.size(), "Note %s", note_name(look, i));
        return;
    case ColorScheme::ByOctave: {
        const int shown = i + look.octave_offset();
        std::snprintf(inside.data(), inside.size(), "%d", shown);
        std::snprintf(name.data(), name.size(), "Octave %d", shown);
        return;
    }
    case ColorScheme::ByVelocity:
        put(inside, "");
        put(name, kVelocities[std::clamp(i, 0, 2)]);
        return;
    case ColorScheme::Single:
        put(inside, "");
        put(name, "All notes");
        return;
    }
}

const char* scheme_heading(ColorScheme scheme);

void scheme_chips(Kit& k, LookSettings& look, float width);

void palette_rows(Kit& k, LookSettings& look, float width);

// A round swatch in its cell, the label inside it in the ink that reads on it. Returns the circle's
// radius.
inline float draw_swatch(const Kit& k, ImDrawList* dl, ImVec2 centre, float r, RGBA c, const char* label,
                  bool chosen) {
    const float rr = r;
    if (chosen) {
        dl->AddCircle(centre, r + k.s(kRingGap), u32(col::kText), 40, k.s(kRingWidth));
    }
    dl->AddCircleFilled(centre, rr, u32(vec(c)), 40);
    dl->AddCircle(centre, rr, u32(col::kHairline), 40, 1.0f);
    if (label != nullptr && label[0] != '\0') {
        const float size = k.s(tok::kCaption);
        label_at(k, dl, k.bold, size, {centre.x - r, centre.y - r, centre.x + r, centre.y + r},
                 LabelAlign::Centre, vec(label_color_for(c)), label);
    }
    return rr;
}

void swatch_grid(Kit& k, LookSettings& look, float width);

// The colour control: the swatch being changed with Done, the square and the hue bar.
void picker_block(Kit& k, LookSettings& look, float width);

// The quick picks and the recent colours under them. The quick picks are a chart: the hues across
// in colour-wheel order, light, bright and deep down each column, the greys last. The recent colours
// are `kRecentRows` rows of the same cells, newest first; the cells not yet used are drawn faint.
// One tap puts a colour on the swatch being changed. Every cell is a hit area.
void quick_picks_block(Kit& k, LookSettings& look, float width);

void hit_tiles(Kit& k, LookSettings& look, float width);

// What playing shows (Key flash, Held glow, Combo), and how the notes and the screen look (Trail
// fade, notes rising, Background, High contrast, Reduced effects): two blocks of switch rows.
void press_rows(Kit& k, LookSettings& look);
void screen_rows(Kit& k, LookSettings& look);

void preview(Kit& k, const LookSettings& look, float width, float height);

// The palette a scheme's colours came from ("Classic", or for the notes "Saved 1"): what a reset
// goes back to. And the name of the palette the colours are now ("Custom" for none).
std::string palette_base_name(const LookSettings& look, ColorScheme scheme);
std::string active_palette_name(const ActivePalette& active);

// Called every frame for each cell the menu can be on: closes the menu when that cell is not where
// it was when the menu opened (the page scrolled).
void follow_hold_menu_cell(HoldMenu::Kind kind, int index, ImVec2 min, ImVec2 max);

// Opens the hold menu for the cell at `min`..`max`.
void open_hold_menu(HoldMenu::Kind kind, int index, ImVec2 min, ImVec2 max);
// Draws the open menu (nothing while none is open) as a card of its own over the page, closing it
// on a pick or a tap outside it. Called once a frame at the end of the page.
void hold_menu(Kit& k, LookSettings& look);

inline void gap(const ColoursLayout& l) { ImGui::Dummy({1.0f, l.gap}); }

} // namespace colour_detail

} // namespace etude::app::ui
