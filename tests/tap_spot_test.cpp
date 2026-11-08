// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/tap_spot.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

bool inside(const RectF& r, SpotF p) {
    return p.x >= r.x && p.x <= r.right() && p.y >= r.y && p.y <= r.bottom();
}

} // namespace

TEST_CASE("a tap outside the tempo popover: the point is clear of the card at every size and scale") {
    constexpr float kSizes[][2] = {{800, 480}, {1024, 600}, {1366, 768}, {1920, 1080}};
    constexpr float kScales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    for (const auto& size : kSizes) {
        for (const float u : kScales) {
            CAPTURE(size[0]);
            CAPTURE(u);
            // The card as the tempo popover lays it out: at most 360 pt wide, centred on the
            // chip, from just under the top row down to the key bed.
            const float w = 360.0f * u;
            const float top = 57.0f * u;
            const RectF card{(size[0] - w) * 0.5f + 60.0f * u, top + 4.0f * u, w,
                             size[1] * 0.5f};
            const TapSpot t = tap_spot_outside(card, size[0], size[1], top, 4.0f * u);
            REQUIRE(t.found);
            CHECK_FALSE(inside(card, t.at));
            CHECK(t.at.y > top);
            CHECK(t.at.x > 0.0f);
            CHECK(t.at.x < size[0]);
            CHECK(t.at.y < size[1]);
        }
    }
}

TEST_CASE("a tap outside: the old fixed point (the window's middle) lay inside the card at 800x480") {
    const RectF card{351.0f, 117.0f, 336.0f, 188.0f}; // measured on a real popover
    CHECK(inside(card, {400.0f, 192.0f}));
    const TapSpot t = tap_spot_outside(card, 800.0f, 480.0f, 57.0f, 4.0f);
    REQUIRE(t.found);
    CHECK_FALSE(inside(card, t.at));
}

TEST_CASE("a tap outside: no spot when the card fills the area under the top row") {
    const RectF card{0.0f, 57.0f, 800.0f, 423.0f};
    CHECK_FALSE(tap_spot_outside(card, 800.0f, 480.0f, 57.0f, 4.0f).found);
}
