// SPDX-License-Identifier: GPL-3.0-or-later
#include "music_font.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace etude::app {

// Generated from the font files by cmake/EmbedFile.cmake.
std::span<const unsigned char> embedded_font_regular();
std::span<const unsigned char> embedded_font_music();

namespace {

std::uint32_t be32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

std::int16_t be16(const unsigned char* p) { return static_cast<std::int16_t>((p[0] << 8) | p[1]); }

void put16(unsigned char* p, int value) {
    p[0] = static_cast<unsigned char>((value >> 8) & 0xFF);
    p[1] = static_cast<unsigned char>(value & 0xFF);
}

// The table `tag` of an sfnt file: where it starts, or nullptr.
unsigned char* table(std::span<unsigned char> font, const char (&tag)[5]) {
    if (font.size() < 12) {
        return nullptr;
    }
    const std::size_t count = (static_cast<std::size_t>(font[4]) << 8) | font[5];
    for (std::size_t i = 0; i < count && 12 + 16 * (i + 1) <= font.size(); ++i) {
        const unsigned char* entry = font.data() + 12 + 16 * i;
        if (std::memcmp(entry, tag, 4) == 0) {
            const std::size_t offset = be32(entry + 8);
            return offset + be32(entry + 12) <= font.size() ? font.data() + offset : nullptr;
        }
    }
    return nullptr;
}

std::vector<unsigned char> make(float line_divisor) {
    const std::span<const unsigned char> base = embedded_font_regular();
    const std::span<const unsigned char> music = embedded_font_music();
    std::vector<unsigned char> out(music.begin(), music.end());
    std::vector<unsigned char> reference(base.begin(), base.end());
    const unsigned char* ref_hhea = table(reference, "hhea");
    unsigned char* hhea = table(out, "hhea");
    if (ref_hhea == nullptr || hhea == nullptr) {
        return out;
    }
    // Both fonts have 1000 units to the em, so the numbers carry over as they are.
    const int ascent = static_cast<int>(std::lround(be16(ref_hhea + 4) / line_divisor));
    const int descent = static_cast<int>(std::lround(be16(ref_hhea + 6) / line_divisor));
    put16(hhea + 4, ascent);
    put16(hhea + 6, descent);
    put16(hhea + 8, 0);
    if (unsigned char* os2 = table(out, "OS/2")) {
        put16(os2 + 68, ascent);   // sTypoAscender
        put16(os2 + 70, descent);  // sTypoDescender
        put16(os2 + 72, 0);        // sTypoLineGap
        put16(os2 + 74, ascent);   // usWinAscent
        put16(os2 + 76, -descent); // usWinDescent
    }
    return out;
}

} // namespace

std::span<const unsigned char> music_font() {
    static const std::vector<unsigned char> font = make(1.0f);
    return font;
}

std::span<const unsigned char> music_font_for_em() {
    static const std::vector<unsigned char> font = make(kSignScale);
    return font;
}

} // namespace etude::app
