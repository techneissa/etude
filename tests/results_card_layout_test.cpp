// SPDX-License-Identifier: GPL-3.0-or-later
// results_card: layout (split from results_card_test.cpp).
#include "etude/results_card.hpp"
#include "ui/results_card_layout.hpp"
#include "ui/summary_layout.hpp"
#include "etude/look.hpp"
#include "etude/screen_layout.hpp"
#include <doctest/doctest.h>
#include <cmath>
#include <vector>

#include "results_card_test_support.hpp"

using namespace etude;
using namespace results_card_test_support;

// ---- Practising the chosen bars -----------------------------------------------------------------

namespace {

// `count` bars of 2 s each, numbered from `first` (0 is a pickup bar).
std::vector<Measure> bars_of(int count, int first = 1) {
    std::vector<Measure> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out[static_cast<std::size_t>(i)].number = first + i;
        out[static_cast<std::size_t>(i)].start_s = 2.0 * i;
    }
    return out;
}

BarPractice practise(std::initializer_list<int> chosen, BarPracticeKind kind, int bars = 8) {
    const std::vector<Measure> m = bars_of(bars);
    const std::vector<int> c(chosen);
    return plan_bar_practice(c, m, 2.0 * bars, kind);
}

} // namespace

TEST_CASE("bar practice: one by one makes a loop of each bar, in song order") {
    const BarPractice p = practise({5, 2, 3}, BarPracticeKind::OneByOne);
    REQUIRE(p.count == 3);
    CHECK(p.dropped == 0);
    CHECK_FALSE(p.single_loop());
    CHECK(p.loops[0] == BarLoop{2.0, 4.0, 2, 2});
    CHECK(p.loops[1] == BarLoop{4.0, 6.0, 3, 3});
    CHECK(p.loops[2] == BarLoop{8.0, 10.0, 5, 5});
}

TEST_CASE("bar practice: one by one keeps the player's order when asked to, together never does") {
    const std::vector<Measure> m = bars_of(8, 1);
    const std::vector<int> chosen{5, 2, 3};
    const BarPractice own =
        plan_bar_practice(chosen, m, 16.0, BarPracticeKind::OneByOne, /*keep_order=*/true);
    REQUIRE(own.count == 3);
    CHECK(own.loops[0].first_measure == 5);
    CHECK(own.loops[1].first_measure == 2);
    CHECK(own.loops[2].first_measure == 3);
    const BarPractice together =
        plan_bar_practice(chosen, m, 16.0, BarPracticeKind::Together, /*keep_order=*/true);
    REQUIRE(together.count == 2);
    CHECK(together.loops[0].first_measure == 2);
    CHECK(together.loops[1].first_measure == 5);
    // Without it the bars are in song order, as before.
    CHECK(plan_bar_practice(chosen, m, 16.0, BarPracticeKind::OneByOne).loops[0].first_measure ==
          2);
}

TEST_CASE("bar practice: together joins neighbouring bars and keeps a gap as a second stretch") {
    const BarPractice p = practise({5, 2, 3}, BarPracticeKind::Together);
    REQUIRE(p.count == 2);
    CHECK(p.loops[0] == BarLoop{2.0, 6.0, 2, 3});
    CHECK(p.loops[1] == BarLoop{8.0, 10.0, 5, 5});
    CHECK_FALSE(p.single_loop());
}

TEST_CASE("bar practice: neighbouring bars together are a single loop that plays until stopped") {
    const BarPractice p = practise({4, 2, 3}, BarPracticeKind::Together);
    REQUIRE(p.count == 1);
    CHECK(p.single_loop());
    CHECK(p.loops[0] == BarLoop{2.0, 8.0, 2, 4});
    // One bar chosen is one loop too; one by one it is a list of one.
    CHECK(practise({6}, BarPracticeKind::Together).single_loop());
    CHECK_FALSE(practise({6}, BarPracticeKind::OneByOne).single_loop());
}

TEST_CASE("bar practice: the last bar ends at the song's end, a pickup bar is a bar") {
    const std::vector<Measure> m = bars_of(4, 0); // bars 0 (pickup), 1, 2, 3
    const std::vector<int> chosen{3, 0};
    const BarPractice p = plan_bar_practice(chosen, m, 7.5, BarPracticeKind::OneByOne);
    REQUIRE(p.count == 2);
    CHECK(p.loops[0] == BarLoop{0.0, 2.0, 0, 0});
    CHECK(p.loops[1] == BarLoop{6.0, 7.5, 3, 3});
}

TEST_CASE("bar practice: repeats and bars the song does not have are dropped, once") {
    const BarPractice p = practise({3, 3, 99, 4}, BarPracticeKind::OneByOne);
    REQUIRE(p.count == 2);
    CHECK(p.dropped == 1);
    CHECK(p.loops[0].first_measure == 3);
    CHECK(p.loops[1].first_measure == 4);
    CHECK(practise({}, BarPracticeKind::Together).count == 0);
}

