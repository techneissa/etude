// SPDX-License-Identifier: GPL-3.0-or-later
#include "music_glyph.hpp"

namespace etude::app::ui {

void draw_music_glyph(const Kit& k, ImDrawList* dl, ImVec2 centre, ImWchar cp, float ink_h, ImU32 ink) {
    if (k.music == nullptr || ink_h < 1.0f) {
        return;
    }
    // The glyph's ink at a reference size, then the size that makes it `ink_h` tall.
    constexpr float kRef = 100.0f;
    const ImFontGlyph* ref = k.music->GetFontBaked(kRef)->FindGlyph(cp);
    if (ref == nullptr || ref->Y1 <= ref->Y0) {
        return;
    }
    const float size = kRef * ink_h / (ref->Y1 - ref->Y0);
    const ImFontGlyph* g = k.music->GetFontBaked(size)->FindGlyph(cp);
    if (g == nullptr) {
        return;
    }
    const ImVec2 pos{centre.x - (g->X0 + g->X1) * 0.5f, centre.y - (g->Y0 + g->Y1) * 0.5f};
    char utf8[5] = {};
    if (cp < 0x10000) {
        utf8[0] = static_cast<char>(0xE0 | (cp >> 12));
        utf8[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        utf8[0] = static_cast<char>(0xF0 | (cp >> 18));
        utf8[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    dl->AddText(k.music, size, pos, ink, utf8);
}

} // namespace etude::app::ui
