// SPDX-License-Identifier: GPL-3.0-or-later
// The Melodies page's layout (#375) at the sizes the app runs in: the header, a section for each
// tutorial path, the tiles, and what is in a tile. 12 mm hits with 2 mm gaps, the stars on the
// bottom row between the hands and Play at every size.
#include "ui/learn_melodies_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;
namespace mt = etude::app::ui::melody_tok;

namespace {

struct Window {
    float w;
    float h;
    float u;
};

// The 7 inch panel's 800x480 as the app draws it at the scale 1.0 and at the 154x86 mm panel's
// (96 dpi against 132 dpi), then the usual screens at their unit.
constexpr std::array<Window, 6> kWindows{{{800, 480, 1.0f},
                                          {800, 480, 1.375f},
                                          {1024, 600, 1.0f},
                                          {1366, 768, 1.0f},
                                          {1920, 1080, 1.4f},
                                          {1920, 1080, 1.0f}}};
constexpr float kEps = 1e-3f;

Box area_for(const Window& s) { return {0.0f, 57.0f * s.u, s.w, s.h}; }

bool inside(const Box& inner, const Box& outer) {
    return inner.x0 >= outer.x0 - kEps && inner.y0 >= outer.y0 - kEps && inner.x1 <= outer.x1 + kEps &&
           inner.y1 <= outer.y1 + kEps;
}

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

bool apart(const Box& a, const Box& b, float u) {
    const float dx = std::max(a.x0 - b.x1, b.x0 - a.x1);
    const float dy = std::max(a.y0 - b.y1, b.y0 - a.y1);
    return std::max(dx, dy) >= tok::kHitGap * u - kEps;
}

bool empty(const Box& b) { return b.w() <= 0.0f || b.h() <= 0.0f; }

// The bundled set: three paths of six, four and four pieces.
MelodyNeeds bundled() {
    MelodyNeeds n;
    n.paths = 3;
    n.pieces[0] = 6;
    n.pieces[1] = 4;
    n.pieces[2] = 4;
    return n;
}

} // namespace

TEST_CASE("melodies layout: the header's parts are a hit across and apart, at every size") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const MelodiesLayout m = melodies_layout(area, s.u, bundled(), 0.0f);
        CHECK(inside(m.header, area));
        CHECK(m.back.w() >= tok::kHit * s.u - kEps);
        CHECK(m.back.h() >= tok::kHit * s.u - kEps);
        CHECK_FALSE(overlap(m.title, m.back));
        for (std::size_t i = 0; i < m.tabs.size(); ++i) {
            CHECK(inside(m.tabs[i], m.header));
            CHECK(m.tabs[i].w() >= tok::kHit * s.u - kEps);
            CHECK_FALSE(overlap(m.tabs[i], m.title));
            CHECK_FALSE(overlap(m.tabs[i], m.back));
            if (i > 0) {
                CHECK(apart(m.tabs[i - 1], m.tabs[i], s.u));
            }
        }
        // Under the header, a dead gap away from it.
        CHECK(m.body.y0 >= m.header.y1 + tok::kHitGap * s.u - kEps);
    }
}

TEST_CASE("melodies layout: a path's header row and tiles keep 12 mm hits and 2 mm gaps") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const MelodiesLayout m = melodies_layout(area, s.u, bundled(), 0.0f);
        REQUIRE(m.sections == 3);
        CHECK(m.tile_w >= mt::kTileMinW * s.u - kEps);
        CHECK(m.tile_h >= mt::kTileMinH * s.u - kEps);
        CHECK(m.tile_w >= tok::kHit * s.u);
        CHECK(m.tile_h >= tok::kHit * s.u);
        for (int i = 0; i < m.sections; ++i) {
            CAPTURE(i);
            const MelodySectionBoxes& sec = m.section[static_cast<std::size_t>(i)];
            CHECK(inside(sec.whole, {area.x0, area.y0 - m.max_scroll, area.x1, area.y1 + m.page_h}));
            CHECK(sec.button.h() >= tok::kHit * s.u - kEps);
            CHECK(sec.button.w() >= tok::kHit * s.u - kEps);
            // The header's parts, left to right, none over another.
            CHECK_FALSE(overlap(sec.icon, sec.title));
            CHECK_FALSE(overlap(sec.title, sec.button));
            if (!empty(sec.next)) {
                CHECK_FALSE(overlap(sec.title, sec.next));
                CHECK_FALSE(overlap(sec.next, sec.button));
                CHECK(sec.next.w() >= mt::kNextMinW * s.u - kEps);
            }
            // The tiles' row a dead gap under the button, and the next section a gap under the row.
            CHECK(apart(sec.button, sec.row, s.u));
            CHECK(sec.row.h() == doctest::Approx(m.tile_h));
            if (i > 0) {
                const MelodySectionBoxes& before = m.section[static_cast<std::size_t>(i - 1)];
                CHECK(apart(before.row, sec.button, s.u));
                CHECK(sec.whole.y0 > before.whole.y1);
            }
            // The row's tiles are apart from each other by the dead gap.
            const int pieces = bundled().pieces[static_cast<std::size_t>(i)];
            for (int p = 1; p < pieces; ++p) {
                const Box a = melody_tile_at(sec.row, p - 1, m.tile_w, m.tile_h, s.u, 0.0f);
                const Box b = melody_tile_at(sec.row, p, m.tile_w, m.tile_h, s.u, 0.0f);
                CHECK(apart(a, b, s.u));
            }
        }
    }
}