TEST_CASE("bar practice: a bar shorter than the shortest loop is dropped, a joined stretch is "
          "judged whole") {
    // Bar 2 is 1.94 to 2.0 and bar 3 is 2.0 to 2.06: 0.06 s each, 0.12 s together.
    std::vector<Measure> m = bars_of(4);
    m[1].start_s = 1.94;
    m[2].start_s = 2.0;
    m[3].start_s = 2.06;
    const std::vector<int> two{2, 3};
    BarPractice p = plan_bar_practice(two, m, 8.0, BarPracticeKind::OneByOne);
    CHECK(p.count == 0);
    CHECK(p.dropped == 2);
    p = plan_bar_practice(two, m, 8.0, BarPracticeKind::Together);
    REQUIRE(p.count == 1);
    CHECK(p.dropped == 0);
    CHECK(p.loops[0].end_s - p.loops[0].start_s == doctest::Approx(0.12));
    // Exactly the shortest loop (0.1 s) is kept, a hundredth under it is not.
    const std::vector<int> one{2};
    m[1].start_s = 2.0;
    m[2].start_s = 2.0 + kMinLoopSeconds;
    CHECK(plan_bar_practice(one, m, 8.0, BarPracticeKind::OneByOne).count == 1);
    m[2].start_s = 2.0 + kMinLoopSeconds - 0.01;
    CHECK(plan_bar_practice(one, m, 8.0, BarPracticeKind::OneByOne).count == 0);
}

TEST_CASE("bar practice: one by one plays each section until a pass is clean, together once") {
    const PlanItem one = bar_plan_item(7, BarPracticeKind::OneByOne);
    CHECK(one.section == 7);
    CHECK(one.repeats == 1);
    CHECK(one.clean_passes_only);
    const PlanItem together = bar_plan_item(7, BarPracticeKind::Together);
    CHECK(together.repeats == 1);
    CHECK_FALSE(together.clean_passes_only);
}

// ---- The card's size ----------------------------------------------------------------------------

