// SPDX-License-Identifier: GPL-3.0-or-later
// The library's search strip: on a short screen (the 7 inch 800x480 panel) with the keyboard up, the
// search row holds the field and, beside it, the results as one compact row a hit tall with the
// level's stars before the heart (library_layout.hpp, `LibraryFrame::strip`).
#include "ui/library_layout.hpp"
#include "ui/osk_layout.hpp"

#include "etude/look.hpp"
#include "etude/panel_size.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;

namespace {

struct Size {
    float w;
    float h;
};
constexpr float kEps = 1e-3f;

// The screen under a top row as tall as a hit.
Box area_for(Size s, float u) { return {0.0f, tok::kHit * u, s.w, s.h}; }

bool inside(const Box& inner, const Box& outer) {
    return inner.x0 >= outer.x0 - kEps && inner.y0 >= outer.y0 - kEps &&
           inner.x1 <= outer.x1 + kEps && inner.y1 <= outer.y1 + kEps;
}

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

} // namespace

namespace {

// The 7 inch panel (154x86 mm, 800x480) as the app lays it out: its pixels per point and the screen
// under the top row, in the units the library is drawn in.
struct Panel7 {
    float u;
    Box area;
};
Panel7 seven_inch() {
    const float px_per_mm = etude::panel_px_per_mm(800.0f, 480.0f, {154.0f, 86.0f});
    const float u = etude::ui_scale(800.0f, 480.0f, px_per_mm);
    const etude::ScreenLayout lay =
        etude::compute_layout(800.0f, 480.0f, px_per_mm, etude::LookSettings{},
                                etude::PinnedBar{});
    return {u, {0.0f, lay.top_row.bottom(), 800.0f, 480.0f}};
}

// The least height of a key's hit area above which the keyboard's first row may sit: its cell less
// half the dead gap at the top.
float first_key_hit_top(const LibraryFrame& f, float u) {
    return keyboard_key(f.keyboard, u, Layer::Letters, 0, 0).y0 + osk_tok::kKeyGap * u * 0.5f;
}

} // namespace

TEST_CASE("library frame: on the 7 inch panel the results stay a hit tall beside the field while "
          "the keyboard is up") {
    const auto [u, area] = seven_inch();
    const float hit = tok::kHit * u;
    const float gap = tok::kHitGap * u;
    const LibraryFrame f = library_frame(area, u, 1.0f);
    REQUIRE(f.strip);
    // One row of results, a full hit tall and wide enough for a song's name and its buttons.
    CHECK(f.results.h() >= hit - kEps);
    CHECK(f.results.w() >= 6.0f * hit);
    CHECK(inside(f.results, area));
    CHECK(inside(f.search, area));
    // The field, the results and the keys keep a dead gap from each other.
    CHECK(f.results.x0 >= f.search.x1 + gap - kEps);
    CHECK(f.results.y1 + gap <= first_key_hit_top(f, u) + kEps);
    CHECK(f.search.y1 + gap <= first_key_hit_top(f, u) + kEps);
    CHECK(f.search.w() >= 4.0f * hit);
    // The keys keep their least size.
    CHECK(f.keyboard.h() >= keyboard_height(area.w(), u, 0.0f) - kEps);
    // The row the results stand in is the list's, so a song's row fits it: Play and the heart a hit
    // each, and the title keeps room.
    const ListRow row = list_row(f.results, u);
    CHECK(row.play.w() >= hit - kEps);
    CHECK(row.heart.w() >= hit - kEps);
    CHECK(row.text.w() > 0.0f);
    CHECK(inside(row.play, f.results));
    CHECK(inside(row.heart, f.results));
    CHECK_FALSE(overlap(row.play, row.heart));
    // The buttons beside the field wait for the keyboard to close.
    CHECK(f.get.w() == 0.0f);
    CHECK(f.select.w() == 0.0f);
    CHECK(f.view.w() == 0.0f);
    CHECK(f.chips.h() == 0.0f);
}

TEST_CASE("library frame: on the 7 inch panel the strip's row carries the level's stars before the "
          "heart, and the title shortens before they go") {
    const auto [u, area] = seven_inch();
    const float hit = tok::kHit * u;
    const float gap = tok::kHitGap * u;
    const LibraryFrame f = library_frame(area, u, 1.0f);
    REQUIRE(f.strip);
    // The row has the strip's whole width: no list padding.
    const ListRow row = list_row(f.results, u, 0.0f, true);
    REQUIRE(row.stars.w() > 0.0f);
    CHECK(row.stars.w() == doctest::Approx(kStarsW * u));
    CHECK(inside(row.stars, f.results));
    // Just before the heart, a gap clear of it, and the title in front of them.
    CHECK(row.stars.x1 == doctest::Approx(row.heart.x0 - gap));
    CHECK(row.text.x1 <= row.stars.x0 - gap + kEps);
    CHECK(row.text.w() >= kStripLeastTitleW * u - kEps);
    // The hit areas are what they were: a hit each, a gap apart.
    CHECK(row.heart.w() >= hit - kEps);
    CHECK(row.play.w() >= hit - kEps);
    CHECK(row.heart.x0 >= row.play.x0 - gap - hit - kEps);
    CHECK_FALSE(overlap(row.heart, row.play));
    CHECK(row.heart.x1 + gap <= row.play.x0 + kEps);
    // The stars are one row of five, a star's height, centred on the row.
    CHECK(row.stars.cy() == doctest::Approx(f.results.cy()));
    CHECK(row.stars.h() == doctest::Approx(2.0f * lib_tok::kLevelStar * u));
}

