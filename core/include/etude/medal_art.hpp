// SPDX-License-Identifier: GPL-3.0-or-later
// The medals of the achievements, drawn into pixels: a ribbon and a hanger, a raised rim and an
// embossed face, and a finish for each of the eight materials (wood grain, forged iron, bronze,
// silver, gold, platinum, faceted diamond, glossy obsidian) lit from the top left, with a shadow.
// Pure CPU code with no clock and no randomness: the same pixels every time, so the app can draw each
// medal once into a texture atlas and never again (the Pi has no frame to spare), and the tests can
// look at them. The badge's own icon is not here: the app lays it on the face, embossed, with the
// colours of `MedalLook`.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace etude {

enum class Material : std::uint8_t { Wood, Iron, Bronze, Silver, Gold, Platinum, Diamond, Obsidian };
inline constexpr std::size_t kMaterialCount = 8;

// Large: the whole medal, ribbon and hanger above the disc. Small: the disc alone, simplified (the
// Household row's size), with the texture kept coarse.
enum class MedalSize : std::uint8_t { Large, Small };

// The canvas of each size in pixels, and where the disc sits on it.
struct MedalFrame {
    int width = 0;
    int height = 0;
    float centre_x = 0.0f; // the disc's centre
    float centre_y = 0.0f;
    float radius = 0.0f;   // the disc's outer radius
    float face = 0.0f;     // the embossed face's radius: the icon goes inside it
};

MedalFrame medal_frame(MedalSize size);

// Straight (not premultiplied) RGBA8, row by row from the top; outside the medal and its shadow the
// alpha is 0 and the colour is the nearest medal colour, so a linear filter never fringes.
struct MedalPixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

MedalPixels render_medal(Material material, MedalSize size);

// What the icon on the face is drawn with, as 0xRRGGBB: its fill, the light edge of its emboss (up
// and left) and the dark one (down and right).
struct MedalLook {
    std::uint32_t icon_fill = 0;
    std::uint32_t emboss_light = 0;
    std::uint32_t emboss_dark = 0;
    std::uint32_t ribbon = 0; // the ribbon's colour, for anything drawn to match it
};

MedalLook medal_look(Material material);

} // namespace etude