namespace {

using namespace etude::app::ui;

struct ScreenSize {
    float w, h;
};

constexpr ScreenSize kSizes[] = {{800, 480}, {1366, 768}, {1920, 1080}, {2560, 1440}};

// The note area of a window with the usual density and the default look (chord label strip
// included), pinned bar or not.
RectF note_area(float w, float h, const PinnedBar& bar = {}) {
    return compute_layout(w, h, kDefaultPxPerMm, LookSettings{}, bar).note_area;
}

float unit_of(const ScreenSize& s) { return ui_scale(s.w, s.h, kDefaultPxPerMm); }

// A Flow run that named six bars, both hands.
RunRecord six_bar_run() {
    RunRecord r = flow_run();
    r.measures = {{1, 6, 2, 0}, {2, 6, 3, 2}, {4, 8, 1, 0},
                  {5, 8, 4, 0}, {7, 8, 5, 0}, {9, 8, 6, 0}};
    return r;
}

// The fullest card: both hands, six bars, Flow's seven counts, the hand choice.
ResultsCard fullest() { return make_results_card(six_bar_run(), first_try(), true, kBoth, false); }

ResultsCard three_bars() { return make_results_card(flow_run(), first_try(), true, kBoth, false); }

bool inside(const RectF& r, const RectF& area) {
    constexpr float kEps = 0.01f;
    return r.x >= area.x - kEps && r.y >= area.y - kEps && r.right() <= area.right() + kEps &&
           r.bottom() <= area.bottom() + kEps;
}

bool overlap(const RectF& a, const RectF& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

} // namespace

TEST_CASE("size: 800x480 keeps the compact form, bigger screens take a share of the note area") {
    // 800x480: the card is the compact form's width at the UI scale, as before the share.
    RectF area = note_area(800, 480);
    CardSize s = card_size(area, unit_of(kSizes[0]));
    CHECK(s.scale == doctest::Approx(1.0f));
    CHECK(s.width == doctest::Approx(kCardBaseWidthPt));
    // 1366x768: 60% of the width at the UI scale.
    area = note_area(1366, 768);
    s = card_size(area, unit_of(kSizes[1]));
    CHECK(s.scale == doctest::Approx(1.0f));
    CHECK(s.width == doctest::Approx(0.6f * area.w));
    // 1920x1080: 60% of the width; the UI scale already holds the fullest card in 60% of the
    // height.
    area = note_area(1920, 1080);
    s = card_size(area, unit_of(kSizes[2]));
    CHECK(s.scale == doctest::Approx(unit_of(kSizes[2])));
    CHECK(s.width == doctest::Approx(0.6f * area.w));
    // 2560x1440: the type and buttons grow past the UI scale (1.6) to fill 60% of the height.
    area = note_area(2560, 1440);
    s = card_size(area, unit_of(kSizes[3]));
    CHECK(s.scale > unit_of(kSizes[3]) * 1.1f);
    CHECK(s.scale <= unit_of(kSizes[3]) * kCardMaxGrow);
    CHECK(s.width == doctest::Approx(0.6f * area.w));
    CHECK(kCardFullHeightPt * s.scale <= 0.6f * area.h + 0.01f);
}

TEST_CASE("size: the growth moves in steps, never past the cap, and a short note area gives none") {
    // A tall, wide area: capped.
    const CardSize tall = card_size({0, 0, 4000, 3000}, 1.0f);
    CHECK(tall.scale == doctest::Approx(kCardMaxGrow));
    // Whole steps of kCardGrowStep.
    for (const float h : {400.0f, 500.0f, 560.0f, 600.0f, 640.0f, 700.0f}) {
        const float grow = card_size({0, 0, 3000, h}, 1.0f).scale;
        const float steps = (grow - 1.0f) / kCardGrowStep;
        CHECK(steps == doctest::Approx(std::round(steps)).epsilon(0.001));
    }
    // Where the share of the height holds the card at the UI scale or less, no growth.
    CHECK(card_size({0, 0, 3000, 100}, 1.3f).scale == doctest::Approx(1.3f));
    // Never wider than the area less its margins, never narrower than the compact form.
    CHECK(card_size({0, 0, 400, 300}, 1.0f).width ==
          doctest::Approx(400.0f - 2.0f * kCardMarginPt));
    CHECK(card_size({0, 0, 3000, 300}, 1.0f).width == doctest::Approx(kCardMaxWidthPt));
    // A bad unit counts as 1; an empty area gives no width.
    CHECK(card_size({0, 0, 800, 300}, 0.0f).scale == doctest::Approx(1.0f));
    CHECK(card_size({}, 1.0f).width == 0.0f);
}

TEST_CASE("size: the width is 60% of the area up to 900 points at the card's scale, then stays") {
    // A short area keeps the scale at 1, so the cap is 900 points. 60% of 1500 is exactly 900.
    CHECK(card_size({0, 0, 1499, 300}, 1.0f).width == doctest::Approx(0.6f * 1499.0f));
    CHECK(card_size({0, 0, 1500, 300}, 1.0f).width == doctest::Approx(kCardMaxWidthPt));
    CHECK(card_size({0, 0, 1501, 300}, 1.0f).width == doctest::Approx(kCardMaxWidthPt));
    CHECK(card_size({0, 0, 4000, 300}, 1.0f).width == doctest::Approx(kCardMaxWidthPt));
    // At a bigger scale the cap grows with it: 900 x 1.5 = 1350, which is 60% of 2250.
    CHECK(card_size({0, 0, 2250, 3000}, 1.0f).width == doctest::Approx(1350.0f));
    CHECK(card_size({0, 0, 2300, 3000}, 1.0f).width == doctest::Approx(1350.0f));
}

TEST_CASE("size: a pixel more height is the same step, a whole step more is the next") {
    // The growth at u = 1 is 0.6 h / 292 floored to a step of 0.05: 520 gives 1.068 -> 1.05 and
    // 600 gives 1.233 -> 1.2.
    CHECK(card_size({0, 0, 3000, 477.0f}, 1.0f).scale == doctest::Approx(1.0f));
    CHECK(card_size({0, 0, 3000, 500.0f}, 1.0f).scale == doctest::Approx(1.0f));
    CHECK(card_size({0, 0, 3000, 520.0f}, 1.0f).scale == doctest::Approx(1.05f));
    CHECK(card_size({0, 0, 3000, 600.0f}, 1.0f).scale == doctest::Approx(1.2f));
}

// ---- Layout -------------------------------------------------------------------------------------

TEST_CASE("layout: the card with three numbers fits the note area at 800x480 and shows every row") {
    const RectF area = note_area(800.0f, 480.0f);
    const CardLayout l = results_card_layout(area, 1.0f, three_bars());
    CHECK(l.fits);
    CHECK(inside(l.card, area));
    CHECK(l.count_cells == 7);
    CHECK_FALSE(l.counts.empty());
    CHECK_FALSE(l.bars.empty());
    CHECK_FALSE(l.bars_head.empty());
    CHECK(l.bars_shown == 3);
    CHECK(l.hand_cells);
    CHECK(l.button_count == 5);
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(l.bar_cells[static_cast<std::size_t>(i)].empty());
    }
    CHECK(l.card.w == doctest::Approx(kCardBaseWidthPt));
}

TEST_CASE("layout: with the weighted score on the headline holds two figures and still fits") {
    ResultsCard card = three_bars();
    card.accuracy_pct = 87;
    const CardLayout plain = results_card_layout(note_area(800.0f, 480.0f), 1.0f, card);
    card.weighted_pct = 84;
    for (const float w : {800.0f, 1366.0f}) {
        const float h = w == 800.0f ? 480.0f : 768.0f;
        const RectF area = note_area(w, h);
        const CardLayout l = results_card_layout(area, 1.0f, card);
        CHECK(l.fits);
        CHECK(inside(l.card, area));
        CHECK(l.figure.right() <= l.text.x);   // the two figures end before the lines of text
        CHECK(l.text.w > 0.0f);
        CHECK(l.count_cells == 7);             // no row is dropped for it
        CHECK(l.bars_shown == results_card_layout(area, 1.0f, three_bars()).bars_shown);
    }
    // Off, the card is as before: the narrow figure and the same card height.
    const CardLayout on = results_card_layout(note_area(800.0f, 480.0f), 1.0f, card);
    CHECK(on.figure.w > plain.figure.w);
    CHECK(on.card.h == doctest::Approx(plain.card.h));
}

