// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the order list, the check-in and the summary go: every control 44 points, all
// inside the note area and clear of the keyboard, at every size and UI scale.
#include "ui/run_card_layout.hpp"

#include "etude/bar_order.hpp"
#include "etude/look.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct ScreenSize {
    float w, h;
};
constexpr ScreenSize kSizes[] = {{800, 480}, {1024, 600}, {1366, 768}, {1920, 1080}, {2560, 1440}};

ScreenLayout screen_of(const ScreenSize& s, float u, bool pinned) {
    return compute_layout(s.w, s.h, kDefaultPxPerMm * u, LookSettings{},
                          pinned ? PinnedBar{true, BarStyle::Groups, true} : PinnedBar{});
}

bool inside(const RectF& r, const RectF& area) {
    constexpr float kEps = 0.01f;
    return r.x >= area.x - kEps && r.y >= area.y - kEps && r.right() <= area.right() + kEps &&
           r.bottom() <= area.bottom() + kEps;
}

bool overlap(const RectF& a, const RectF& b) {
    constexpr float kEps = 0.01f; // rects that touch are not overlapping
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

// How many bars the card can hold at all: the compact form (800x480) three, the others six.
int bars_for(const ScreenSize& s) { return s.w <= 800.0f ? kCardCompactBars : kMaxRunBars; }

} // namespace

TEST_CASE("order layout: the rows are 44 points, in order, inside the card, clear of each other") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            const ScreenLayout lay = screen_of(sz, scale, false);
            const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm * scale);
            for (int count = 2; count <= bars_for(sz); ++count) {
                CAPTURE(sz.w);
                CAPTURE(scale);
                CAPTURE(count);
                const OrderLayout l = order_layout(lay.note_area, u, count);
                REQUIRE(l.count == count);
                // Only a small screen at a large UI scale may be too short, whatever is done.
                if (!l.fits) {
                    CHECK(sz.w <= 1024.0f);
                    CHECK(scale > 1.0f);
                    continue;
                }
                CHECK(inside(l.card, lay.note_area));
                CHECK_FALSE(overlap(l.card, lay.keyboard));
                CHECK_FALSE(overlap(l.card, lay.top_row));
                const float hit = tok::kHit * l.scale;
                std::vector<RectF> cells{l.back, l.start};
                for (int i = 0; i < count; ++i) {
                    const auto j = static_cast<std::size_t>(i);
                    CHECK(l.rows[j].h >= hit - 0.01f);
                    CHECK(l.ups[j].w >= hit - 0.01f);
                    CHECK(l.ups[j].h >= hit - 0.01f);
                    CHECK(l.downs[j].w >= hit - 0.01f);
                    CHECK(l.downs[j].h >= hit - 0.01f);
                    CHECK(l.grabs[j].h >= hit - 0.01f);
                    CHECK(inside(l.rows[j], l.card));
                    // The grab area and the two buttons make up the row, and never overlap.
                    CHECK_FALSE(overlap(l.grabs[j], l.ups[j]));
                    CHECK_FALSE(overlap(l.ups[j], l.downs[j]));
                    CHECK_FALSE(overlap(l.grabs[j], l.downs[j]));
                    CHECK(l.grabs[j].w > hit);
                    if (i > 0 && l.rows[j].x == l.rows[j - 1].x) {
                        CHECK(l.rows[j].y - l.rows[j - 1].y == doctest::Approx(l.pitch));
                    }
                    cells.push_back(l.grabs[j]);
                    cells.push_back(l.ups[j]);
                    cells.push_back(l.downs[j]);
                }
                for (const RectF& c : {l.back, l.start}) {
                    CHECK(c.w >= hit);
                    CHECK(c.h >= hit - 0.01f);
                    CHECK(inside(c, l.card));
                }
                for (std::size_t a = 0; a < cells.size(); ++a) {
                    for (std::size_t b = a + 1; b < cells.size(); ++b) {
                        CHECK_FALSE(overlap(cells[a], cells[b]));
                    }
                }
                // Nothing is dropped or shrunk: a short area gets two columns instead.
                CHECK((l.columns == 1 || l.columns == 2));
                CHECK(l.caption.right() <= l.back.x);
                CHECK(l.rows_top >= l.head.bottom());
            }
        }
    }
}

