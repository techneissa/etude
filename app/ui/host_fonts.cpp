// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_fonts.hpp"

#include "../music_font.hpp"

#include <imgui.h>

#include <span>

namespace etude::app {

// Generated from the font files by cmake/EmbedFile.cmake.
std::span<const unsigned char> embedded_font();
std::span<const unsigned char> embedded_font_regular();
std::span<const unsigned char> embedded_icons();

namespace ui {

void add_ui_fonts(Kit& kit) {
    ImGuiIO& io = ImGui::GetIO();
    // Whatever was measured so far was measured in other fonts (or none): the kit's stores of
    // widths, ink and fitted lines start again here and once more when the fonts are all in.
    forget_fonts();
    // Each text font gets the icon font merged in, so a string can mix words and icons, and the
    // music font for the two signs Atkinson lacks: ♭ (U+266D) and ♯ (U+266F), written by every
    // note, chord and key name. Each is its own source so each can be drawn at the size that stands
    // it beside Atkinson's capitals: the flat is small in its font, the sharp tall.
    // The ranges the music font does not give: everything but the one sign.
    static const ImWchar kNotFlat[] = {1, 0x266C, 0x266E, 0xFFFF, 0};
    static const ImWchar kNotSharp[] = {1, 0x266E, 0x2670, 0xFFFF, 0};
    // The sharp hangs a quarter em below the baseline in its font: lifted so it stands on it. A
    // glyph offset is in pixels at the first source's `SizePixels` and scales with the size drawn,
    // so the text font names a reference size (every size is pushed explicitly).
    constexpr float kReferencePx = 16.0f;
    constexpr float kSharpLiftPx = 3.2f;
    constexpr float kSignWeight = 1.2f;
    const auto add = [&io](std::span<const unsigned char> data, bool merge,
                           const ImWchar* exclude = nullptr, float scale = 1.0f,
                           float lift_px = 0.0f) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false; // the bytes are compiled into the program
        cfg.MergeMode = merge;
        cfg.GlyphExcludeRanges = exclude;
        cfg.ExtraSizeScale = scale;
        cfg.GlyphOffset.y = -lift_px;
        if (exclude != nullptr) {
            // The signs are thin strokes: no oversampling (it smears them sideways) and a little
            // more weight.
            cfg.OversampleH = 1;
            cfg.OversampleV = 1;
            cfg.RasterizerMultiply = kSignWeight;
        }
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data.data()),
                                              static_cast<int>(data.size()),
                                              merge && lift_px == 0.0f ? 0.0f : kReferencePx, &cfg);
    };
    kit.regular = add(embedded_font_regular(), false);
    add(embedded_icons(), true);
    add(music_font(), true, kNotFlat, kFlatSignScale);
    add(music_font(), true, kNotSharp, kSharpSignScale, kSharpLiftPx);
    kit.bold = add(embedded_font(), false);
    add(embedded_icons(), true);
    add(music_font(), true, kNotFlat, kFlatSignScale);
    add(music_font(), true, kNotSharp, kSharpSignScale, kSharpLiftPx);
    // The music font alone, with the three signs the title medals' emblems use: the quarter note, the
    // beamed eighth notes and the treble clef (drawn by ui/medal_emblem.cpp).
    static const ImWchar kEmblemGlyphs[] = {0x2669, 0x2669, 0x266B, 0x266B, 0x1D11E, 0x1D11E, 0};
    ImFontConfig emblem;
    emblem.FontDataOwnedByAtlas = false;
    emblem.GlyphRanges = kEmblemGlyphs;
    emblem.OversampleH = 1;
    emblem.OversampleV = 1;
    const std::span<const unsigned char> music = music_font();
    kit.music = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(music.data()),
                                                      static_cast<int>(music.size()), kReferencePx, &emblem);
    forget_fonts();
}

} // namespace ui
} // namespace etude::app