TEST_CASE("layout: six bars when the card has the room, three at 800x480") {
    const ResultsCard c = fullest();
    REQUIRE(c.bar_count == 6);
    int shown[4];
    for (int i = 0; i < 4; ++i) {
        shown[i] = results_card_layout(note_area(kSizes[i].w, kSizes[i].h), unit_of(kSizes[i]), c)
                       .bars_shown;
    }
    CHECK(shown[0] == 3);
    CHECK(shown[1] == 6);
    CHECK(shown[2] == 6);
    CHECK(shown[3] == 6);
    // Fewer bars named, fewer chips.
    CHECK(results_card_layout(note_area(1920, 1080), unit_of(kSizes[2]), three_bars()).bars_shown ==
          3);
}

TEST_CASE("layout: the default look's taller note area keeps the card's six chips at 1366x768") {
    // The calmer chord label strip leaves the notes more height, and a bigger area lets the card
    // grow: it must not grow so far that a chip drops out of its row.
    const ResultsCard c = fullest();
    for (const ScreenSize& sz : kSizes) {
        const RectF area = note_area(sz.w, sz.h); // the default look, chord label on
        const CardLayout l = results_card_layout(area, unit_of(sz), c);
        CAPTURE(sz.w);
        CHECK(l.bars_shown == (sz.w < 1000 ? 3 : 6));
    }
    const RectF area = note_area(1366, 768);
    CHECK(card_size(area, unit_of(kSizes[1])).scale == doctest::Approx(1.0f));
    // The label off gives the notes still more room: still six.
    LookSettings no_label;
    no_label.chord_label = false;
    const RectF taller = compute_layout(1366, 768, kDefaultPxPerMm, no_label).note_area;
    CHECK(taller.h > area.h);
    CHECK(results_card_layout(taller, 1.0f, c).bars_shown == 6);
}

TEST_CASE("size: growth never costs the card a chip, over every window height and width") {
    // The chips of a row at a scale, as the layout counts them.
    const auto shown = [](const CardSize& size) {
        const float inner = size.width - 2.0f * tok::kS4 * size.scale;
        const float gap = tok::kS2 * size.scale;
        const int fit =
            static_cast<int>(std::floor((inner + gap) / (kCardChipMinPt * size.scale + gap)));
        const bool compact = size.width <= kCardBaseWidthPt * size.scale + 0.5f;
        return std::clamp(fit, 1, compact ? kCardCompactBars : kMaxCardBars);
    };
    int checked = 0;
    int grew = 0;
    for (float w = 800.0f; w <= 2600.0f; w += 130.0f) {
        for (float h = 300.0f; h <= 1500.0f; h += 37.0f) {
            for (const float u : {1.0f, 1.25f, 1.5f}) {
                const RectF area{0.0f, 80.0f, w, h};
                RectF flat = area; // too short for the card to grow: the card at the UI scale
                flat.h = 1.0f;
                const CardSize base = card_size(flat, u);
                const CardSize grown = card_size(area, u);
                CAPTURE(w);
                CAPTURE(h);
                CAPTURE(u);
                CHECK(base.scale == doctest::Approx(u));
                CHECK(grown.scale >= u - 1e-4f);
                CHECK(shown(grown) >= shown(base));
                ++checked;
                grew += grown.scale > u + 1e-4f ? 1 : 0;
            }
        }
    }
    CHECK(checked > 300);
    CHECK(grew > 20); // the cap leaves the card growing where its chips still fit
}

TEST_CASE("size: the card's padding and chip gap are the UI's tokens") {
    CHECK(kCardPadPt == tok::kS4);
    CHECK(kCardChipGapPt == tok::kS2);
}

TEST_CASE(
    "layout: at every screen the card takes about 60% of the note area, no more, never past it") {
    for (const ScreenSize& sz : kSizes) {
        const RectF area = note_area(sz.w, sz.h);
        const float u = unit_of(sz);
        const CardLayout l = results_card_layout(area, u, fullest());
        CAPTURE(sz.w);
        CHECK(l.fits);
        CHECK(inside(l.card, area));
        // The compact form is wider than the share where the share would be narrower than it.
        CHECK(l.card.w <= std::max(0.6f * area.w, kCardBaseWidthPt * u) + 0.01f);
        if (sz.w >= 1366.0f) {
            // The space is there: the card is 60% wide and (the type grown) up to 60% tall.
            CHECK(l.card.w >= 0.59f * area.w);
            CHECK(l.card.h <= 0.6f * area.h + 0.5f);
            CHECK(l.card.h >= 0.5f * area.h);
        }
        // The type, figures and buttons follow the card's scale.
        CHECK(l.scale >= u);
        CHECK(l.buttons.h == doctest::Approx(tok::kHit * l.scale));
    }
}

