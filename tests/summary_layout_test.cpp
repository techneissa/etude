// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the "How it went" page go: every control a touch target, all inside the card
// and the note area, clear of the keyboard and of each other, at every size and UI scale; what a
// short area gives up, in order.
#include "ui/summary_layout.hpp"
#include "ui/toast_regions.hpp" // band_height_pt

#include "etude/look.hpp"
#include "etude/panel_size.hpp"
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

ScreenLayout screen_of(const ScreenSize& s, float u) {
    return compute_layout(s.w, s.h, kDefaultPxPerMm * u, LookSettings{}, PinnedBar{});
}

bool inside(const RectF& r, const RectF& area) {
    constexpr float kEps = 0.01f;
    return r.x >= area.x - kEps && r.y >= area.y - kEps && r.right() <= area.right() + kEps &&
           r.bottom() <= area.bottom() + kEps;
}

bool overlap(const RectF& a, const RectF& b) {
    constexpr float kEps = 0.01f;
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

// Every placed part of a page, to test them all alike.
std::vector<RectF> cells_of(const SummaryLayout& l) {
    std::vector<RectF> out{l.back, l.pin, l.done, l.actions, l.tabs[0], l.tabs[1]};
    for (int i = 0; i < l.bar_count; ++i) {
        const SummaryBarCell& b = l.bars[static_cast<std::size_t>(i)];
        out.push_back(b.watch);
        out.push_back(b.practise);
    }
    return out;
}

// The page as the host places it: in `summary_area`, at the in-play card's scale.
SummaryLayout summary_for(const ScreenLayout& lay, float u, const SummaryContent& c) {
    return summary_layout(summary_area(lay), u, c, {}, card_size(lay.note_area, u).scale);
}

} // namespace

TEST_CASE("summary layout: the page fits every size and scale with its parts inside it") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            CAPTURE(sz.w);
            CAPTURE(scale);
            const ScreenLayout lay = screen_of(sz, scale);
            const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm * scale);
            const SummaryLayout l = summary_for(lay, u, {3, true, true});
            REQUIRE_FALSE(l.card.empty());
            if (!l.fits) {
                // Only a small screen at a large UI scale may be too short, whatever is dropped.
                CHECK(sz.w <= 1024.0f);
                CHECK(scale > 1.0f);
                continue;
            }
            CHECK(inside(l.card, summary_area(lay)));
            // The card may cover the chord strip and the top of the keys, never the top row, and
            // leaves a third of the keyboard showing below it.
            CHECK(l.card.bottom() <= lay.keyboard.bottom() - lay.keyboard.h / 3.0f + 0.01f);
            CHECK(l.card.y >= lay.chord_label.y - 0.01f);
            CHECK_FALSE(overlap(l.card, lay.top_row));
            const float hit = tok::kHit * l.scale;
            const std::vector<RectF> cells = cells_of(l);
            for (std::size_t i = 0; i < cells.size(); ++i) {
                CHECK(inside(cells[i], l.card));
                CHECK(cells[i].h >= hit - 0.01f);
                for (std::size_t j = i + 1; j < cells.size(); ++j) {
                    CHECK_FALSE(overlap(cells[i], cells[j]));
                }
            }
            CHECK(l.back.w >= hit - 0.01f);
            CHECK(l.done.w >= hit - 0.01f);
            CHECK(l.tabs[0].w >= hit - 0.01f);
            CHECK(l.tabs[1].w >= hit - 0.01f);
            CHECK(l.title.w > 0.0f);
        }
    }
}

TEST_CASE("summary layout: the pin chip is a touch target in the header between the title and Done") {
    for (const ScreenSize& sz : {ScreenSize{1366, 768}, ScreenSize{800, 480}}) {
        CAPTURE(sz.w);
        const ScreenLayout lay = screen_of(sz, 1.0f);
        const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm);
        const SummaryLayout l = summary_for(lay, u, {3, true, true});
        REQUIRE(l.fits);
        const float hit = tok::kHit * l.scale;
        CHECK(l.pin.w >= hit - 0.01f);
        CHECK(l.pin.h >= hit - 0.01f);
        CHECK(inside(l.pin, l.card));
        CHECK(l.pin.y == doctest::Approx(l.done.y)); // on the header's row
        CHECK(l.pin.right() <= l.done.x + 0.01f);    // left of Done
        CHECK(l.title.right() <= l.tabs[0].x + 0.01f); // the title keeps clear of the tabs
        CHECK(l.tabs[1].right() <= l.pin.x + 0.01f);   // which end before the pin
        CHECK_FALSE(overlap(l.pin, l.done));
        CHECK_FALSE(overlap(l.pin, l.back));
        CHECK_FALSE(overlap(l.pin, l.title));
        CHECK(l.title.w > 0.0f);
    }
}

