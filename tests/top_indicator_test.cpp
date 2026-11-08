// SPDX-License-Identifier: GPL-3.0-or-later
// The count-in and the resume countdown show in the top row's status area and never over the
// notes: the indicator's rect stays inside the area and so inside the top row, which never meets
// the note area, the chord label, the pinned bar's band or the keyboard.
#include "etude/screen_layout.hpp"
#include "etude/top_indicator.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace etude;

namespace {

bool intersects(const RectF& a, const RectF& b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool inside(const RectF& r, const RectF& outer) {
    return r.x >= outer.x && r.y >= outer.y && r.right() <= outer.right() &&
           r.bottom() <= outer.bottom();
}

struct Size {
    float w;
    float h;
};
constexpr Size kSizes[] = {{800, 480}, {1366, 768}, {1920, 1080}};

} // namespace

TEST_CASE("top indicator: it never meets the note area, the bar's band or the keyboard") {
    for (const Size& size : kSizes) {
        for (const bool pinned : {false, true}) {
            for (const BarStyle style : {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock}) {
                PinnedBar pb;
                pb.pinned = pinned;
                pb.style = style;
                pb.has_song = true;
                const ScreenLayout lay =
                    compute_layout(size.w, size.h, kDefaultPxPerMm, LookSettings{}, pb);
                CAPTURE(size.w);
                CAPTURE(pinned);
                REQUIRE(lay.status_visible);
                // The bar hidden (the whole status area) and open (what a 480 px bar leaves).
                const TopRowSplit open = split_top_row(lay, 480.0f, 100.0f, style == BarStyle::Dock);
                for (const RectF& area : {lay.status_text, open.status_text}) {
                    if (area.empty()) {
                        continue;
                    }
                    for (const int dots : {0, 2, 3, 4, 5, kMaxIndicatorDots, kMaxIndicatorDots + 1}) {
                        CAPTURE(dots);
                        const TopIndicator t = place_top_indicator(area, lay.status_font_px, dots);
                        if (!t.visible()) {
                            continue;
                        }
                        CHECK(inside(t.rect, area));
                        CHECK(inside(t.rect, lay.top_row));
                        CHECK_FALSE(intersects(t.rect, lay.note_area));
                        CHECK_FALSE(intersects(t.rect, lay.chord_label));
                        CHECK_FALSE(intersects(t.rect, lay.bar_under));
                        CHECK_FALSE(intersects(t.rect, lay.keyboard));
                        CHECK_FALSE(intersects(t.rect, lay.gear));
                        CHECK(inside(t.number, t.rect));
                    }
                }
                // A hidden bar's status area holds a 4/4 count-in and the countdown at every size.
                CHECK(place_top_indicator(lay.status_text, lay.status_font_px, 4).visible());
                CHECK(place_top_indicator(lay.status_text, lay.status_font_px, 0).visible());
            }
        }
    }
}

TEST_CASE("top indicator: it sits against the right end, one dot per beat, the number after") {
    const RectF area{10.0f, 0.0f, 400.0f, 48.0f};
    const TopIndicator t = place_top_indicator(area, 16.0f, 4);
    REQUIRE(t.visible());
    CHECK(t.rect.right() == doctest::Approx(area.right()));
    CHECK(t.rect.w == doctest::Approx(top_indicator_width(16.0f, 4)));
    CHECK(t.number.right() == doctest::Approx(t.rect.right()));
    CHECK(t.number.x >= t.rect.x + 4.0f * t.dot + 3.0f * t.dot_gap); // after the dots
    // Two beats (6/8) are narrower than four (4/4).
    CHECK(top_indicator_width(16.0f, 2) < top_indicator_width(16.0f, 4));
    // Centred vertically in the row.
    CHECK(t.rect.y + t.rect.h * 0.5f == doctest::Approx(24.0f).epsilon(0.03));
}

TEST_CASE("top indicator: no room shows nothing, exactly enough shows it") {
    const float font = 16.0f;
    const float w = top_indicator_width(font, 4);
    CHECK(place_top_indicator({0.0f, 0.0f, w, 40.0f}, font, 4).visible()); // exactly at
    CHECK_FALSE(place_top_indicator({0.0f, 0.0f, w - 1.0f, 40.0f}, font, 4).visible()); // just under
    CHECK_FALSE(place_top_indicator({0.0f, 0.0f, 400.0f, 10.0f}, font, 0).visible()); // ring too tall
    CHECK_FALSE(place_top_indicator({}, font, 4).visible());
    CHECK_FALSE(place_top_indicator({0.0f, 0.0f, 400.0f, 40.0f}, 0.0f, 4).visible());
    const float nan = static_cast<float>(std::nan(""));
    CHECK_FALSE(place_top_indicator({0.0f, 0.0f, 400.0f, 40.0f}, nan, 4).visible());
    CHECK_FALSE(place_top_indicator({nan, 0.0f, 400.0f, 40.0f}, font, 4).visible());
    CHECK(top_indicator_width(nan, 4) == 0.0f);
}