TEST_CASE("layout: it never reaches the keyboard or the top row, at every size and unit") {
    for (const ScreenSize& sz : {ScreenSize{800, 480}, ScreenSize{1024, 600}, ScreenSize{1366, 768},
                                 ScreenSize{1920, 1080}, ScreenSize{2560, 1440}}) {
        for (const float u : {1.0f, 1.25f, 1.5f}) {
            for (const bool pinned : {false, true}) {
                CAPTURE(sz.w);
                CAPTURE(sz.h);
                CAPTURE(u);
                CAPTURE(pinned);
                const ScreenLayout lay =
                    compute_layout(sz.w, sz.h, kDefaultPxPerMm, LookSettings{},
                                   pinned ? PinnedBar{true, BarStyle::Groups, true} : PinnedBar{});
                const CardLayout l = results_card_layout(lay.note_area, u, fullest());
                CHECK(inside(l.card, lay.note_area));
                CHECK_FALSE(overlap(l.card, lay.keyboard));
                CHECK_FALSE(overlap(l.card, lay.top_row));
                CHECK_FALSE(overlap(l.card, lay.bar_under));
                // Everything shown lies inside the card.
                for (const RectF& r : {l.headline, l.counts, l.bars_head, l.bars, l.buttons}) {
                    if (!r.empty()) {
                        CHECK(inside(r, l.card));
                    }
                }
            }
        }
    }
}

TEST_CASE(
    "layout: every control is at least 44 points tall and wide, inside the card, none overlap") {
    for (const ScreenSize& sz : kSizes) {
        for (const float u : {1.0f, 1.5f}) {
            const RectF area = note_area(sz.w, sz.h);
            const CardLayout l = results_card_layout(area, u, fullest());
            CAPTURE(sz.w);
            CAPTURE(u);
            std::vector<RectF> cells;
            for (int i = 0; i < l.button_count; ++i) {
                cells.push_back(l.button_cells[static_cast<std::size_t>(i)]);
            }
            for (int i = 0; i < l.bars_shown; ++i) {
                cells.push_back(l.bar_cells[static_cast<std::size_t>(i)]);
            }
            if (!l.bars_head.empty()) {
                cells.push_back(l.action_cells[0]);
                cells.push_back(l.action_cells[1]);
            }
            REQUIRE(l.button_count == 5);
            REQUIRE(cells.size() ==
                    static_cast<std::size_t>(5 + l.bars_shown + (l.bars_head.empty() ? 0 : 2)));
            for (std::size_t i = 0; i < cells.size(); ++i) {
                CHECK(cells[i].h >= 44.0f * u - 0.01f);
                CHECK(cells[i].w >= 44.0f * u);
                CHECK(inside(cells[i], l.card));
                for (std::size_t j = i + 1; j < cells.size(); ++j) {
                    CHECK_FALSE(overlap(cells[i], cells[j]));
                }
            }
            // The caption of the bars' head does not run under the actions.
            if (!l.bars_head.empty()) {
                CHECK(l.bars_caption.right() <= l.action_cells[0].x);
            }
        }
    }
}

TEST_CASE("layout: the hand choice sits between Replay and Done; a practice list has two buttons") {
    ResultsCard c = fullest();
    CardLayout l = results_card_layout(note_area(1366, 768), 1.0f, c);
    CHECK(l.hand_cells);
    CHECK(l.button_count == 5);
    for (int i = 1; i < l.button_count; ++i) {
        CHECK(l.button_cells[static_cast<std::size_t>(i)].x >
              l.button_cells[static_cast<std::size_t>(i) - 1].x);
    }
    // Replay is the widest: it is the primary.
    CHECK(l.button_cells[0].w > l.button_cells[1].w);
    c.hand_choice = false;
    l = results_card_layout(note_area(1366, 768), 1.0f, c);
    CHECK_FALSE(l.hand_cells);
    CHECK(l.button_count == 2);
}

TEST_CASE("layout: a few bars sit at the left in chips of the same width, never stretched") {
    ResultsCard c = fullest();
    c.bar_count = 1;
    const CardLayout one = results_card_layout(note_area(1920, 1080), 1.406f, c);
    CHECK(one.bars_shown == 1);
    c.bar_count = 3;
    const CardLayout three = results_card_layout(note_area(1920, 1080), 1.406f, c);
    CHECK(three.bars_shown == 3);
    CHECK(one.bar_cells[0].w == doctest::Approx(three.bar_cells[0].w));
    CHECK(three.bar_cells[0].x == doctest::Approx(one.bar_cells[0].x));
    CHECK(three.bar_cells[2].right() <= three.bars.right() + 0.01f);
    CHECK(one.bar_cells[0].right() <
          one.bars.right() - 1.0f); // one chip fills a third, not the row
}