TEST_CASE("melodies layout: six tiles fit across from 1366 wide; a narrower window scrolls the row "
          "sideways and shows four whole at 800") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.u);
        const MelodiesLayout m = melodies_layout(area_for(s), s.u, bundled(), 0.0f);
        const MelodySectionBoxes& first = m.section[0];
        const float across = s.w - 2.0f * learn_tok::kPad * s.u;
        if (s.w / s.u >= 1360.0f) {
            CHECK_FALSE(first.scrolls);
            CHECK(first.content_w <= across + kEps);
            CHECK(first.row.x1 <= s.w - learn_tok::kPad * s.u + kEps);
        } else {
            CHECK(first.scrolls);
            // The row runs to the screen's edge, so the cut tile shows there is more.
            CHECK(first.row.x1 == doctest::Approx(s.w));
        }
        if (s.w <= 800.0f && s.u <= 1.0f) {
            // Four whole tiles and the fifth cut at the edge.
            const Box fourth = melody_tile_at(first.row, 3, m.tile_w, m.tile_h, s.u, 0.0f);
            const Box fifth = melody_tile_at(first.row, 4, m.tile_w, m.tile_h, s.u, 0.0f);
            CHECK(fourth.x1 <= first.row.x1 + kEps);
            CHECK(fifth.x1 > first.row.x1);
        }
        // The shorter paths have four tiles and never scroll where six do not.
        if (!first.scrolls) {
            CHECK_FALSE(m.section[1].scrolls);
        }
    }
}

TEST_CASE("melodies layout: the page fits from 1366x768 and scrolls down on a shorter screen") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const MelodiesLayout m = melodies_layout(area, s.u, bundled(), 0.0f);
        if (s.h / s.u >= 768.0f) {
            CHECK(m.max_scroll == doctest::Approx(0.0f));
            CHECK(m.section[2].row.y1 <= area.y1 + kEps);
            CHECK(m.goal); // the goal line is shown where the tiles are tall
        } else if (s.h / s.u <= 480.0f) {
            CHECK(m.max_scroll > 0.0f);
            CHECK_FALSE(m.goal);
        }
        // Scrolled all the way, the last row ends inside the screen; further changes nothing.
        const MelodiesLayout end = melodies_layout(area, s.u, bundled(), 1e6f);
        CHECK(end.scroll == doctest::Approx(m.max_scroll));
        CHECK(end.section[2].row.y1 <= area.y1 + kEps);
        // And a scroll moves every section up by as much.
        const MelodiesLayout some = melodies_layout(area, s.u, bundled(), 10.0f);
        if (m.max_scroll >= 10.0f) {
            CHECK(some.section[0].whole.y0 == doctest::Approx(m.section[0].whole.y0 - 10.0f));
            CHECK(some.section[2].row.y0 == doctest::Approx(m.section[2].row.y0 - 10.0f));
        }
        CHECK(melodies_layout(area, s.u, bundled(), -5.0f).scroll == doctest::Approx(0.0f));
    }
}