TEST_CASE("list row: in the strip the title shortens to its least width before the stars are "
          "dropped, and the stars are never in the title's or the heart's way") {
    const float u = 1.0f;
    float last_title = 1e9f;
    bool last_stars = true;
    bool dropped = false;
    for (float w = 380.0f; w >= 150.0f; w -= 2.0f) {
        const Box box{0.0f, 0.0f, w * u, tok::kHit * u};
        const ListRow row = list_row(box, u, 0.0f, true);
        CAPTURE(w);
        const bool stars = row.stars.w() > 0.0f;
        // Once dropped they stay dropped as the row narrows, and the title then has what they had.
        CHECK_FALSE((dropped && stars));
        if (stars) {
            CHECK(row.text.w() >= kStripLeastTitleW * u - kEps);
            CHECK(row.text.x1 <= row.stars.x0 - tok::kHitGap * u + kEps);
            CHECK(row.stars.x1 <= row.heart.x0 - tok::kHitGap * u + kEps);
        } else {
            dropped = true;
            // Dropped only where the title would be under its least with them.
            CHECK(row.text.w() - (kStarsW + tok::kHitGap) * u < kStripLeastTitleW * u + kEps);
        }
        // The title only shortens as the row does, within each stretch (it takes the stars' room
        // back at the width where they are dropped).
        if (stars == last_stars) {
            CHECK(row.text.w() <= last_title + kEps);
        }
        last_title = row.text.w();
        last_stars = stars;
    }
    CHECK(dropped);
    // Without `with_stars` (the list, the cards' neighbours) nothing changes.
    CHECK(list_row({0.0f, 0.0f, 400.0f, 64.0f}, u).stars.w() == 0.0f);
}

TEST_CASE("library frame: on the 7 inch panel the full screen comes back when the keyboard is down") {
    const auto [u, area] = seven_inch();
    const LibraryFrame f = library_frame(area, u, 0.0f);
    CHECK_FALSE(f.strip);
    CHECK(f.get.w() >= tok::kHit * u - kEps);
    CHECK(f.view.w() >= tok::kHit * u - kEps);
    CHECK(f.chips.h() >= tok::kHit * u - kEps);
    CHECK(f.results.y0 >= f.chips.y1 - kEps);
    CHECK(f.keyboard.h() == 0.0f);
}

TEST_CASE("library frame: on the 7 inch panel a pick of \"Same piece as another song\" keeps its way out and "
          "its results") {
    const auto [u, area] = seven_inch();
    const float hit = tok::kHit * u;
    const float gap = tok::kHitGap * u;
    const LibraryFrame f = library_frame(area, u, 1.0f, true);
    REQUIRE(f.strip);
    CHECK(f.view.w() >= hit - kEps);
    CHECK(f.view.h() >= hit - kEps);
    CHECK(inside(f.view, area));
    CHECK(f.results.x1 <= f.view.x0 - gap + kEps);
    CHECK(f.results.w() >= 4.0f * hit);
    CHECK(f.results.h() >= hit - kEps);
    // The instruction ("Tap the song" over "it joins") stands left of the way out, a dead gap from
    // it and from the results, inside the row.
    CHECK(f.hint.w() >= kStripHintW * u - kEps);
    CHECK(f.hint.h() >= hit - kEps);
    CHECK(inside(f.hint, area));
    CHECK(f.hint.x1 <= f.view.x0 - gap + kEps);
    CHECK(f.results.x1 <= f.hint.x0 - gap + kEps);
    CHECK_FALSE(overlap(f.hint, f.results));
    CHECK_FALSE(overlap(f.hint, f.view));
    CHECK_FALSE(overlap(f.hint, f.search));
    CHECK_FALSE(overlap(f.view, f.search));
    CHECK_FALSE(overlap(f.view, f.results));
    CHECK(f.results.y1 + gap <= first_key_hit_top(f, u) + kEps);
}

TEST_CASE("library frame: a tall screen never uses the strip, the keyboard leaves it the cards") {
    for (const Size s : {Size{1366, 768}, Size{1920, 1080}, Size{1280, 800}}) {
        const float u = 1.0f;
        const LibraryFrame f = library_frame(area_for(s, u), u, 1.0f);
        CHECK_FALSE(f.strip);
        CHECK(f.results.h() >= lib_tok::kCardH * lib_tok::kLeastResultsShare * u - kEps);
    }
}