TEST_CASE("layout: a short area tightens the spacing, then drops the counts, then the bars") {
    const ResultsCard c = three_bars();
    const float margin = 2.0f * card_tok::kMargin;
    // With the normal spacing: pad 16 twice, headline 40, gap 12, buttons 44, and the counts and
    // the bars' two rows each after a gap of 12.
    const float full = 2.0f * tok::kS4 + card_tok::kHeadlineH + tok::kS3 + tok::kHit +
                       (tok::kS3 + card_tok::kCountsH) +
                       (tok::kS3 + tok::kHit + tok::kS2 + tok::kHit);
    CHECK(full == doctest::Approx(kCardFullHeightPt));
    // The compact form: pad 8, gap 8, headline 32.
    const float tight = 2.0f * tok::kS2 + card_tok::kCompactHeadlineH + tok::kS2 + tok::kHit +
                        (tok::kS2 + card_tok::kCountsH) +
                        (tok::kS2 + tok::kHit + tok::kS2 + tok::kHit);
    const float no_counts = tight - (tok::kS2 + card_tok::kCountsH);
    const float no_bars = no_counts - (tok::kS2 + tok::kHit + tok::kS2 + tok::kHit);
    const auto area_of = [](float h) { return RectF{0.0f, 0.0f, 800.0f, h}; };
    // Room for everything.
    CardLayout l = results_card_layout(area_of(full + margin), 1.0f, c);
    CHECK_FALSE(l.compact);
    CHECK_FALSE(l.counts.empty());
    CHECK_FALSE(l.bars.empty());
    // A point short: the same rows, tighter.
    l = results_card_layout(area_of(full + margin - 1.0f), 1.0f, c);
    CHECK(l.compact);
    CHECK_FALSE(l.counts.empty());
    CHECK_FALSE(l.bars.empty());
    CHECK(l.fits);
    l = results_card_layout(area_of(tight + margin), 1.0f, c);
    CHECK(l.compact);
    CHECK_FALSE(l.counts.empty());
    CHECK(l.fits);
    // A point short of that: the counts go.
    l = results_card_layout(area_of(tight + margin - 1.0f), 1.0f, c);
    CHECK(l.counts.empty());
    CHECK_FALSE(l.bars.empty());
    CHECK_FALSE(l.bars_head.empty());
    l = results_card_layout(area_of(no_counts + margin), 1.0f, c);
    CHECK_FALSE(l.bars.empty());
    CHECK(l.fits);
    // Then the bars and their actions.
    l = results_card_layout(area_of(no_counts + margin - 1.0f), 1.0f, c);
    CHECK(l.bars.empty());
    CHECK(l.bars_head.empty());
    CHECK(l.bars_shown == 0);
    CHECK(l.fits);
    CHECK(l.buttons.h >= tok::kHit);
    l = results_card_layout(area_of(no_bars + margin), 1.0f, c);
    CHECK(l.fits);
    CHECK_FALSE(l.headline.empty());
    // The headline and the buttons always stay, whole.
    l = results_card_layout(area_of(120.0f), 1.0f, c);
    CHECK_FALSE(l.headline.empty());
    CHECK_FALSE(l.buttons.empty());
    CHECK(l.buttons.h >= tok::kHit);
}

TEST_CASE("layout: a run that graded nothing is a headline, the hand choice and buttons") {
    const ResultsCard c = make_ungraded_card(RunMode::Flow, HandFilter::Both, kBoth, false);
    const CardLayout l = results_card_layout(note_area(800.0f, 480.0f), 1.0f, c);
    CHECK(l.counts.empty());
    CHECK(l.bars.empty());
    CHECK(l.bars_head.empty());
    CHECK(l.count_cells == 0);
    CHECK_FALSE(l.buttons.empty());
    CHECK(l.button_count == 5);
}

TEST_CASE("layout: Wait Mode has three count cells") {
    ResultsCard c = fullest();
    c.mode = RunMode::Wait;
    CHECK(results_card_layout(note_area(1366.0f, 768.0f), 1.0f, c).count_cells == 3);
}

TEST_CASE("layout: an empty or tiny area gives no card, never a crash") {
    CHECK(results_card_layout({}, 1.0f, fullest()).card.empty());
    CHECK(results_card_layout({0, 0, 20, 20}, 1.0f, fullest()).card.empty());
}