TEST_CASE("summary layout: the bar rows are slim faces inside a hit-tall row, chips at the right") {
    const ScreenLayout lay = screen_of({1366, 768}, 1.0f);
    const float u = ui_scale(1366.0f, 768.0f, kDefaultPxPerMm);
    const SummaryLayout l = summary_for(lay, u, {3, true, true});
    REQUIRE(l.fits);
    REQUIRE(l.bar_count == 3);
    CHECK(l.wide);
    for (int i = 0; i < 3; ++i) {
        const SummaryBarCell& b = l.bars[static_cast<std::size_t>(i)];
        CHECK(b.face.h < b.row.h);                // slimmer than the hit area
        CHECK(inside(b.face, b.row));
        CHECK(b.practise.x < b.watch.x);          // practise, then watch at the very end
        CHECK(b.watch.right() == doctest::Approx(b.row.right()));
        CHECK(b.text.right() <= b.practise.x + 0.01f);
        CHECK(b.text.w > 0.0f);
        if (i > 0) {
            CHECK(b.row.y > l.bars[static_cast<std::size_t>(i) - 1].row.y);
        }
    }
}

TEST_CASE("summary layout: a narrow card stacks, with the bars as a row of cells") {
    const ScreenLayout lay = screen_of({800, 480}, 1.0f);
    const float u = ui_scale(800.0f, 480.0f, kDefaultPxPerMm);
    RectF area = summary_area(lay);
    area.w = 700.0f; // a card under the width from which the page has two columns
    const SummaryLayout l =
        summary_layout(area, u, {3, true, true}, {}, card_size(lay.note_area, u).scale);
    REQUIRE(l.fits);
    CHECK_FALSE(l.wide);
    CHECK(l.stacked_bars);
    REQUIRE(l.bar_count == 3);
    CHECK(l.bars[0].row.x < l.bars[1].row.x);
    CHECK(l.bars[1].row.x < l.bars[2].row.x);
    CHECK(l.bars[0].row.y == doctest::Approx(l.bars[2].row.y));
}

TEST_CASE("summary layout: a short area gives up the spacing, the hands, the ruler, the bars") {
    const ScreenLayout lay = screen_of({1366, 768}, 1.0f);
    const float u = ui_scale(1366.0f, 768.0f, kDefaultPxPerMm);
    RectF area = summary_area(lay);
    const float sc = card_size(lay.note_area, u).scale;
    const SummaryLayout full = summary_layout(area, u, {3, true, true}, {}, sc);
    REQUIRE(full.fits);
    CHECK_FALSE(full.compact);
    CHECK(full.hands);
    CHECK_FALSE(full.ruler.empty());
    // Squeeze the area step by step: each step gives up no more than the one before it did.
    bool compact_seen = false;
    bool hands_gone = false;
    bool ruler_gone = false;
    bool bars_gone = false;
    for (float h = area.h; h > 150.0f; h -= 4.0f) {
        area.h = h;
        const SummaryLayout l = summary_layout(area, u, {3, true, true}, {}, sc);
        if (!l.fits) {
            continue;
        }
        compact_seen = compact_seen || l.compact;
        hands_gone = hands_gone || !l.hands;
        ruler_gone = ruler_gone || l.ruler.empty();
        bars_gone = bars_gone || l.bar_count == 0;
        // Never gives up a later thing before an earlier one.
        if (l.hands) {
            CHECK_FALSE(ruler_gone);
        }
        if (!l.ruler.empty()) {
            CHECK_FALSE(bars_gone);
        }
        // The header and the two actions always stay.
        CHECK_FALSE(l.back.empty());
        CHECK_FALSE(l.done.empty());
        CHECK_FALSE(l.actions.empty());
        CHECK(inside(l.card, area));
    }
    CHECK(compact_seen);
    CHECK(hands_gone);
    CHECK(ruler_gone);
    CHECK(bars_gone);
}