TEST_CASE("melodies tile: the stars sit on the bottom row between the hands and Play, at every size and "
          "for every hand") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.u);
        const MelodiesLayout m = melodies_layout(area_for(s), s.u, bundled(), 0.0f);
        for (const bool goal : {false, true}) {
            for (const float h : {m.tile_h, mt::kTileMinH * s.u, mt::kTileGoalH * s.u, mt::kTileMaxH * s.u}) {
                for (const int hands : {1, 2, 3}) {
                    CAPTURE(goal);
                    CAPTURE(h);
                    CAPTURE(hands);
                    const Box tile{100.0f, 100.0f, 100.0f + m.tile_w, 100.0f + h};
                    const bool tall = goal && h >= mt::kTileGoalH * s.u;
                    const MelodyTileBoxes t = melody_tile_boxes(tile, tall, hands, s.u);
                    CHECK(inside(t.badge, tile));
                    CHECK(inside(t.title, tile));
                    CHECK(inside(t.stars, tile));
                    CHECK(inside(t.play, tile));
                    // The bottom row: the hands, then the stars, then Play, left to right.
                    Box last_hand{};
                    for (const Box& hand : {t.hand_left, t.hand_right}) {
                        if (empty(hand)) {
                            continue;
                        }
                        CHECK(inside(hand, tile));
                        CHECK_FALSE(overlap(hand, t.stars));
                        CHECK_FALSE(overlap(hand, t.play));
                        CHECK(hand.x1 <= t.stars.x0 + kEps);
                        last_hand = hand;
                    }
                    CHECK(t.stars.x1 <= t.play.x0 + kEps);
                    CHECK(t.stars.cy() == doctest::Approx(t.play.cy()).epsilon(0.2));
                    CHECK(t.stars.cy() > t.title.y1 - kEps);
                    // Nothing of the top overlaps the bottom row.
                    for (const Box& low : {t.hand_left, t.hand_right, t.stars, t.play}) {
                        if (!empty(low)) {
                            CHECK_FALSE(overlap(low, t.badge));
                            CHECK_FALSE(overlap(low, t.title));
                            CHECK_FALSE(overlap(low, t.goal));
                        }
                    }
                    CHECK_FALSE(overlap(t.badge, t.title));
                    if (tall) {
                        CHECK(t.goal.y0 >= t.title.y1);
                        CHECK(inside(t.goal, tile));
                    } else {
                        CHECK(empty(t.goal));
                    }
                    (void)last_hand;
                }
            }
        }
    }
}

TEST_CASE("melodies tile: a right-hand piece shows its hand where the first hand of a two-hand piece "
          "goes") {
    const Box tile{0.0f, 0.0f, 200.0f, 120.0f};
    const MelodyTileBoxes one = melody_tile_boxes(tile, false, 2, 1.0f);
    const MelodyTileBoxes both = melody_tile_boxes(tile, false, 3, 1.0f);
    CHECK(empty(one.hand_left));
    CHECK_FALSE(empty(one.hand_right));
    CHECK(one.hand_right.x0 == doctest::Approx(both.hand_left.x0));
    CHECK(both.hand_right.x0 > both.hand_left.x1 - kEps);
    // The same tile has the same Play and stars place in every row of a path, whatever the hands.
    CHECK(one.play.x0 == doctest::Approx(both.play.x0));
}

TEST_CASE("melodies tile: the tile is the pressable, so every tile is a hit across") {
    for (const Window& s : kWindows) {
        const MelodiesLayout m = melodies_layout(area_for(s), s.u, bundled(), 0.0f);
        CAPTURE(s.w);
        CHECK(m.tile_w >= tok::kHit * s.u);
        CHECK(m.tile_h >= tok::kHit * s.u);
    }
}

TEST_CASE("melodies layout: a path with no pieces, and no paths, lay out without a tile") {
    MelodyNeeds none;
    const MelodiesLayout empty_page = melodies_layout(area_for({800, 480, 1.0f}), 1.0f, none, 0.0f);
    CHECK(empty_page.sections == 0);
    CHECK(empty_page.max_scroll == doctest::Approx(0.0f));
    MelodyNeeds one;
    one.paths = 1;
    one.pieces[0] = 1;
    const MelodiesLayout single = melodies_layout(area_for({1366, 768, 1.0f}), 1.0f, one, 0.0f);
    CHECK(single.sections == 1);
    CHECK_FALSE(single.section[0].scrolls);
    CHECK(single.tile_w <= mt::kTileMaxW + kEps);
}

