// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/color.hpp"

namespace etude {

// A song's level drawn as stars: five of them, in halves, coloured by the level. The count is the
// cue; the colour only adds to it, so nothing is told by colour alone.
//
// Colour: a smooth ramp through named stops, green at 1 star, yellow at 2.5, orange at 3.5 and red
// at 5, mixed in sRGB between them. Every stop reads at `kLevelStarMinContrast` (3:1, what a
// drawn shape needs) on the dark surface the cards sit on, and with High contrast on the colour is
// lifted in lightness, keeping its hue, to `kLevelStarHighContrastMinContrast` (4.5:1).
inline constexpr int kLevelStars = 5;
inline constexpr double kLevelStarMinContrast = 3.0;
inline constexpr double kLevelStarHighContrastMinContrast = 4.5;
// The surface the stars are measured on: the library card's face.
inline constexpr RGBA kLevelStarSurface{22, 25, 34, 255};

namespace level_tok {
inline constexpr RGBA kEasy{0x4C, 0xC9, 0x6B, 255};   // 1 star: green
inline constexpr RGBA kMiddle{0xF2, 0xD0, 0x3B, 255}; // 2.5: yellow
inline constexpr RGBA kHarder{0xFF, 0x96, 0x3C, 255}; // 3.5: orange
inline constexpr RGBA kHard{0xF2, 0x55, 0x5A, 255};   // 5: red
inline constexpr double kMiddleAt = 2.5;
inline constexpr double kHarderAt = 3.5;
} // namespace level_tok

// The colour for a level of `stars` (1 to 5; outside that the ends).
RGBA level_star_colour(double stars, bool high_contrast = false);

// How much of star `index` (0 to 4) a level of `half_stars` (2 to 10, half stars) fills: 0, 0.5
// or 1.
double level_star_fill(int half_stars, int index);

} // namespace etude
