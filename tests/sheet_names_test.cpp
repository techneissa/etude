// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_names.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace etude;

namespace {

// A grand staff system 1000 px wide: the top line at y 100, the bottom line at y 300.
SheetLayout grand_staff() {
    SheetLayout l;
    SheetSystem s;
    s.width = 1000.0f;
    s.height = 400.0f;
    s.staff_top = 100.0f;
    s.staff_bottom = 300.0f;
    s.staves = 2;
    l.systems.push_back(s);
    return l;
}

std::uint32_t add_note(SheetLayout& l, int staff, int bar, double offset, int pitch, float x, float y) {
    SheetMark m;
    m.kind = SheetMarkKind::Note;
    m.staff = staff;
    m.bar = bar;
    m.offset_q = offset;
    m.pitch = pitch;
    m.head = {x - 6.0f, y - 5.0f, 12.0f, 10.0f};
    m.head_x = x;
    m.box = m.head;
    l.marks.push_back(m);
    l.obstacles.push_back({0, m.head});
    return static_cast<std::uint32_t>(l.marks.size() - 1);
}

SheetNameStyle style() {
    SheetNameStyle st;
    st.size = 20.0f;
    st.gap = 2.0f;
    st.line_gap = 1.0f;
    st.clear = 15.0f;
    st.top_limit = -100.0f;
    st.bottom_limit = 500.0f;
    return st;
}

// The name of `mark` (never a "…", which carries a mark of its column for its colour).
const SheetName* name_of(const SheetNames& n, std::uint32_t mark) {
    for (const SheetName& s : n.names) {
        if (s.mark == mark && s.kind != SheetNameKind::More) {
            return &s;
        }
    }
    return nullptr;
}

bool overlap(const RectF& a, const RectF& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

// Columns, then names, as the app places them (chord symbols 1.5 wide at size 1).
void place(const SheetLayout& l, int system, const std::vector<float>& w, const SheetNameStyle& st,
           SheetNames& n, bool chords = false) {
    std::vector<SheetNameColumn> columns;
    sheet_name_columns(l, system, w, chords, columns);
    for (SheetNameColumn& c : columns) {
        if (c.chord.found) {
            c.symbol = 7;
            c.symbol_unit_width = 1.5f;
        }
    }
    place_sheet_names(l, system, columns, w, st, n);
}

int count(const SheetNames& n, SheetNameKind kind) {
    int k = 0;
    for (const SheetName& s : n.names) {
        k += s.kind == kind ? 1 : 0;
    }
    return k;
}

} // namespace

TEST_CASE("sheet names: treble names above the system, bass names below, clear of the cursor") {
    SheetLayout l = grand_staff();
    const auto e = add_note(l, 0, 0, 0.0, 64, 100.0f, 140.0f);
    const auto c = add_note(l, 1, 0, 0.0, 48, 100.0f, 260.0f);
    const std::vector<float> w(l.marks.size(), 0.8f);
    SheetNames n;
    place(l, 0, w, style(), n);
    REQUIRE(n.names.size() == 2);
    CHECK(n.size == 20.0f);
    CHECK(n.dropped == 0);
    const SheetName* top = name_of(n, e);
    const SheetName* bottom = name_of(n, c);
    REQUIRE(top != nullptr);
    REQUIRE(bottom != nullptr);
    // In the lane: `clear` outside the outer staff lines, where the cursor never reaches.
    CHECK(top->box.bottom() == doctest::Approx(100.0f - 15.0f));
    CHECK(bottom->box.y == doctest::Approx(300.0f + 15.0f));
    // Centred on the head, as wide as the name.
    CHECK(top->box.x + top->box.w * 0.5f == doctest::Approx(100.0f));
    CHECK(top->box.w == doctest::Approx(16.0f));
}

TEST_CASE("sheet names: a chord stacks its names, the highest pitch on top") {
    SheetLayout l = grand_staff();
    const auto g = add_note(l, 0, 0, 1.0, 67, 200.0f, 125.0f);
    const auto c = add_note(l, 0, 0, 1.0, 60, 200.0f, 165.0f);
    const auto e = add_note(l, 0, 0, 1.0, 64, 202.0f, 145.0f);
    const std::vector<float> w(l.marks.size(), 0.8f);
    SheetNames n;
    place(l, 0, w, style(), n);
    REQUIRE(n.names.size() == 3);
    const RectF& rg = name_of(n, g)->box;
    const RectF& re = name_of(n, e)->box;
    const RectF& rc = name_of(n, c)->box;
    CHECK(rg.bottom() < re.y);
    CHECK(re.bottom() < rc.y);
    CHECK(rc.bottom() <= 100.0f - 15.0f + 0.01f);
    CHECK_FALSE(overlap(rg, re));
}

TEST_CASE("sheet names: a stem or a ledger line sticking out pushes the column further out") {
    SheetLayout l = grand_staff();
    const auto a = add_note(l, 0, 0, 0.0, 81, 300.0f, 70.0f); // high A on ledger lines
    l.obstacles.push_back({0, {290.0f, 79.0f, 20.0f, 2.0f}}); // a ledger line
    l.obstacles.push_back({0, {305.0f, 40.0f, 2.0f, 30.0f}}); // its stem, up to y 40
    const auto b = add_note(l, 1, 0, 0.0, 40, 300.0f, 290.0f);
    l.obstacles.push_back({0, {293.0f, 290.0f, 2.0f, 50.0f}}); // a stem down to y 340
    const std::vector<float> w(l.marks.size(), 0.8f);
    SheetNames n;
    place(l, 0, w, style(), n);
    REQUIRE(n.names.size() == 2);
    CHECK(name_of(n, a)->box.bottom() <= 40.0f - 2.0f + 0.01f);
    CHECK(name_of(n, b)->box.y >= 340.0f + 2.0f - 0.01f);
    for (const SheetObstacle& o : l.obstacles) {
        CHECK_FALSE(overlap(name_of(n, a)->box, o.box));
        CHECK_FALSE(overlap(name_of(n, b)->box, o.box));
    }
}

TEST_CASE("sheet names: a crowded column is staggered past its neighbour at its size") {
    // Two notes 30 px apart with names 2.0 wide (40 px at size 20): side by side they would overlap.
    SheetLayout l = grand_staff();
    const auto a = add_note(l, 0, 0, 0.0, 60, 100.0f, 165.0f);
    const auto b = add_note(l, 0, 0, 1.0, 62, 130.0f, 160.0f);
    SheetNames n;
    place(l, 0, std::vector<float>(2, 2.0f), style(), n);
    REQUIRE(n.names.size() == 2);
    CHECK(n.dropped == 0);
    CHECK(n.staggered == 1);
    const RectF& ra = name_of(n, a)->box;
    const RectF& rb = name_of(n, b)->box;
    CHECK(ra.h == doctest::Approx(20.0f)); // the one size, both
    CHECK(rb.h == doctest::Approx(20.0f));
    CHECK(ra.bottom() == doctest::Approx(100.0f - 15.0f)); // the left one in the lane
    CHECK(rb.bottom() <= ra.y - 2.0f + 0.01f);             // the right one out past it
    CHECK_FALSE(overlap(ra, rb));
    // Far enough apart, neither moves.
    SheetLayout l2 = grand_staff();
    add_note(l2, 0, 0, 0.0, 60, 100.0f, 165.0f);
    add_note(l2, 0, 0, 1.0, 62, 160.0f, 160.0f);
    place(l2, 0, std::vector<float>(2, 2.0f), style(), n);
    CHECK(n.staggered == 0);
    CHECK(n.names[0].box.y == doctest::Approx(n.names[1].box.y));
}

TEST_CASE("sheet names: a column too tall for its slot slopes, each name a step sideways") {
    SheetLayout l = grand_staff();
    for (int p = 0; p < 4; ++p) {
        add_note(l, 0, 0, 0.0, 60 + 4 * p, 100.0f, 160.0f - 10.0f * static_cast<float>(p));
    }
    SheetNameStyle st = style();
    st.min_size = 16.0f;
    st.top_limit = 0.0f; // the lane above holds 100 - 15 = 85 px: 4 names take 4 * 20 + 3 = 83
    SheetNames n;
    place(l, 0, std::vector<float>(4, 0.8f), st, n);
    CHECK(n.names.size() == 4);
    CHECK(n.sloped == 0);
    st.clear = 40.0f; // 60 px left: straight they take 83, on the gentle slope 20 + 3 * 12 = 56
    place(l, 0, std::vector<float>(4, 0.8f), st, n);
    REQUIRE(n.names.size() == 4);
    CHECK(n.sloped == 4);
    CHECK(n.smaller == 0);
    CHECK(n.dropped == 0);
    // From the staff out, the lowest first: each name further out and clear to the side of the last.
    for (std::size_t i = 1; i < n.names.size(); ++i) {
        const RectF& a = n.names[i - 1].box;
        const RectF& b = n.names[i].box;
        CHECK(l.marks[n.names[i].mark].pitch > l.marks[n.names[i - 1].mark].pitch);
        CHECK(b.y < a.y);
        CHECK((b.x >= a.right() + st.gap - 0.01f || b.right() <= a.x - st.gap + 0.01f));
        CHECK(b.h == doctest::Approx(20.0f));
    }
    for (const SheetName& name : n.names) {
        CHECK(name.box.y >= st.top_limit);
        CHECK(name.box.bottom() <= 100.0f - st.clear + 0.01f);
    }
}

TEST_CASE("sheet names: then a step smaller, never under the floor") {
    SheetLayout l = grand_staff();
    for (int p = 0; p < 4; ++p) {
        add_note(l, 0, 0, 0.0, 60 + 4 * p, 100.0f, 160.0f - 10.0f * static_cast<float>(p));
    }
    SheetNameStyle st = style();
    st.min_size = 16.0f;
    st.top_limit = 0.0f;
    st.clear = 60.0f; // 40 px: the steepest slope at 20 takes 44; at 18, 39.6
    SheetNames n;
    place(l, 0, std::vector<float>(4, 0.8f), st, n);
    REQUIRE(n.names.size() == 4);
    CHECK(n.smaller == 4);
    CHECK(n.sloped == 4);
    CHECK(n.size == 20.0f); // the full size, as asked
    for (const SheetName& name : n.names) {
        CHECK(name.size < 20.0f);
        CHECK(name.size >= 16.0f);
        CHECK(name.box.y >= 0.0f);
    }
}

TEST_CASE("sheet names: names that never fit give way to a quiet mark, the outermost kept") {
    // Above: room for one name at the floor and the mark, not two.
    SheetLayout l = grand_staff();
    std::uint32_t top = 0;
    for (int p = 0; p < 4; ++p) {
        top = add_note(l, 0, 0, 0.0, 60 + 4 * p, 100.0f, 160.0f - 10.0f * static_cast<float>(p));
    }
    SheetNameStyle st = style();
    st.min_size = 16.0f;
    st.top_limit = 0.0f;
    st.clear = 75.0f; // 25 px
    SheetNames n;
    place(l, 0, std::vector<float>(4, 0.8f), st, n);
    REQUIRE(n.names.size() == 2);
    CHECK(n.marks == 1);
    CHECK(n.left_out == 3);
    CHECK(n.dropped == 0);
    const SheetName* kept = name_of(n, top);
    REQUIRE(kept != nullptr);
    CHECK(kept->kind == SheetNameKind::Note); // the chord's top note
    const SheetName& more = n.names[0];
    CHECK(more.kind == SheetNameKind::More);
    CHECK(more.box.y > kept->box.bottom()); // the mark nearest the staff, where names are missing
    CHECK(more.box.h < 0.5f * more.size);   // a line of dots, not a name
    // Below: the bass is kept.
    SheetLayout b = grand_staff();
    std::uint32_t bass = 0;
    for (int p = 3; p >= 0; --p) {
        bass = add_note(b, 1, 0, 0.0, 36 + 4 * p, 100.0f, 240.0f + 10.0f * static_cast<float>(3 - p));
    }
    st = style();
    st.min_size = 16.0f;
    st.bottom_limit = 400.0f;
    st.clear = 75.0f;
    place(b, 0, std::vector<float>(4, 0.8f), st, n);
    REQUIRE(name_of(n, bass) != nullptr);
    CHECK(name_of(n, bass)->kind == SheetNameKind::Note);
    CHECK(n.marks == 1);
}

TEST_CASE("sheet names: a mark with no room over its column sits beside it, clear of everything") {
    SheetLayout l = grand_staff();
    add_note(l, 0, 0, 0.0, 72, 100.0f, 140.0f);
    l.obstacles.push_back({0, {97.0f, 0.0f, 6.0f, 100.0f}}); // a stem up to the slot's edge
    SheetNameStyle st = style();
    st.min_size = 16.0f;
    st.top_limit = 50.0f;
    SheetNames n;
    place(l, 0, std::vector<float>(1, 0.8f), st, n);
    REQUIRE(n.names.size() == 1);
    CHECK(n.names[0].kind == SheetNameKind::More);
    CHECK(n.dropped == 0);
    CHECK(std::abs(n.names[0].box.x + n.names[0].box.w * 0.5f - 100.0f) < 4.0f * 0.75f * 16.0f);
    for (const SheetObstacle& o : l.obstacles) {
        CHECK_FALSE(overlap(n.names[0].box, o.box));
    }
}

TEST_CASE("sheet names: a recognised chord shows one symbol per staff, a cluster its names") {
    SheetLayout l = grand_staff();
    // Treble: E G C over E, the first inversion of C. Bass: C and G, two pitch classes: names.
    add_note(l, 0, 0, 0.0, 64, 100.0f, 160.0f);
    add_note(l, 0, 0, 0.0, 67, 100.0f, 150.0f);
    add_note(l, 0, 0, 0.0, 72, 100.0f, 130.0f);
    add_note(l, 1, 0, 0.0, 36, 100.0f, 290.0f);
    add_note(l, 1, 0, 0.0, 43, 100.0f, 270.0f);
    // A cluster, C C# D, later in the treble: never a chord.
    add_note(l, 0, 0, 1.0, 60, 400.0f, 170.0f);
    add_note(l, 0, 0, 1.0, 61, 400.0f, 170.0f);
    add_note(l, 0, 0, 1.0, 62, 400.0f, 165.0f);
    const std::vector<float> w(l.marks.size(), 0.8f);
    std::vector<SheetNameColumn> columns;
    sheet_name_columns(l, 0, w, true, columns);
    REQUIRE(columns.size() == 3);
    int chords = 0;
    for (const SheetNameColumn& c : columns) {
        if (c.chord.found) {
            ++chords;
            CHECK(c.above);
            CHECK(chord_name(c.chord) == "C/E");
        }
    }
    CHECK(chords == 1);
    SheetNames n;
    place(l, 0, w, style(), n, true);
    CHECK(n.symbols == 1);
    CHECK(count(n, SheetNameKind::Symbol) == 1);
    CHECK(count(n, SheetNameKind::Note) == 5); // the bass's two, the cluster's three
    for (const SheetName& s : n.names) {
        if (s.kind == SheetNameKind::Symbol) {
            CHECK(s.symbol == 7);
            CHECK(s.box.bottom() <= 85.0f + 0.01f);
            CHECK(s.box.w == doctest::Approx(30.0f));
        }
    }
    // Off, the chord keeps its names.
    place(l, 0, w, style(), n, false);
    CHECK(n.symbols == 0);
    CHECK(count(n, SheetNameKind::Note) == 8);
    // A bare fifth or two notes are never a symbol.
    CHECK_FALSE(sheet_column_chord(std::vector<int>{48, 55, 60}).found);
    CHECK(sheet_column_chord(std::vector<int>{55, 59, 62, 65}).found); // G7
}

TEST_CASE("sheet names: one staff puts every name above; marks without a width get none") {
    SheetLayout l = grand_staff();
    l.systems[0].staves = 1;
    const auto a = add_note(l, 0, 0, 0.0, 60, 100.0f, 165.0f);
    const auto b = add_note(l, 0, 0, 1.0, 62, 300.0f, 160.0f);
    std::vector<float> w(l.marks.size(), 0.8f);
    w[b] = 0.0f;
    SheetNames n;
    place(l, 0, w, style(), n);
    REQUIRE(n.names.size() == 1);
    CHECK(n.names[0].mark == a);
    CHECK(n.names[0].box.bottom() <= 85.0f + 0.01f);
    place(l, 1, w, style(), n); // no such system
    CHECK(n.names.empty());
}

TEST_CASE("sheet names: moving out past obstacles always ends, on awkward sizes too") {
    SheetLayout l = grand_staff();
    const auto a = add_note(l, 0, 0, 0.0, 72, 300.0f, 90.0f);
    // Obstacles stacked above the lane at sizes that do not add up exactly in floats.
    for (int i = 0; i < 40; ++i) {
        const float y = 80.0f - 3.3f * static_cast<float>(i) - 0.1f / 3.0f;
        l.obstacles.push_back({0, {295.0f + 0.37f * static_cast<float>(i % 3), y, 9.7f, 1.3f}});
    }
    SheetNameStyle st = style();
    st.gap = 0.7f;
    st.size = 13.7f;
    st.top_limit = -1000.0f;
    SheetNames n;
    place(l, 0, std::vector<float>(l.marks.size(), 0.77f), st, n);
    REQUIRE(n.names.size() == 1);
    CHECK(n.names[0].mark == a);
    for (const SheetObstacle& o : l.obstacles) {
        CHECK_FALSE(overlap(n.names[0].box, o.box));
    }
}

TEST_CASE("sheet names: the lanes are planned for the most notes starting together per staff") {
    Song song;
    const auto add = [&](std::int64_t start, int pitch, Hand hand) {
        Note n;
        n.start_tick = start;
        n.end_tick = start + 100;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.hand = hand;
        song.notes.push_back(n);
    };
    add(0, 48, Hand::Left);
    add(0, 52, Hand::Left);
    add(0, 60, Hand::Right);
    add(0, 64, Hand::Right);
    add(0, 67, Hand::Right);
    add(100, 72, Hand::Right);
    add(200, 36, Hand::Left);
    add(200, 40, Hand::Left);
    add(200, 43, Hand::Left);
    add(200, 47, Hand::Left);
    Note drum;
    drum.start_tick = 200;
    drum.drum = true;
    song.notes.push_back(drum);
    const SheetNameStacks two = sheet_name_stacks(song, true);
    CHECK(two.above == 3);
    CHECK(two.below == 4);
    const SheetNameStacks one = sheet_name_stacks(song, false);
    CHECK(one.above == 5); // both hands on one staff at tick 0
    CHECK(one.below == 0);
    // With chord symbols a recognised chord takes one line: C E G above; below, C E stays two
    // names and C E G B (Cmaj7) is one.
    const SheetNameStacks chords = sheet_name_stacks(song, true, true);
    CHECK(chords.above == 1);
    CHECK(chords.below == 2);
}