TEST_CASE("layout: at 800x480 with a pinned bar the card fits, buttons whole, in every bar style") {
    for (const BarStyle style : {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock}) {
        for (const float u : {1.0f, 1.25f, 1.5f}) {
            const ScreenLayout lay = compute_layout(800.0f, 480.0f, kDefaultPxPerMm, LookSettings{},
                                                    PinnedBar{true, style, true});
            const CardLayout l = results_card_layout(lay.note_area, u, fullest());
            CAPTURE(static_cast<int>(style));
            CAPTURE(u);
            CAPTURE(lay.note_area.h);
            CHECK(l.fits);
            CHECK(inside(l.card, lay.note_area));
            CHECK_FALSE(overlap(l.card, lay.keyboard));
            CHECK_FALSE(overlap(l.card, lay.bar_under));
            REQUIRE(l.button_count == 5);
            for (int i = 0; i < 5; ++i) {
                const RectF& b = l.button_cells[static_cast<std::size_t>(i)];
                CHECK(inside(b, l.card));
                CHECK(b.h >= 44.0f * u - 0.01f);
            }
        }
    }
}

// ---- The "How it went" chip -----------------------------------------------------------------------

TEST_CASE("layout: the How it went chip sits in the headline, clear of the figure, the text and the hands") {
    for (const float w : {800.0f, 1024.0f, 1366.0f, 1920.0f}) {
        const float h = w == 800.0f ? 480.0f : w == 1024.0f ? 600.0f : w == 1366.0f ? 768.0f : 1080.0f;
        for (const bool pair : {false, true}) {
            ResultsCard card = three_bars();
            card.weighted_pct = pair ? 84 : -1;
            const RectF area = note_area(w, h);
            const CardLayout l = results_card_layout(area, 1.0f, card);
            CAPTURE(w);
            CAPTURE(pair);
            REQUIRE_FALSE(l.review.empty());
            CHECK(inside(l.review, l.card));
            CHECK_FALSE(overlap(l.review, l.figure));
            CHECK_FALSE(overlap(l.review, l.text));
            if (!l.hands.empty()) {
                CHECK_FALSE(overlap(l.review, l.hands));
            }
            // A touch target at least: its width, or the headline's own height.
            CHECK(l.review.w >= 44.0f * l.scale - 0.01f);
            CHECK(l.review.h >= 44.0f * l.scale - 0.01f);
            CHECK_FALSE(overlap(l.review, l.counts));
            // The words are there where the text beside the chip keeps its room.
            if (l.review_words) {
                CHECK(l.review.w >= card_tok::kReviewW * l.scale - 0.01f);
                CHECK(l.text.w >= card_tok::kTextMinW * l.scale - 0.01f);
            }
        }
    }
}

TEST_CASE("layout: a card at the width of a big screen shows the chip with its words") {
    const CardLayout l = results_card_layout(note_area(1366, 768), 1.0f, three_bars());
    CHECK(l.review_words);
}

// ---- The summary page's room and size --------------------------------------------------------------

TEST_CASE("summary area: from the chord strip to a third of the keyboard above the foot, never the top "
          "row") {
    struct Window {
        float w, h;
    };
    for (const Window& win : {Window{800, 480}, Window{1366, 768}, Window{1920, 1080}, Window{2560, 1440}}) {
        for (const bool pinned : {false, true}) {
            CAPTURE(win.w);
            CAPTURE(pinned);
            PinnedBar bar;
            bar.pinned = pinned;
            bar.has_song = true;
            const ScreenLayout lay = compute_layout(win.w, win.h, kDefaultPxPerMm, LookSettings{}, bar);
            const RectF a = summary_area(lay);
            REQUIRE_FALSE(a.empty());
            CHECK(a.x == doctest::Approx(lay.note_area.x));
            CHECK(a.w == doctest::Approx(lay.note_area.w));
            // It starts where the chord strip does (the note area's top when the strip is off) ...
            CHECK(a.y == doctest::Approx(lay.chord_label.y));
            CHECK(a.y >= lay.top_row.bottom() - 0.01f);
            CHECK(a.y >= lay.bar_under.bottom() - 0.01f);
            CHECK(a.y >= lay.lane.bottom() - 0.01f);
            // ... and ends a third of the keyboard's height above the window's foot.
            CHECK(a.bottom() == doctest::Approx(lay.keyboard.bottom() - lay.keyboard.h / 3.0f));
            CHECK(a.bottom() > lay.note_area.bottom());
            CHECK(a.bottom() < lay.keyboard.bottom());
        }
    }
    // A strip that is off leaves the note area's top; no note area, no room.
    LookSettings no_strip;
    no_strip.chord_label = false;
    const ScreenLayout lay = compute_layout(1366, 768, kDefaultPxPerMm, no_strip, PinnedBar{});
    CHECK(summary_area(lay).y == doctest::Approx(lay.note_area.y));
    CHECK(summary_area(ScreenLayout{}).empty());
}

