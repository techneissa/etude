// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_ink.hpp"

#include "ink_measure.hpp"
#include "kit.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>

namespace etude::app::ui {

namespace {

// By font as well: the same code point at the same size is another glyph in another font.
using InkCache = std::map<std::tuple<const ImFont*, std::uint32_t, std::uint32_t>, GlyphInk>;

InkCache& ink_cache() {
    static InkCache cache;
    return cache;
}

using CapCache = std::map<std::pair<ImFont*, int>, CapBand>;

CapCache& cap_cache() {
    static CapCache cache;
    return cache;
}

} // namespace

GlyphInk glyph_ink(ImFont* font, ImFontBaked* baked, const ImFontGlyph* g, ImWchar cp) {
    InkCache& cache = ink_cache();
    const auto key =
        std::make_tuple(static_cast<const ImFont*>(font), static_cast<std::uint32_t>(cp),
                        static_cast<std::uint32_t>(baked->Size * 16.0f));
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    GlyphInk ink{g->X0, g->Y0, g->X1, g->Y1};
    const ImTextureData* tex = font->OwnerAtlas != nullptr ? font->OwnerAtlas->TexData : nullptr;
    if (tex != nullptr && tex->Pixels != nullptr && tex->Width > 0 && tex->Height > 0 &&
        (tex->BytesPerPixel == 1 || tex->BytesPerPixel == 4)) {
        const int u0 = static_cast<int>(std::lround(g->U0 * static_cast<float>(tex->Width)));
        const int u1 = static_cast<int>(std::lround(g->U1 * static_cast<float>(tex->Width)));
        const int v0 = static_cast<int>(std::lround(g->V0 * static_cast<float>(tex->Height)));
        const int v1 = static_cast<int>(std::lround(g->V1 * static_cast<float>(tex->Height)));
        if (u1 > u0 && v1 > v0) {
            const std::uint8_t* alpha =
                tex->Pixels + (tex->BytesPerPixel == 4 ? 3 : 0); // RGBA32 keeps coverage in alpha
            const InkBox box =
                measure_alpha(alpha, tex->Width * tex->BytesPerPixel, tex->BytesPerPixel,
                              tex->Width, tex->Height, {u0, v0, u1, v1});
            if (box.found) {
                // Bitmap pixels to the baked size's pixels (the rasteriser may draw denser).
                const float sx = (g->X1 - g->X0) / static_cast<float>(u1 - u0);
                const float sy = (g->Y1 - g->Y0) / static_cast<float>(v1 - v0);
                ink = {g->X0 + (box.left - static_cast<float>(u0)) * sx,
                       g->Y0 + (box.top - static_cast<float>(v0)) * sy,
                       g->X0 + (box.right - static_cast<float>(u0)) * sx,
                       g->Y0 + (box.bottom - static_cast<float>(v0)) * sy};
            }
        }
    }
    cache.emplace(key, ink);
    return ink;
}

namespace {

// Where a code point's ink lies from the pen position at `size`, and its advance.
bool codepoint_ink(ImFont* font, float size, ImWchar cp, ImVec2* min, ImVec2* max,
                   float* advance = nullptr) {
    ImFontBaked* baked = font->GetFontBaked(size);
    const ImFontGlyph* g = baked->FindGlyphNoFallback(cp);
    if (g == nullptr) {
        return false;
    }
    const GlyphInk ink = glyph_ink(font, baked, g, cp);
    const float scale = size / baked->Size;
    *min = {ink.l * scale, ink.t * scale};
    *max = {ink.r * scale, ink.b * scale};
    if (advance != nullptr) {
        *advance = g->AdvanceX * scale;
    }
    return true;
}

} // namespace

bool font_ink(ImFont* font, float size, char c, ImVec2* min, ImVec2* max) {
    return codepoint_ink(font, size, static_cast<ImWchar>(c), min, max);
}

CapBand cap_band(ImFont* font, float size) {
    CapCache& cache = cap_cache();
    const std::pair<ImFont*, int> key{font, static_cast<int>(std::lround(size * 16.0f))};
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    CapBand band;
    ImVec2 lo;
    ImVec2 hi;
    if (codepoint_ink(font, size, 'H', &lo, &hi)) {
        band = {lo.y, hi.y};
    } else {
        const float h =
            text_size(font, size, "H").y; // no capital in the font: the middle of the line
        band = {h * 0.2f, h * 0.8f};
    }
    cache.emplace(key, band);
    return band;
}

void forget_font_ink() {
    ink_cache().clear();
    cap_cache().clear();
}

} // namespace etude::app::ui
