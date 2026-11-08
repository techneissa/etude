// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/count_band.hpp"

#include <cmath>

#include <doctest/doctest.h>

using namespace etude;

namespace {

// A note area like the app's at three window sizes, at UI scale `unit`.
struct Area {
    RectF rect;
    float unit;
};

constexpr Area kAreas[] = {
    {{0.0f, 57.0f, 800.0f, 330.0f}, 1.0f},
    {{0.0f, 57.0f, 1366.0f, 575.0f}, 1.0f},
    {{0.0f, 80.0f, 1920.0f, 805.0f}, 1920.0f / 1366.0f},
};

bool inside(const RectF& a, const RectF& outer) {
    return a.x >= outer.x && a.y >= outer.y && a.right() <= outer.right() + 0.01f &&
           a.bottom() <= outer.bottom() + 0.01f;
}

} // namespace

TEST_CASE("count band: across the top of the note area, the number centred, the dots under it") {
    for (const Area& a : kAreas) {
        CAPTURE(a.rect.w);
        const CountBand b = place_count_band(a.rect, a.unit);
        REQUIRE(b.visible());
        CHECK(b.band.x == a.rect.x);
        CHECK(b.band.y == a.rect.y);
        CHECK(b.band.w == a.rect.w);
        CHECK(inside(b.band, a.rect));
        CHECK(b.number_cx == doctest::Approx(a.rect.x + a.rect.w * 0.5f));
        // The number and the dots inside the band, the dots below the number.
        CHECK(b.number_cy - b.font_px * 0.5f >= b.band.y);
        CHECK(b.dots_cy > b.number_cy + b.font_px * 0.5f);
        CHECK(b.dots_cy + b.dot_px * 0.5f <= b.band.bottom());
        // Large, within its limits and its share of the note area.
        CHECK(b.font_px >= kCountMinFontPt * a.unit - 1.0f);
        CHECK(b.font_px <= kCountMaxFontPt * a.unit);
        CHECK(b.band.h <= kCountBandMaxShare * a.rect.h + 1.0f);
        CHECK(b.dot_px == doctest::Approx(kCountDotPt * a.unit));
    }
}

TEST_CASE("count band: a taller note area gets a larger number, up to the most") {
    const CountBand small = place_count_band({0, 0, 1366, 400}, 1.0f);
    const CountBand tall = place_count_band({0, 0, 1366, 600}, 1.0f);
    const CountBand very = place_count_band({0, 0, 1366, 3000}, 1.0f);
    CHECK(tall.font_px > small.font_px);
    CHECK(very.font_px == doctest::Approx(kCountMaxFontPt));
}

TEST_CASE("count band: nothing when the share cannot hold the smallest number; bad input too") {
    // Just tall enough and a pixel short of it.
    float h = 100.0f;
    while (!place_count_band({0, 0, 800, h}, 1.0f).visible() && h < 1000.0f) {
        h += 1.0f;
    }
    CHECK(place_count_band({0, 0, 800, h}, 1.0f).font_px >= kCountMinFontPt);
    CHECK_FALSE(place_count_band({0, 0, 800, h - 1.0f}, 1.0f).visible());
    CHECK_FALSE(place_count_band({}, 1.0f).visible());
    CHECK_FALSE(place_count_band({0, 0, 800, 400}, 0.0f).visible());
    CHECK_FALSE(place_count_band({0, 0, 800, 400}, -1.0f).visible());
}

TEST_CASE("count band: the number and the dots start below a line kept clear (the toasts' slot)") {
    for (const Area& a : kAreas) {
        CAPTURE(a.rect.w);
        // A slot reaching 40 points into the note area.
        const float clear = a.rect.y + 40.0f * a.unit;
        const CountBand b = place_count_band(a.rect, a.unit, clear);
        REQUIRE(b.visible());
        // The band still starts at the area's top, so no block shows above the count either.
        CHECK(b.band.y == a.rect.y);
        CHECK(b.number_cy - b.font_px * 0.5f >= clear);
        CHECK(b.dots_cy + b.dot_px * 0.5f <= b.band.bottom());
        CHECK(inside(b.band, a.rect));
        // The same count as with nothing to keep clear, moved down by the slot's reach.
        const CountBand free = place_count_band(a.rect, a.unit);
        CHECK(b.font_px == free.font_px);
        CHECK(b.number_cy == doctest::Approx(free.number_cy + 40.0f * a.unit));
    }
    // A line above the area, or not a number, keeps nothing clear.
    const RectF r{0, 100, 1366, 575};
    CHECK(place_count_band(r, 1.0f, 50.0f).number_cy == place_count_band(r, 1.0f).number_cy);
    CHECK(place_count_band(r, 1.0f, std::nanf("")).number_cy == place_count_band(r, 1.0f).number_cy);
    // A line below the whole area leaves no room.
    CHECK_FALSE(place_count_band(r, 1.0f, r.bottom()).visible());
}