TEST_CASE("summary area: a resized keyboard moves the foot with it, a hidden one leaves the note "
          "area's") {
    for (const float share : {kMinKeyboardHeight, 0.12f, 0.18f, 0.25f, kMaxKeyboardHeight}) {
        CAPTURE(share);
        LookSettings look;
        look.keyboard_height = share;
        const ScreenLayout lay = compute_layout(1366, 768, kDefaultPxPerMm, look, PinnedBar{});
        REQUIRE_FALSE(lay.keyboard.empty());
        const RectF a = summary_area(lay);
        REQUIRE_FALSE(a.empty());
        // A third of the keyboard stays showing, whatever its height: the taller the keyboard, the
        // higher the page's foot.
        CHECK(lay.keyboard.bottom() - a.bottom() == doctest::Approx(lay.keyboard.h / 3.0f));
        CHECK(a.bottom() > lay.note_area.bottom());
        CHECK(a.y == doctest::Approx(lay.chord_label.y));
    }
    // Taller keys take more room from the page.
    LookSettings tall;
    tall.keyboard_height = kMaxKeyboardHeight;
    LookSettings low;
    low.keyboard_height = kMinKeyboardHeight;
    CHECK(summary_area(compute_layout(1366, 768, kDefaultPxPerMm, tall, PinnedBar{})).bottom() <
          summary_area(compute_layout(1366, 768, kDefaultPxPerMm, low, PinnedBar{})).bottom());
    // A keyboard with no row (nothing left of the window for it): the page's foot is the note
    // area's.
    ScreenLayout lay = compute_layout(1366, 768, kDefaultPxPerMm, LookSettings{}, PinnedBar{});
    lay.keyboard = {};
    REQUIRE(lay.keyboard.empty());
    const RectF a = summary_area(lay);
    REQUIRE_FALSE(a.empty());
    CHECK(a.bottom() == doctest::Approx(lay.note_area.bottom()));
    CHECK(a.y == doctest::Approx(lay.chord_label.y));
}

TEST_CASE("summary area: a pinned bar's band keeps its row above the page") {
    PinnedBar bar;
    bar.pinned = true;
    bar.has_song = true;
    const ScreenLayout lay = compute_layout(1366, 768, kDefaultPxPerMm, LookSettings{}, bar);
    REQUIRE_FALSE(lay.bar_under.empty());
    const RectF a = summary_area(lay);
    CHECK(a.y >= lay.bar_under.bottom() - 0.01f);
    CHECK(a.y >= lay.top_row.bottom() + lay.bar_under.h - 0.01f);
}

TEST_CASE("summary size: the page is centred in its area, sideways and up and down") {
    struct Window {
        float w, h;
    };
    for (const Window& win : {Window{800, 480}, Window{1366, 768}, Window{1920, 1080}, Window{2560, 1440}}) {
        CAPTURE(win.w);
        const ScreenLayout lay = compute_layout(win.w, win.h, kDefaultPxPerMm, LookSettings{}, PinnedBar{});
        const float u = ui_scale(win.w, win.h, kDefaultPxPerMm);
        const RectF area = summary_area(lay);
        const app::ui::SummaryLayout l = app::ui::summary_layout(
            area, u, {3, true, true}, {}, card_size(lay.note_area, u).scale);
        REQUIRE_FALSE(l.card.empty());
        CHECK(l.card.x - area.x == doctest::Approx(area.right() - l.card.right()));
        CHECK(l.card.y - area.y == doctest::Approx(area.bottom() - l.card.bottom()));
        // The width is the size's own.
        CHECK(l.card.w == doctest::Approx(summary_size(area, u, card_size(lay.note_area, u).scale).width));
    }
}

TEST_CASE("summary size: 85% of the width up to 1100 points at the card's scale") {
    const RectF area{0, 100, 800, 394};
    CardSize s = summary_size(area, 1.0f, 1.0f);
    CHECK(s.scale == doctest::Approx(1.0f));
    CHECK(s.width == doctest::Approx(kSummaryShare * 800.0f));
    // 85% of 1366 is over 1100: the cap, so 1100 points.
    s = summary_size({0, 100, 1366, 600}, 1.0f, 1.0f);
    CHECK(s.width == doctest::Approx(kSummaryMaxWidthPt));
    // The cap grows with the card's scale: 1100 x 1.41.
    s = summary_size({0, 100, 1920, 900}, 1.41f, 1.41f);
    CHECK(s.scale == doctest::Approx(1.41f));
    CHECK(s.width == doctest::Approx(kSummaryMaxWidthPt * 1.41f));
    // The card scale never goes under the UI's; 0 means the UI's.
    CHECK(summary_size(area, 1.3f, 0.0f).scale == doctest::Approx(1.3f));
    CHECK(summary_size(area, 1.3f, 1.0f).scale == doctest::Approx(1.3f));
    // Never wider than the area less its margins, never narrower than the compact form; a bad unit
    // counts as 1; an empty area gives no width.
    CHECK(summary_size({0, 0, 400, 300}, 1.0f, 1.0f).width ==
          doctest::Approx(400.0f - 2.0f * kCardMarginPt));
    CHECK(summary_size({0, 0, 600, 300}, 1.0f, 1.0f).width == doctest::Approx(kCardBaseWidthPt));
    CHECK(summary_size({0, 0, 800, 300}, 0.0f, 0.0f).scale == doctest::Approx(1.0f));
    CHECK(summary_size({}, 1.0f, 1.0f).width == 0.0f);
}
