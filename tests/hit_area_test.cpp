// SPDX-License-Identifier: GPL-3.0-or-later
// The hit-area rule: each area at least the minimum, clipped at the midpoint between neighbours,
// never over another's drawn part, with a dead gap between (core hit_area.hpp).
#include "etude/hit_area.hpp"

#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {
constexpr float kLeast = 12.0f;
constexpr float kGap = 2.0f;
} // namespace

TEST_CASE("hit area: a lone small face grows to the least, centred") {
    const std::vector<RectF> faces{{10.0f, 10.0f, 8.0f, 8.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    REQUIRE(a.size() == 1);
    CHECK(a[0].rect.x == 8.0f);
    CHECK(a[0].rect.y == 8.0f);
    CHECK(a[0].rect.w == 12.0f);
    CHECK(a[0].rect.h == 12.0f);
    CHECK(a[0].full);
}

TEST_CASE("hit area: a big face keeps its own size") {
    const std::vector<RectF> faces{{0.0f, 0.0f, 30.0f, 14.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    CHECK(a[0].rect.w == 30.0f);
    CHECK(a[0].rect.h == 14.0f);
}

TEST_CASE("hit area: far apart controls are left alone") {
    const std::vector<RectF> faces{{0.0f, 0.0f, 8.0f, 8.0f}, {30.0f, 0.0f, 8.0f, 8.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    CHECK(a[0].rect.w == 12.0f);
    CHECK(a[1].rect.w == 12.0f);
    CHECK(a[0].full);
    CHECK(a[1].full);
}

TEST_CASE("hit area: close neighbours are cut at the midpoint, a gap apart") {
    // Faces 8 wide, 4 apart: grown to 12 they would overlap by 4.
    const std::vector<RectF> faces{{0.0f, 0.0f, 8.0f, 8.0f}, {12.0f, 0.0f, 8.0f, 8.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    // The midpoint between the faces' facing edges (8 and 12) is 10; a gap of 2 leaves 9 and 11.
    CHECK(a[0].rect.right() == doctest::Approx(9.0f));
    CHECK(a[1].rect.x == doctest::Approx(11.0f));
    CHECK(hit_area_gap(a[0].rect, a[1].rect) == doctest::Approx(kGap));
    // The cut keeps each face whole but leaves each under the least: the layout must space them.
    CHECK_FALSE(a[0].full);
    CHECK_FALSE(a[1].full);
}

TEST_CASE("hit area: faces spaced a least plus a gap apart keep full areas") {
    // Centres 14 apart: 12 + 2. Their areas already keep the gap, so nothing is cut.
    const std::vector<RectF> faces{{0.0f, 0.0f, 8.0f, 8.0f}, {14.0f, 0.0f, 8.0f, 8.0f},
                                   {28.0f, 0.0f, 8.0f, 8.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    for (const HitArea& h : a) {
        CHECK(h.rect.w == doctest::Approx(12.0f));
        CHECK(h.full);
    }
    CHECK(hit_area_gap(a[0].rect, a[1].rect) >= kGap - 1e-3f);
    CHECK(hit_area_gap(a[1].rect, a[2].rect) >= kGap - 1e-3f);
}

TEST_CASE("hit area: stacked controls are cut across the other axis") {
    const std::vector<RectF> faces{{0.0f, 0.0f, 8.0f, 8.0f}, {0.0f, 12.0f, 8.0f, 8.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    CHECK(a[0].rect.bottom() == doctest::Approx(9.0f));
    CHECK(a[1].rect.y == doctest::Approx(11.0f));
    CHECK(a[0].rect.w == 12.0f); // across: not cut
    CHECK(hit_area_gap(a[0].rect, a[1].rect) == doctest::Approx(kGap));
}

TEST_CASE("hit area: an area never reaches over another control's drawn part") {
    // A wide neighbour 2 away: the small control's area stops before the neighbour's face.
    const std::vector<RectF> faces{{0.0f, 0.0f, 8.0f, 8.0f}, {10.0f, 0.0f, 40.0f, 8.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    CHECK(a[0].rect.right() <= faces[1].x);
    CHECK(a[1].rect.x >= faces[0].right());
}

TEST_CASE("hit area: faces on top of each other cannot be full") {
    const std::vector<RectF> faces{{0.0f, 0.0f, 8.0f, 8.0f}, {4.0f, 0.0f, 8.0f, 8.0f}};
    const auto a = clip_hit_areas(faces, kLeast, kGap);
    CHECK_FALSE(a[0].full);
    CHECK_FALSE(a[1].full);
}

TEST_CASE("hit area: the order of the controls does not change the result") {
    const std::vector<RectF> forward{{0.0f, 0.0f, 8.0f, 8.0f}, {14.0f, 0.0f, 8.0f, 8.0f}};
    const std::vector<RectF> reverse{{14.0f, 0.0f, 8.0f, 8.0f}, {0.0f, 0.0f, 8.0f, 8.0f}};
    const auto f = clip_hit_areas(forward, kLeast, kGap);
    const auto r = clip_hit_areas(reverse, kLeast, kGap);
    CHECK(f[0].rect.x == r[1].rect.x);
    CHECK(f[0].rect.right() == r[1].rect.right());
    CHECK(f[1].rect.x == r[0].rect.x);
}

TEST_CASE("hit area: the distance between two areas") {
    CHECK(hit_area_gap({0.0f, 0.0f, 10.0f, 10.0f}, {12.0f, 0.0f, 10.0f, 10.0f}) ==
          doctest::Approx(2.0f));
    CHECK(hit_area_gap({0.0f, 0.0f, 10.0f, 10.0f}, {0.0f, 13.0f, 10.0f, 10.0f}) ==
          doctest::Approx(3.0f));
    CHECK(hit_area_gap({0.0f, 0.0f, 10.0f, 10.0f}, {8.0f, 0.0f, 10.0f, 10.0f}) < 0.0f);
    CHECK(hit_area_gap({0.0f, 0.0f, 10.0f, 10.0f}, {8.0f, 6.0f, 10.0f, 10.0f}) ==
          doctest::Approx(-2.0f)); // overlapping in both: the smaller overlap
    CHECK(hit_area_gap({0.0f, 0.0f, 10.0f, 10.0f}, {13.0f, 14.0f, 10.0f, 10.0f}) ==
          doctest::Approx(5.0f)); // apart on both axes: corner to corner (3, 4)
    CHECK(hit_area_gap({0.0f, 0.0f, 10.0f, 10.0f}, {10.0f, 0.0f, 10.0f, 10.0f}) ==
          doctest::Approx(0.0f)); // touching
}