TEST_CASE("summary layout: with no bar to name and nothing to time there is a header and the actions") {
    const ScreenLayout lay = screen_of({1366, 768}, 1.0f);
    const float u = ui_scale(1366.0f, 768.0f, kDefaultPxPerMm);
    const SummaryLayout l = summary_for(lay, u, {0, false, false});
    REQUIRE(l.fits);
    CHECK(l.bar_count == 0);
    CHECK(l.ruler.empty());
    CHECK_FALSE(l.hands);
    CHECK(l.bars_label.empty());
    CHECK_FALSE(l.actions.empty());
}

TEST_CASE("summary layout: an empty area gives no page") {
    const SummaryLayout l = summary_layout({}, 1.0f, {3, true, true});
    CHECK(l.card.empty());
    CHECK_FALSE(l.fits);
}

// The band hanging from the revealed bar (unpinned) is out over the top of the area the page may use.
// The page does not move for it: nothing in the layout reads it, so the page is where it is with the
// band in or out. The band draws over it and takes the touches in its own area, so it covers the
// page's header controls and nothing of its body.
TEST_CASE("summary layout: the page does not move for the band hanging from the revealed bar") {
    struct Case {
        ScreenSize size;
        float ppmm;
    };
    for (const Case& c : {Case{{800, 480}, kDefaultPxPerMm}, Case{{1366, 768}, kDefaultPxPerMm},
                          Case{{800, 480}, panel_px_per_mm(800.0f, 480.0f, {154.0f, 86.0f})}}) {
        CAPTURE(c.size.w);
        CAPTURE(c.ppmm);
        const ScreenLayout lay =
            compute_layout(c.size.w, c.size.h, c.ppmm, LookSettings{}, PinnedBar{});
        const float u = ui_scale(c.size.w, c.size.h, c.ppmm);
        const RectF band{0.0f, lay.top_row.bottom(), c.size.w, band_height_pt(false, u)};
        const RectF area = summary_area(lay);
        // The band is out over the area's top (the chord strip's place) ...
        CHECK(overlap(band, area));
        // ... the area is the same whether the band is out or not (the layout has no band to read) ...
        const ScreenLayout pinned_off =
            compute_layout(c.size.w, c.size.h, c.ppmm, LookSettings{}, PinnedBar{false});
        CHECK(summary_area(pinned_off).y == doctest::Approx(area.y));
        CHECK(summary_area(pinned_off).h == doctest::Approx(area.h));
        // ... and so is the page.
        const SummaryContent content{3, true, true};
        const float sc = card_size(lay.note_area, u).scale;
        const SummaryLayout a = summary_layout(area, u, content, {}, sc);
        const SummaryLayout b = summary_layout(summary_area(pinned_off), u, content, {}, sc);
        REQUIRE(a.fits);
        CHECK(a.card.x == doctest::Approx(b.card.x));
        CHECK(a.card.y == doctest::Approx(b.card.y));
        CHECK(a.card.w == doctest::Approx(b.card.w));
        CHECK(a.card.h == doctest::Approx(b.card.h));
        CHECK(a.actions.y == doctest::Approx(b.actions.y));
        // The band covers the page's header controls, whose touches it takes, and none of its body.
        const SummaryLayout board = summary_layout(area, u, content, [] {
            BoardContent bc;
            bc.on = true;
            bc.rows = 4;
            return bc;
        }(), sc);
        REQUIRE(board.fits);
        if (c.size.w < 1000.0f) {
            CHECK(overlap(band, board.back));
            CHECK(overlap(band, board.done));
        }
        CHECK_FALSE(overlap(band, board.actions));
        CHECK(board.board.head.y >= band.bottom() - 0.01f);
        CHECK(board.board.rows_view.y >= band.bottom() - 0.01f);
    }
}

TEST_CASE("summary layout: a page above a live band goes behind it, one behind it stays") {
    // The window order is the touch order: the page made after the band, or touched last, is above
    // the band and is lowered; one already behind it is left (nothing between is moved).
    CHECK(lower_behind_band(5, 3));
    CHECK(lower_behind_band(4, 3));
    CHECK_FALSE(lower_behind_band(2, 3));
    CHECK_FALSE(lower_behind_band(0, 3));
}
