// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/screen_layout.hpp"

#include <limits>

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("the minimum name size is 9 px at 96 dpi and grows with the density") {
    CHECK(kMinNameFontPx == 9.0f);
    CHECK(min_name_font_px(kDefaultPxPerMm) == 9.0f);
    CHECK(min_name_font_px(2.0f * kDefaultPxPerMm) == 18.0f);
    CHECK(min_name_font_px(0.0f) == 9.0f);
    CHECK(min_name_font_px(-3.0f) == 9.0f);
    CHECK(min_name_font_px(std::numeric_limits<float>::quiet_NaN()) == 9.0f);
    CHECK(min_name_font_px(1000.0f) < 100.0f); // a silly density is capped
}

TEST_CASE(
    "the small minimum is 7 px at 96 dpi, below the legible one, and grows with the density") {
    CHECK(kMinSmallNameFontPx == 7.0f);
    CHECK(kMinSmallNameFontPx < kMinNameFontPx);
    CHECK(min_small_name_font_px(kDefaultPxPerMm) == 7.0f);
    CHECK(min_small_name_font_px(2.0f * kDefaultPxPerMm) == 14.0f);
    CHECK(min_small_name_font_px(0.0f) == 7.0f);
    CHECK(min_small_name_font_px(-3.0f) == 7.0f);
    CHECK(min_small_name_font_px(std::numeric_limits<float>::quiet_NaN()) == 7.0f);
    for (float ppmm : {1.0f, kDefaultPxPerMm, 5.0f, 7.5f, 1000.0f}) {
        CHECK(min_small_name_font_px(ppmm) <= min_name_font_px(ppmm));
    }
}
