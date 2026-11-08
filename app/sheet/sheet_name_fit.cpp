// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_name_fit.hpp"

#include "etude/accidental_text.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app {
namespace {

constexpr float kMeasurePx = 32.0f; // names are measured at this size, then scaled
// In staff spaces of the engraving: the gap round a name; the lane's start outside the outer
// staff lines (the cursor reaches 1 space out, the loop bracket's band 0.4 more).
constexpr float kNameGapSpaces = 0.25f;
constexpr float kNameClearSpaces = 1.0f + 0.4f + 0.3f;

} // namespace

void SheetNameTexts::make(const LookSettings& look, Text& font) {
    LookSettings named = look;
    named.name_mode = look.sheet_naming == SheetNaming::Solfege ? NameMode::Solfege : NameMode::Letters;
    const float line = std::max(1.0f, font.measure("Hg", kMeasurePx).y);
    line_em = line / kMeasurePx;
    for (int p = 0; p < 128; ++p) {
        text[static_cast<std::size_t>(p)] = accidental_display(note_label(p, named));
        unit_width[static_cast<std::size_t>(p)] = font.measure(text[static_cast<std::size_t>(p)], kMeasurePx).x / line;
    }
}

void SheetSymbols::clear() {
    text.clear();
    unit_width.clear();
    ids.clear();
}

void SheetSymbols::name_columns(std::vector<SheetNameColumn>& columns, bool flats, float line_em, Text& font) {
    for (SheetNameColumn& c : columns) {
        if (!c.chord.found) {
            continue;
        }
        std::string symbol = accidental_display(chord_name(c.chord, flats));
        auto [it, added] = ids.try_emplace(symbol, static_cast<std::int32_t>(text.size()));
        if (added) {
            unit_width.push_back(font.measure(symbol, kMeasurePx).x / (std::max(0.01f, line_em) * kMeasurePx));
            text.push_back(std::move(symbol));
        }
        c.symbol = it->second;
        c.symbol_unit_width = unit_width[static_cast<std::size_t>(it->second)];
    }
}

float sheet_system_scale(const SheetSystem& sys, float engraved_width, float system_width_px, float slot_px,
                         float margin_px) {
    float scale = system_width_px / std::max(1.0f, engraved_width);
    const float room = slot_px + margin_px;
    if (room > 0.0f && sys.height * scale > room) {
        scale = room / std::max(1.0f, sys.height);
    }
    return scale;
}

float sheet_name_font_px(float staff_space_px, float px_per_mm) {
    const float ppmm = std::isfinite(px_per_mm) && px_per_mm > 0.0f ? px_per_mm : kDefaultPxPerMm;
    return std::round(std::clamp(kSheetNameFontSpaces * staff_space_px, kSheetNameFontMm * ppmm,
                                 kSheetNameFontMaxMm * ppmm));
}

SheetNameRoom sheet_name_room(const SheetNameStacks& stacks, float px_per_mm, float line_em) {
    SheetNameRoom r;
    r.above = stacks.above;
    r.below = stacks.below;
    r.line_px = sheet_name_font_px(0.0f, px_per_mm) * line_em;
    r.line_spaces = kSheetNameFontSpaces * line_em;
    const float ppmm = std::isfinite(px_per_mm) && px_per_mm > 0.0f ? px_per_mm : kDefaultPxPerMm;
    r.line_max_px = std::round(kSheetNameFontMaxMm * ppmm) * line_em;
    return r;
}

SheetNameStyle sheet_name_style(float staff_space_px, float scale, float px_per_mm,
                                const SheetNameTexts& texts) {
    SheetNameStyle st;
    const float s = std::max(0.01f, scale);
    // The font is sized on the screen; the picture is drawn `scale` times as large.
    const float font = sheet_name_font_px(staff_space_px * s, px_per_mm);
    const float floor_font = std::min(font, std::max(min_name_font_px(px_per_mm), std::round(kSheetNameFloorShare * font)));
    st.size = font * texts.line_em / s;
    st.min_size = floor_font * texts.line_em / s;
    st.more_unit_height = kSheetNameMoreDot;
    st.more_unit_width = 5.0f * kSheetNameMoreDot;
    st.gap = kNameGapSpaces * staff_space_px;
    st.line_gap = 0.0f;
    st.clear = kNameClearSpaces * staff_space_px;
    return st;
}

SheetNameSpacing sheet_name_spacing(bool names, SheetNaming naming) {
    if (!names) {
        return {};
    }
    // Measured with --sheet-dump --names at 800x480, 1366x768 and 1920x1080: from here no name of
    // the Beethoven or the ABC reel drops, in letters or Do Re Mi (Verovio's own 0.25 / 0.6 drops
    // some at 800x480).
    (void)naming;
    return SheetNameSpacing{0.35f, 0.65f};
}

} // namespace etude::app
