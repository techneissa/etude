// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_view.hpp"

#include "etude/settings_doc.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace etude;

namespace {

bool inside(const RectF& inner, const RectF& outer) {
    return inner.x >= outer.x - 0.01f && inner.y >= outer.y - 0.01f &&
           inner.right() <= outer.right() + 0.01f && inner.bottom() <= outer.bottom() + 0.01f;
}

} // namespace

namespace {
bool same_rect(const RectF& a, const RectF& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
} // namespace

TEST_CASE("sheet view: the switch goes falling, sheet, both and round; the file's words read") {
    CHECK(next_view_mode(ViewMode::Falling) == ViewMode::Sheet);
    CHECK(next_view_mode(ViewMode::Sheet) == ViewMode::Both);
    CHECK(next_view_mode(ViewMode::Both) == ViewMode::Falling);
    CHECK(parse_view_mode("falling") == ViewMode::Falling);
    CHECK(parse_view_mode("sheet") == ViewMode::Sheet);
    CHECK(parse_view_mode("both") == ViewMode::Both);
    CHECK_FALSE(parse_view_mode("Sheet").has_value());
}

TEST_CASE("sheet view: the view is saved in [app] and an unknown word keeps falling notes") {
    SettingsDocument doc;
    AppSettings app;
    app.view_mode = ViewMode::Both;
    write_app_settings(doc, app);
    CHECK(read_app_settings(doc).view_mode == ViewMode::Both);
    const LoadedSettings bad = load_settings("[app]\nview_mode = sideways\n");
    CHECK(bad.app.view_mode == ViewMode::Falling);
    CHECK_FALSE(bad.issues.empty());
}

TEST_CASE("sheet view: falling notes, or no song, show no score") {
    const RectF area{0.0f, 100.0f, 1366.0f, 500.0f};
    const SheetPlan falling = plan_sheet(area, ViewMode::Falling, true, true, kDefaultPxPerMm);
    CHECK_FALSE(falling.shown);
    CHECK(falling.notes.h == area.h);
    const SheetPlan free_play = plan_sheet(area, ViewMode::Sheet, false, true, kDefaultPxPerMm);
    CHECK_FALSE(free_play.shown);
}

TEST_CASE("sheet view: the Sheet view takes the whole area, two systems when they fit") {
    const float ppmm = kDefaultPxPerMm;
    // 1366x768 and 1920x1080: room for two grand staves at a legible size.
    for (const RectF area : {RectF{0.0f, 57.0f, 1366.0f, 570.0f}, RectF{0.0f, 80.0f, 1920.0f, 800.0f}}) {
        const SheetPlan p = plan_sheet(area, ViewMode::Sheet, true, true, ppmm);
        REQUIRE(p.shown);
        CHECK(p.systems == 2);
        CHECK(p.notes.h == 0.0f);
        CHECK(inside(p.paper, p.sheet));
        CHECK(inside(p.sheet, area));
        CHECK(p.staff_space_px >= kSheetMinSpaceMm * ppmm);
        CHECK(p.staff_space_px <= kSheetMaxSpaceMm * ppmm);
        CHECK(p.system_width_px <= p.paper.w);
        CHECK(p.slot_height_px * 2.0f <= p.paper.h);
    }
    // A short area: one system.
    const SheetPlan small = plan_sheet({0.0f, 50.0f, 800.0f, 230.0f}, ViewMode::Sheet, true, true, ppmm);
    REQUIRE(small.shown);
    CHECK(small.systems == 1);
}

TEST_CASE("sheet view: two systems from exactly the smallest staff space") {
    const float ppmm = kDefaultPxPerMm;
    const float min_space = kSheetMinSpaceMm * ppmm;
    const float frame = 2.0f * (kSheetMarginSpaces + kSheetGapSpaces);
    const float exact = min_space * (2.0f * kGrandStaffSpaces + frame);
    const RectF wide{0.0f, 0.0f, 4000.0f, exact};
    CHECK(plan_sheet(wide, ViewMode::Sheet, true, true, ppmm).systems == 2);
    const RectF short_by_one{0.0f, 0.0f, 4000.0f, exact - 1.0f};
    CHECK(plan_sheet(short_by_one, ViewMode::Sheet, true, true, ppmm).systems == 1);
}

TEST_CASE("sheet view: Both puts one system in a strip above the falling notes") {
    const RectF area{0.0f, 100.0f, 1366.0f, 530.0f};
    const SheetPlan p = plan_sheet(area, ViewMode::Both, true, true, kDefaultPxPerMm);
    REQUIRE(p.shown);
    CHECK(p.systems == 1);
    CHECK(p.sheet.y == area.y);
    CHECK(p.sheet.h <= area.h * kSheetStripMaxShare + 0.5f);
    CHECK(p.notes.y == doctest::Approx(p.sheet.bottom()));
    CHECK(p.notes.bottom() == doctest::Approx(area.bottom()));
    CHECK(p.notes.h > 0.0f);
    // A single staff needs a shorter strip.
    const SheetPlan single = plan_sheet(area, ViewMode::Both, true, false, kDefaultPxPerMm);
    CHECK(single.sheet.h < p.sheet.h);
    // A short area caps the strip at its share.
    const RectF low{0.0f, 100.0f, 1366.0f, 200.0f};
    const SheetPlan capped = plan_sheet(low, ViewMode::Both, true, true, kDefaultPxPerMm);
    CHECK(capped.sheet.h <= low.h * kSheetStripMaxShare + 0.5f);
}

TEST_CASE("sheet view: the score's room is the note area and the chord name's strip above it") {
    ScreenLayout l;
    l.chord_label = {0.0f, 57.0f, 1366.0f, 40.0f};
    l.note_area = {0.0f, 97.0f, 1366.0f, 500.0f};
    const RectF r = score_area(l);
    CHECK(r.y == 57.0f);
    CHECK(r.bottom() == l.note_area.bottom());
    l.chord_label = {};
    CHECK(score_area(l).y == 97.0f);
}

TEST_CASE("sheet view: the page turn slides, eased, and jumps when it must") {
    CHECK(sheet_turn_at(0.0f, 1.0f, 0.0f, false) == doctest::Approx(0.0f));
    const float half = sheet_turn_at(0.0f, 1.0f, kSheetTurnS * 0.5f, false);
    CHECK(half > 0.5f); // eased out: more than half way at half the time
    CHECK(half < 1.0f);
    CHECK(sheet_turn_at(0.0f, 1.0f, kSheetTurnS, false) == 1.0f);
    CHECK(sheet_turn_at(0.0f, 1.0f, 0.01f, true) == 1.0f);  // reduced effects: at once
    CHECK(sheet_turn_at(0.0f, 3.0f, 0.01f, false) == 3.0f); // a jump of more than one system
    CHECK(sheet_turn_at(4.0f, 0.0f, 0.01f, false) == 0.0f); // a repeat back to the start
}

TEST_CASE("sheet view: room for the toggles beside the paper's top right, clear of the music") {
    const float corner = 44.0f;
    for (const RectF area : {RectF{0.0f, 57.0f, 1366.0f, 570.0f}, RectF{0.0f, 40.0f, 800.0f, 300.0f}}) {
        for (const ViewMode mode : {ViewMode::Sheet, ViewMode::Both}) {
            const SheetPlan p = plan_sheet(area, mode, true, true, kDefaultPxPerMm, corner);
            REQUIRE(p.shown);
            CHECK(inside(p.corner, p.sheet));
            CHECK(p.corner.x >= p.paper.right()); // beside the paper, never on it
            // The view button first, the two toggles under it (stacked, or side by side when the
            // sheet is short), or in a row to its left when the sheet is too short for that too.
            const RectF button = sheet_view_button(area, corner);
            CHECK(button.x >= p.paper.right());
            CHECK(p.corner.w * p.corner.h == doctest::Approx(2.0f * corner * corner));
            if (p.corner.y > button.y) {
                CHECK(p.corner.y == doctest::Approx(button.bottom()));
                CHECK(p.corner.right() == doctest::Approx(button.right()));
            } else {
                CHECK(p.corner.y == doctest::Approx(button.y));
                CHECK(p.corner.right() == doctest::Approx(button.x));
            }
            const float margin = p.staff_space_px * kSheetMarginSpaces;
            CHECK(p.paper.x + 2.0f * margin + p.system_width_px <= p.paper.right() + 0.01f);
        }
    }
    // None asked for: none kept, and the systems use the width.
    const SheetPlan none = plan_sheet({0.0f, 57.0f, 1366.0f, 570.0f}, ViewMode::Sheet, true, true,
                                      kDefaultPxPerMm);
    CHECK(none.corner.empty());
}

TEST_CASE("sheet view: note names on the sheet plan taller systems, lanes for the tallest stack") {
    const RectF area{0.0f, 57.0f, 1366.0f, 570.0f};
    const SheetPlan plain = plan_sheet(area, ViewMode::Sheet, true, true, kDefaultPxPerMm, 44.0f);
    SheetNameRoom room;
    room.above = 3;
    room.below = 2;
    room.line_px = 14.0f;
    room.line_spaces = 1.5f;
    const SheetPlan named = plan_sheet(area, ViewMode::Sheet, true, true, kDefaultPxPerMm, 44.0f, room);
    REQUIRE(named.shown);
    // Each system's slot holds the staves, the lanes' spaces and five names' lines.
    const float line = std::max(room.line_px, room.line_spaces * named.staff_space_px);
    CHECK(named.slot_height_px >=
          named.staff_space_px * (kGrandStaffSpaces + 2.0f * kSheetNameLaneSpaces) + 5.0f * line - 0.5f);
    CHECK(named.staff_space_px <= plain.staff_space_px);
    // Taller stacks, smaller music.
    room.above = 5;
    const SheetPlan taller = plan_sheet(area, ViewMode::Sheet, true, true, kDefaultPxPerMm, 44.0f, room);
    CHECK((taller.staff_space_px < named.staff_space_px || taller.systems < named.systems));
    // Both keeps its strip: no room is planned for names there.
    const SheetPlan both = plan_sheet(area, ViewMode::Both, true, true, kDefaultPxPerMm, 44.0f, room);
    const SheetPlan both_plain = plan_sheet(area, ViewMode::Both, true, true, kDefaultPxPerMm, 44.0f);
    CHECK(same_rect(both.sheet, both_plain.sheet));
    CHECK(both.staff_space_px == both_plain.staff_space_px);
}

TEST_CASE("sheet view: in Both the strip keeps its size with names on") {
    SheetNameRoom room;
    room.above = 4;
    room.below = 7;
    room.line_px = 13.0f;
    room.line_spaces = 1.5f;
    for (const RectF area : {RectF{0.0f, 57.0f, 1366.0f, 560.0f}, RectF{0.0f, 40.0f, 800.0f, 320.0f},
                             RectF{0.0f, 57.0f, 1920.0f, 800.0f}}) {
        const SheetPlan named = plan_sheet(area, ViewMode::Both, true, true, kDefaultPxPerMm, 44.0f, room);
        const SheetPlan plain = plan_sheet(area, ViewMode::Both, true, true, kDefaultPxPerMm, 44.0f);
        REQUIRE(named.shown);
        CHECK(same_rect(named.sheet, plain.sheet));
        CHECK(named.slot_height_px == plain.slot_height_px);
        CHECK(named.sheet.h <= area.h * kSheetStripMaxShare + 0.5f);
    }
}

TEST_CASE("sheet view: name lanes keep to the names' largest size on a big staff") {
    SheetNameRoom room;
    room.above = 3;
    room.below = 3;
    room.line_px = 12.0f;
    room.line_spaces = 1.5f;
    room.line_max_px = 15.0f;
    const RectF area{0.0f, 57.0f, 1920.0f, 1000.0f};
    const SheetPlan p = plan_sheet(area, ViewMode::Sheet, true, true, kDefaultPxPerMm, 44.0f, room);
    REQUIRE(p.shown);
    REQUIRE(room.line_spaces * p.staff_space_px > room.line_max_px); // a staff big enough to cap
    // Each slot holds the staves, the lanes' spaces and six names at the largest size, no more.
    const float system = p.staff_space_px * (kGrandStaffSpaces + 2.0f * kSheetNameLaneSpaces) + 6.0f * 15.0f;
    CHECK(p.slot_height_px >= system - 0.5f);
    CHECK(p.slot_height_px <= system * kSheetSlotSlack + 0.5f);
}


TEST_CASE("sheet view: the view button stands at the same top-right spot in every view, and the toggles "
          "and the paper keep clear of it") {
    const float corner = 44.0f;
    const RectF area{0.0f, 57.0f, 1366.0f, 570.0f};
    const RectF button = sheet_view_button(area, corner);
    CHECK(button.w == doctest::Approx(corner));
    CHECK(button.h == doctest::Approx(corner));
    CHECK(button.right() < area.right()); // in from the edge
    CHECK(button.y > area.y);             // and from the top
    CHECK(inside(button, area));
    // The same rect whatever the view: it is a function of the area alone.
    CHECK(same_rect(button, sheet_view_button(area, corner)));
    for (const ViewMode mode : {ViewMode::Sheet, ViewMode::Both}) {
        const SheetPlan p = plan_sheet(area, mode, true, true, kDefaultPxPerMm, corner);
        REQUIRE(p.shown);
        // Never over the paper or the two toggles, and a dead gap of the cells' pitch from them.
        CHECK(button.x >= p.paper.right());
        const bool over_x = button.x < p.corner.right() && p.corner.x < button.right();
        const bool over_y = button.y < p.corner.bottom() && p.corner.y < button.bottom();
        const bool over = over_x && over_y;
        CHECK_FALSE(over);
    }
    // No area or no size: none.
    CHECK(sheet_view_button({}, corner).empty());
    CHECK(sheet_view_button(area, 0.0f).empty());
    // A short strip (800x480's Both) puts the two toggles in a row to the button's left, which keeps
    // the button where it was.
    const RectF small{0.0f, 40.0f, 800.0f, 150.0f};
    const SheetPlan strip = plan_sheet(small, ViewMode::Both, true, true, kDefaultPxPerMm, corner);
    if (strip.shown) {
        const RectF b = sheet_view_button(small, corner);
        CHECK(b.x >= strip.paper.right());
        const bool across = b.x < strip.corner.right() && strip.corner.x < b.right();
        const bool down = b.y < strip.corner.bottom() && strip.corner.y < b.bottom();
        const bool overlap = across && down;
        CHECK_FALSE(overlap);
    }
}

namespace {

// The gap between two cells along one axis (0: they touch).
float gap_between(const RectF& a, const RectF& b) {
    const float across = std::max(b.x - a.right(), a.x - b.right());
    const float down = std::max(b.y - a.bottom(), a.y - b.bottom());
    return std::max(across, down);
}

} // namespace

TEST_CASE("side buttons: a tall room is one column, the button on top, cells touching") {
    const float corner = 44.0f;
    const RectF area{0.0f, 57.0f, 1366.0f, 570.0f};
    const SheetSideButtons s = sheet_side_buttons(area, corner, area.bottom());
    CHECK(s.column);
    CHECK(same_rect(s.view, sheet_view_button(area, corner)));
    // One x for all three, one pitch down, in the order button, names, colours.
    CHECK(s.names.x == doctest::Approx(s.view.x));
    CHECK(s.colours.x == doctest::Approx(s.view.x));
    CHECK(s.names.y == doctest::Approx(s.view.bottom()));
    CHECK(s.colours.y == doctest::Approx(s.names.bottom()));
    CHECK(s.block.h == doctest::Approx(3.0f * corner));
    CHECK(s.block.w == doctest::Approx(corner));
    CHECK(same_rect(s.toggles, RectF{s.names.x, s.names.y, corner, 2.0f * corner}));
}

TEST_CASE("side buttons: a room under three cells tall is one row, the button at its right end") {
    const float corner = 44.0f;
    const RectF area{0.0f, 40.0f, 800.0f, 150.0f};
    const float inset = std::round(corner * kSheetColumnInsetShare);
    // Just short of three cells down from the button's top: a row.
    const float bottom = area.y + inset + 3.0f * corner - 1.0f;
    const SheetSideButtons s = sheet_side_buttons(area, corner, bottom);
    CHECK_FALSE(s.column);
    // The button stays where it stands in the column; the switches run left from it, names first.
    CHECK(same_rect(s.view, sheet_view_button(area, corner)));
    CHECK(s.names.y == doctest::Approx(s.view.y));
    CHECK(s.colours.y == doctest::Approx(s.view.y));
    CHECK(s.colours.right() == doctest::Approx(s.view.x));
    CHECK(s.names.right() == doctest::Approx(s.colours.x));
    CHECK(s.block.w == doctest::Approx(3.0f * corner));
    CHECK(s.block.h == doctest::Approx(corner));
    // Exactly three cells down fits a column.
    CHECK(sheet_side_buttons(area, corner, bottom + 1.0f).column);
}

TEST_CASE("side buttons: the gaps between hit areas are equal, in a column and in a row") {
    const float corner = 44.0f;
    const float dead = 8.0f; // a hit area is its cell less the dead gap, centred
    const RectF area{0.0f, 57.0f, 1366.0f, 570.0f};
    for (const float bottom : {area.bottom(), 120.0f}) {
        const SheetSideButtons s = sheet_side_buttons(area, corner, bottom);
        CHECK(s.column == (bottom > 120.0f));
        const auto hit = [&](const RectF& c) {
            return RectF{c.x + dead * 0.5f, c.y + dead * 0.5f, c.w - dead, c.h - dead};
        };
        // In the order they stand in: neighbours are one pitch apart, so every gap is the same.
        const RectF first = s.column ? s.view : s.names;
        const RectF second = s.column ? s.names : s.colours;
        const RectF third = s.column ? s.colours : s.view;
        const float g1 = gap_between(hit(first), hit(second));
        const float g2 = gap_between(hit(second), hit(third));
        CHECK(g1 == doctest::Approx(dead));
        CHECK(g2 == doctest::Approx(g1));
    }
}

TEST_CASE("side buttons: in the plan they are beside the paper, in a column or a row, never an L") {
    const float corner = 44.0f;
    const RectF areas[] = {RectF{0.0f, 57.0f, 1920.0f, 800.0f}, RectF{0.0f, 57.0f, 1366.0f, 570.0f},
                           RectF{0.0f, 40.0f, 800.0f, 300.0f}};
    for (const RectF area : areas) {
        for (const ViewMode mode : {ViewMode::Sheet, ViewMode::Both}) {
            const SheetPlan p = plan_sheet(area, mode, true, true, kDefaultPxPerMm, corner);
            REQUIRE(p.shown);
            const SheetSideButtons& s = p.side;
            CHECK(inside(s.block, p.sheet));
            // The rule: a column exactly where three cells fit under the button's top.
            CHECK(s.column == (s.view.y + 3.0f * corner <= p.sheet.bottom()));
            // All three share one x (column) or one y (row): no L.
            CHECK((s.column ? (s.names.x == s.view.x && s.colours.x == s.view.x)
                            : (s.names.y == s.view.y && s.colours.y == s.view.y)));
            CHECK(same_rect(p.corner, s.toggles));
            // Nothing lies on the music: the paper ends before the block, a gap short of it.
            CHECK(p.paper.right() <= s.block.x);
            CHECK(p.paper.right() + 0.01f < s.block.x);
            const float margin = p.staff_space_px * kSheetMarginSpaces;
            CHECK(p.paper.x + 2.0f * margin + p.system_width_px <= p.paper.right() + 0.01f);
        }
    }
    // The Both strip at 800x480 is under three cells tall: a row; the Sheet view is a column.
    const RectF small{0.0f, 40.0f, 800.0f, 200.0f};
    CHECK_FALSE(plan_sheet(small, ViewMode::Both, true, true, kDefaultPxPerMm, corner).side.column);
    CHECK(plan_sheet(small, ViewMode::Sheet, true, true, kDefaultPxPerMm, corner).side.column);
    // None asked for, or no area: none.
    CHECK(plan_sheet(small, ViewMode::Sheet, true, true, kDefaultPxPerMm).side.block.empty());
    CHECK(sheet_side_buttons({}, corner, 100.0f).block.empty());
    CHECK(sheet_side_buttons(small, 0.0f, 100.0f).block.empty());
}
