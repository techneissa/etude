// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/level_stars.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("level stars: the ramp runs green, yellow, orange, red through its stops") {
    CHECK(level_star_colour(1.0) == level_tok::kEasy);
    CHECK(level_star_colour(2.5) == level_tok::kMiddle);
    CHECK(level_star_colour(3.5) == level_tok::kHarder);
    CHECK(level_star_colour(5.0) == level_tok::kHard);
    CHECK(level_star_colour(0.0) == level_tok::kEasy); // outside 1..5: the ends
    CHECK(level_star_colour(9.0) == level_tok::kHard);
    // Smooth: half a star moves the colour a little, not to the next stop.
    const RGBA a = level_star_colour(3.0);
    CHECK(a.r > level_tok::kMiddle.r - 1);
    CHECK(a.g < level_tok::kMiddle.g);
    CHECK(a.g > level_tok::kHarder.g);
}

TEST_CASE("level stars: every level reads on the card, and more so with High contrast") {
    for (double s = 1.0; s <= 5.0; s += 0.5) {
        CAPTURE(s);
        CHECK(contrast_ratio(level_star_colour(s), kLevelStarSurface) >= kLevelStarMinContrast);
        CHECK(contrast_ratio(level_star_colour(s, true), kLevelStarSurface) >=
              kLevelStarHighContrastMinContrast);
    }
    // High contrast keeps the ramp's order of hue: the easy end stays greener than the hard one.
    const RGBA easy = level_star_colour(1.0, true);
    const RGBA hard = level_star_colour(5.0, true);
    CHECK(easy.g > easy.r);
    CHECK(hard.r > hard.g);
}

TEST_CASE("level stars: half stars fill the stars in order") {
    CHECK(level_star_fill(2, 0) == 1.0);
    CHECK(level_star_fill(2, 1) == 0.0);
    CHECK(level_star_fill(5, 0) == 1.0);
    CHECK(level_star_fill(5, 1) == 1.0);
    CHECK(level_star_fill(5, 2) == 0.5);
    CHECK(level_star_fill(5, 3) == 0.0);
    CHECK(level_star_fill(10, 4) == 1.0);
    double total = 0.0;
    for (int i = 0; i < kLevelStars; ++i) {
        total += level_star_fill(7, i);
    }
    CHECK(total == 3.5);
}