TEST_CASE("order layout: at the usual scale one column fits every size") {
    for (const ScreenSize& sz : kSizes) {
        const ScreenLayout lay = screen_of(sz, 1.0f, false);
        const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm);
        const OrderLayout l = order_layout(lay.note_area, u, bars_for(sz));
        CAPTURE(sz.w);
        CHECK(l.fits);
        CHECK(l.columns == 1);
    }
}

TEST_CASE("order layout: too short for six rows, they go in two columns, first half on the left") {
    const OrderLayout one = order_layout({0, 0, 1366, 600}, 1.0f, 6);
    const OrderLayout two = order_layout({0, 0, 1366, 300}, 1.0f, 6);
    CHECK(one.columns == 1);
    CHECK(two.columns == 2);
    CHECK(two.fits);
    CHECK(two.rows[0].x < two.rows[3].x);
    CHECK(two.rows[0].y == doctest::Approx(two.rows[3].y));
    CHECK(two.rows[2].y > two.rows[1].y);
    // The two columns share the width, so a row keeps room for its words and two 44 pt buttons.
    CHECK(two.grabs[0].w > tok::kHit);
}

TEST_CASE("order layout: no bars, or no room, gives no card and never crashes") {
    CHECK(order_layout({0, 0, 800, 300}, 1.0f, 0).card.empty());
    CHECK(order_layout({0, 0, 0, 0}, 1.0f, 3).card.empty());
    CHECK(order_layout({0, 0, 40, 40}, 1.0f, 3).card.empty());
    // More than the card names are clamped.
    CHECK(order_layout({0, 0, 1366, 600}, 1.0f, 40).count == kMaxCardBars);
}

TEST_CASE("order layout: the place under a pointer is the row it is over") {
    const ScreenLayout lay = screen_of(kSizes[2], 1.0f, false);
    const OrderLayout l = order_layout(lay.note_area, 1.0f, 5);
    const ReorderDrag::Rows rows{l.rows_top,    l.pitch,    l.count,     l.columns,
                                 l.column_left, l.column_w, l.column_gap};
    for (int i = 0; i < l.count; ++i) {
        const RectF& r = l.rows[static_cast<std::size_t>(i)];
        CHECK(place_at(rows, r.x + 5.0f, r.y + r.h * 0.5f) == i);
        CHECK(place_at(rows, r.x + 5.0f, r.y + 0.1f) == i);
    }
}

TEST_CASE("check-in layout: four buttons of 44 points, the figures, inside the note area") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.5f}) {
            for (const bool pinned : {false, true}) {
                CAPTURE(sz.w);
                CAPTURE(scale);
                CAPTURE(pinned);
                const ScreenLayout lay = screen_of(sz, scale, pinned);
                const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm * scale);
                const RunCardLayout l = run_card_layout(lay.note_area, u, RunCardKind::CheckIn, 4);
                REQUIRE(l.button_count == kCheckInButtons);
                CHECK(inside(l.card, lay.note_area));
                CHECK_FALSE(overlap(l.card, lay.keyboard));
                CHECK_FALSE(overlap(l.card, lay.top_row));
                CHECK_FALSE(overlap(l.card, lay.bar_under));
                const float hit = tok::kHit * l.scale;
                for (int i = 0; i < l.button_count; ++i) {
                    const RectF& a = l.button_cells[static_cast<std::size_t>(i)];
                    CHECK(a.h >= hit - 0.01f);
                    CHECK(a.w >= hit);
                    CHECK(inside(a, l.card));
                    for (int j = i + 1; j < l.button_count; ++j) {
                        CHECK_FALSE(overlap(a, l.button_cells[static_cast<std::size_t>(j)]));
                    }
                }
                // The primary is the widest.
                CHECK(l.button_cells[0].w > l.button_cells[1].w);
                for (const RectF& r : {l.headline, l.counts, l.stats, l.buttons}) {
                    if (!r.empty()) {
                        CHECK(inside(r, l.card));
                    }
                }
                // A very short area drops the stats row, then the counts; the headline and the
                // buttons stay. The counts are the top, the stats below the headline.
                CHECK(l.count_cells == (l.counts.empty() ? 0 : 7));
                CHECK(l.stat_cells == (l.stats.empty() ? 0 : kCheckInStats));
                CHECK_FALSE(l.headline.empty());
                if (!l.counts.empty()) {
                    CHECK(l.counts.bottom() <= l.headline.y + 0.01f);
                }
                if (!l.stats.empty()) {
                    CHECK(l.headline.bottom() <= l.stats.y + 0.01f);
                    CHECK(l.stats.bottom() <= l.buttons.y + 0.01f);
                    CHECK_FALSE(l.counts.empty()); // the counts outrank the stats
                }
            }
        }
    }
}