TEST_CASE("melodies layout: the library's section sits under the last path, a hit across and a gap "
          "apart, and the page scrolls to it") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        MelodyNeeds needs = bundled();
        const MelodiesLayout without = melodies_layout(area, s.u, needs, 0.0f);
        CHECK_FALSE(without.has_library);
        needs.library = 12;
        const MelodiesLayout m = melodies_layout(area, s.u, needs, 0.0f);
        REQUIRE(m.has_library);
        const MelodySectionBoxes& lib = m.library;
        const MelodySectionBoxes& last = m.section[2];
        // The paths' tiles are the same size with the row there; the page is longer by the section.
        CHECK(m.tile_w == doctest::Approx(without.tile_w));
        CHECK(m.tile_h == doctest::Approx(without.tile_h));
        CHECK(m.page_h > without.page_h);
        CHECK(lib.whole.y0 > last.whole.y1);
        CHECK(apart(last.row, lib.button, s.u));
        CHECK(lib.button.w() >= tok::kHit * s.u - kEps);
        CHECK(lib.button.h() >= tok::kHit * s.u - kEps);
        CHECK_FALSE(overlap(lib.icon, lib.title));
        CHECK_FALSE(overlap(lib.title, lib.button));
        if (!empty(lib.next)) {
            CHECK_FALSE(overlap(lib.title, lib.next));
            CHECK_FALSE(overlap(lib.next, lib.button));
        }
        CHECK(apart(lib.button, lib.row, s.u));
        CHECK(lib.row.h() == doctest::Approx(m.tile_h));
        for (int p = 1; p < needs.library; ++p) {
            const Box a = melody_tile_at(lib.row, p - 1, m.tile_w, m.tile_h, s.u, 0.0f);
            const Box b = melody_tile_at(lib.row, p, m.tile_w, m.tile_h, s.u, 0.0f);
            CHECK(apart(a, b, s.u));
        }
        // Twelve tiles never fit across: the row scrolls sideways, and runs to the screen's edge.
        CHECK(lib.scrolls);
        CHECK(lib.row.x1 == doctest::Approx(s.w));
        // Scrolled all the way down, the library's row ends inside the screen.
        const MelodiesLayout end = melodies_layout(area, s.u, needs, 1e6f);
        CHECK(end.library.row.y1 <= area.y1 + kEps);
        CHECK(end.scroll == doctest::Approx(m.max_scroll));
    }
}

TEST_CASE("melodies layout: a library with no song to show is its header alone, and none is no section") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        const Box area = area_for(s);
        MelodyNeeds needs = bundled();
        needs.library = 0;
        const MelodiesLayout m = melodies_layout(area, s.u, needs, 0.0f);
        REQUIRE(m.has_library);
        CHECK(m.library.row.h() == doctest::Approx(0.0f));
        CHECK_FALSE(m.library.scrolls);
        const MelodiesLayout end = melodies_layout(area, s.u, needs, 1e6f);
        CHECK(end.library.whole.y1 <= area.y1 + kEps);
        needs.library = -1;
        CHECK_FALSE(melodies_layout(area, s.u, needs, 0.0f).has_library);
    }
}

TEST_CASE("melodies tile: a library song's tile keeps the stars on the bottom row, and its mark never "
          "touches the title") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.u);
        MelodyNeeds needs = bundled();
        needs.library = 5;
        const MelodiesLayout m = melodies_layout(area_for(s), s.u, needs, 0.0f);
        for (const bool goal : {false, true}) {
            for (const float h : {m.tile_h, mt::kTileMinH * s.u, mt::kTileGoalH * s.u}) {
                for (const int hands : {2, 3}) {
                    CAPTURE(goal);
                    CAPTURE(h);
                    const Box tile{100.0f, 100.0f, 100.0f + m.tile_w, 100.0f + h};
                    const bool tall = goal && h >= mt::kTileGoalH * s.u;
                    const MelodyTileBoxes t = melody_tile_boxes(tile, tall, hands, s.u, true);
                    const MelodyTileBoxes path = melody_tile_boxes(tile, tall, hands, s.u, false);
                    // Same bottom row as a path tile.
                    CHECK(t.stars.x0 == doctest::Approx(path.stars.x0));
                    CHECK(t.play.x0 == doctest::Approx(path.play.x0));
                    CHECK(t.stars.cy() == doctest::Approx(t.play.cy()).epsilon(0.2));
                    if (tall) {
                        // The goal line carries the mark: none in the corner, the title as wide as a path's.
                        CHECK(empty(t.mark));
                        CHECK(t.title.w() == doctest::Approx(path.title.w()));
                    } else {
                        CHECK(inside(t.mark, tile));
                        CHECK_FALSE(overlap(t.mark, t.title));
                        CHECK_FALSE(overlap(t.mark, t.badge));
                        CHECK(t.title.w() > 0.0f);
                        CHECK(t.mark.x0 >= t.title.x1 - kEps);
                    }
                }
            }
        }
    }
}