TEST_CASE("check-in layout: at the usual scale both rows fit every size, Wait Mode has 3 counts") {
    for (const ScreenSize& sz : kSizes) {
        const ScreenLayout lay = screen_of(sz, 1.0f, false);
        const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm);
        const RunCardLayout flow = run_card_layout(lay.note_area, u, RunCardKind::CheckIn, 4);
        const RunCardLayout wait = run_card_layout(lay.note_area, u, RunCardKind::CheckIn, 4, 3);
        CAPTURE(sz.w);
        CHECK(flow.fits);
        CHECK(flow.count_cells == 7);
        CHECK(flow.stat_cells == kCheckInStats);
        CHECK(wait.count_cells == 3);
    }
}

TEST_CASE("summary layout: a row for each bar, Done of 44 points, nothing past the card") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.5f}) {
            for (int bars = 1; bars <= bars_for(sz); ++bars) {
                CAPTURE(sz.w);
                CAPTURE(scale);
                CAPTURE(bars);
                const ScreenLayout lay = screen_of(sz, scale, false);
                const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm * scale);
                const RunCardLayout l =
                    run_card_layout(lay.note_area, u, RunCardKind::Summary, bars);
                REQUIRE(l.row_count == bars);
                CHECK(inside(l.card, lay.note_area));
                CHECK_FALSE(overlap(l.card, lay.keyboard));
                CHECK(l.button_count == 1);
                const RectF& done = l.button_cells[0];
                CHECK(done.h >= tok::kHit * l.scale - 0.01f);
                CHECK(done.w >= tok::kHit * l.scale);
                CHECK(inside(done, l.card));
                for (int i = 0; i < bars; ++i) {
                    const RectF& r = l.rows[static_cast<std::size_t>(i)];
                    CHECK(inside(r, l.card));
                    CHECK_FALSE(overlap(r, done));
                    for (int j = i + 1; j < bars; ++j) {
                        CHECK_FALSE(overlap(r, l.rows[static_cast<std::size_t>(j)]));
                    }
                }
            }
        }
    }
}

TEST_CASE("summary layout: the counts line is kept while it fits and goes before the columns") {
    const RunCardLayout roomy = run_card_layout({0, 0, 1366, 600}, 1.0f, RunCardKind::Summary, 6);
    CHECK(roomy.row_counts);
    CHECK(roomy.columns == 1);
    CHECK(roomy.rows[0].h > run_tok::kRowH * roomy.scale);
    const RunCardLayout tight = run_card_layout({0, 0, 1366, 230}, 1.0f, RunCardKind::Summary, 6);
    CHECK_FALSE(tight.row_counts);
    CHECK(tight.columns == 2);
    CHECK(tight.fits);
}

TEST_CASE("summary layout: at the usual scale it fits, in one column where there is room") {
    for (const ScreenSize& sz : kSizes) {
        const ScreenLayout lay = screen_of(sz, 1.0f, false);
        const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm);
        const RunCardLayout l =
            run_card_layout(lay.note_area, u, RunCardKind::Summary, bars_for(sz));
        CAPTURE(sz.w);
        CHECK(l.fits);
        CHECK(l.columns == 1);
    }
}

TEST_CASE("summary layout: too short for one column, the rows go in two") {
    // A note area that holds six rows in two columns but not one.
    const RectF area{0, 0, 1366, 230};
    const RunCardLayout one = run_card_layout({0, 0, 1366, 600}, 1.0f, RunCardKind::Summary, 6);
    const RunCardLayout two = run_card_layout(area, 1.0f, RunCardKind::Summary, 6);
    CHECK(one.columns == 1);
    CHECK(two.columns == 2);
    CHECK(two.row_count == 6);
    CHECK(two.rows[0].x < two.rows[3].x);
    CHECK(two.rows[0].y == doctest::Approx(two.rows[3].y)); // the second column starts at the top
    CHECK(two.fits);
}

TEST_CASE("run card layout: an empty or tiny area gives no card, never a crash") {
    CHECK(run_card_layout({0, 0, 0, 0}, 1.0f, RunCardKind::CheckIn, 3).card.empty());
    CHECK(run_card_layout({0, 0, 30, 30}, 1.0f, RunCardKind::Summary, 3).card.empty());
    CHECK(run_card_layout({0, 0, 1366, 600}, 1.0f, RunCardKind::Summary, 0).row_count == 0);
}
