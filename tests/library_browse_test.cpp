// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_browse.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

constexpr std::int32_t kToday = 20000;

std::vector<std::uint32_t> browse(const std::vector<BrowseSong>& songs, BrowseOrder order,
                                  std::span<const std::uint8_t> include = {}) {
    LibraryBrowser b;
    b.browse(songs, order, kToday, include);
    return b.order();
}

// Five songs named A..E (name ranks 0..4), with what the sorts need.
std::vector<BrowseSong> five() {
    std::vector<BrowseSong> s(5);
    for (std::uint32_t i = 0; i < 5; ++i) {
        s[i].name_rank = i;
    }
    // Composers: B and D share rank 1, A rank 0, the others none.
    s[0].composer_rank = 0;
    s[1].composer_rank = 1;
    s[3].composer_rank = 1;
    s[0].difficulty = 3;
    s[1].difficulty = 1;
    s[2].difficulty = 5;
    s[3].difficulty = 0; // not read yet
    s[4].difficulty = 1;
    s[0].duration_s = 120.0;
    s[1].duration_s = 60.0;
    s[2].duration_s = 0.0; // not known
    s[3].duration_s = 300.0;
    s[4].duration_s = 60.0;
    s[1].recency = 4;
    s[1].last_day = kToday;
    s[1].best_permille = 950;
    s[2].recency = 9;
    s[2].last_day = kToday - 40;
    s[2].best_permille = 400;
    s[4].recency = 2;
    s[4].last_day = kToday - 29;
    s[0].added_s = 1000;
    s[1].added_s = 3000;
    s[4].added_s = 2000;
    s[3].favourite = true;
    return s;
}

} // namespace

TEST_CASE("browse: a tap on the sort that is on turns it round, another starts its own way") {
    BrowseOrder o;
    o = with_sort(o, BrowseSort::Name);
    CHECK(o.reversed);
    o = with_sort(o, BrowseSort::Name);
    CHECK(!o.reversed);
    o = with_sort(o, BrowseSort::Name);
    o = with_sort(o, BrowseSort::Length);
    CHECK(o.sort == BrowseSort::Length);
    CHECK(!o.reversed);
}

TEST_CASE("browse: every sort in its natural direction, ties by name") {
    const auto s = five();
    CHECK(browse(s, {BrowseSort::Name}) == std::vector<std::uint32_t>{0, 1, 2, 3, 4});
    CHECK(browse(s, {BrowseSort::Composer}) == std::vector<std::uint32_t>{0, 1, 3, 2, 4});
    CHECK(browse(s, {BrowseSort::Difficulty}) == std::vector<std::uint32_t>{1, 4, 0, 2, 3});
    CHECK(browse(s, {BrowseSort::Progress}) == std::vector<std::uint32_t>{1, 2, 0, 3, 4});
    CHECK(browse(s, {BrowseSort::Length}) == std::vector<std::uint32_t>{1, 4, 0, 3, 2});
    CHECK(browse(s, {BrowseSort::Recent}) == std::vector<std::uint32_t>{2, 1, 4, 0, 3});
    CHECK(browse(s, {BrowseSort::Added}) == std::vector<std::uint32_t>{1, 4, 0, 2, 3});
}

TEST_CASE("browse: turned round, a song with no value stays last") {
    const auto s = five();
    CHECK(browse(s, {BrowseSort::Name, true}) == std::vector<std::uint32_t>{4, 3, 2, 1, 0});
    CHECK(browse(s, {BrowseSort::Composer, true}) == std::vector<std::uint32_t>{1, 3, 0, 2, 4});
    CHECK(browse(s, {BrowseSort::Difficulty, true}) == std::vector<std::uint32_t>{2, 0, 1, 4, 3});
    CHECK(browse(s, {BrowseSort::Progress, true}) == std::vector<std::uint32_t>{2, 1, 0, 3, 4});
    CHECK(browse(s, {BrowseSort::Length, true}) == std::vector<std::uint32_t>{3, 0, 1, 4, 2});
    CHECK(browse(s, {BrowseSort::Recent, true}) == std::vector<std::uint32_t>{4, 1, 2, 0, 3});
    CHECK(browse(s, {BrowseSort::Added, true}) == std::vector<std::uint32_t>{0, 4, 1, 2, 3});
}

TEST_CASE("browse: the quick filters") {
    const auto s = five();
    CHECK(browse(s, {BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::Favourites}) ==
          std::vector<std::uint32_t>{3});
    // Played today and 29 days ago are recent; 40 days ago is not.
    CHECK(browse(s, {BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::Recent}) ==
          std::vector<std::uint32_t>{1, 4});
    // Mastered (95%) is not in progress; 40% and a played song with no score are.
    CHECK(browse(s, {BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::InProgress}) ==
          std::vector<std::uint32_t>{2, 4});
    const std::vector<std::uint8_t> include = {1, 1, 0, 1, 1};
    CHECK(browse(s, {}, include) == std::vector<std::uint32_t>{0, 1, 3, 4});
}

TEST_CASE("browse: progress buckets and the recent boundary") {
    BrowseSong never;
    CHECK(progress_of(never) == Progress::NotStarted);
    BrowseSong played;
    played.recency = 0;
    CHECK(progress_of(played) == Progress::InProgress);
    played.best_permille = kMasteredPermille - 1;
    CHECK(progress_of(played) == Progress::InProgress);
    played.best_permille = kMasteredPermille;
    CHECK(progress_of(played) == Progress::Mastered);
    played.last_day = kToday - (kRecentDays - 1);
    CHECK(passes_filter(played, BrowseFilter::Recent, kToday));
    played.last_day = kToday - kRecentDays;
    CHECK(!passes_filter(played, BrowseFilter::Recent, kToday));
}

TEST_CASE("browse: groups in their fixed order, sorted inside") {
    const auto s = five();
    LibraryBrowser b;
    b.browse(s, {BrowseSort::Name, false, BrowseGroup::Composer}, kToday);
    CHECK(b.order() == std::vector<std::uint32_t>{0, 1, 3, 2, 4});
    REQUIRE(b.sections().size() == 3);
    CHECK(b.sections()[0].key == 0);
    CHECK(b.sections()[1].key == 1);
    CHECK(b.sections()[1].begin == 1);
    CHECK(b.sections()[1].end == 3);
    CHECK(b.sections()[2].key == kNoRank); // "Unknown composer" last
    // Reversing the sort turns the songs inside each group, never the groups.
    b.browse(s, {BrowseSort::Name, true, BrowseGroup::Composer}, kToday);
    CHECK(b.order() == std::vector<std::uint32_t>{0, 3, 1, 4, 2});
    b.browse(s, {BrowseSort::Name, false, BrowseGroup::Difficulty}, kToday);
    CHECK(b.order() == std::vector<std::uint32_t>{1, 4, 0, 2, 3});
    REQUIRE(b.sections().size() == 4);
    CHECK(b.sections()[0].key == 1);
    CHECK(b.sections()[3].key == 0); // not read yet: last
    b.browse(s, {BrowseSort::Name, false, BrowseGroup::Progress}, kToday);
    REQUIRE(b.sections().size() == 3);
    CHECK(b.sections()[0].key == static_cast<std::uint32_t>(Progress::NotStarted));
    CHECK(b.sections()[2].key == static_cast<std::uint32_t>(Progress::Mastered));
    b.browse(s, {BrowseSort::Name, false, BrowseGroup::Genre}, kToday);
    REQUIRE(b.sections().size() == 1);
    CHECK(b.sections()[0].key == kNoRank); // every song is "Other"
    b.browse(s, {}, kToday);
    REQUIRE(b.sections().size() == 1);
    b.browse(s, {BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::Favourites}, kToday,
             std::vector<std::uint8_t>{1, 1, 1, 0, 1});
    CHECK(b.order().empty());
    CHECK(b.sections().empty());
}

TEST_CASE("browse: search results stay in their ranked order") {
    LibraryBrowser b;
    const std::vector<SearchHit> hits = {{4, MatchRank::ExactTitle, 0}, {1, MatchRank::Typo, 3}};
    b.ranked(hits);
    CHECK(b.order() == std::vector<std::uint32_t>{4, 1});
    REQUIRE(b.sections().size() == 1);
    b.ranked({});
    CHECK(b.sections().empty());
}

TEST_CASE("grid: as many columns as fit the smallest card") {
    // 1366 wide, 16 a side, cards of at least 180 with 12 between: (1334 + 12) / 192 = 7.
    GridColumns g = grid_columns(1366.0f, 180.0f, 12.0f, 16.0f);
    CHECK(g.columns == 7);
    CHECK(g.card_w >= 180.0f);
    CHECK(g.x(0) == doctest::Approx(16.0f));
    CHECK(g.x(6) + g.card_w == doctest::Approx(1366.0f - 16.0f));
    // Exactly enough for two cards and a gap: two columns; a hair less: one.
    g = grid_columns(2.0f * 180.0f + 12.0f + 32.0f, 180.0f, 12.0f, 16.0f);
    CHECK(g.columns == 2);
    CHECK(g.card_w == doctest::Approx(180.0f));
    g = grid_columns(2.0f * 180.0f + 12.0f + 31.0f, 180.0f, 12.0f, 16.0f);
    CHECK(g.columns == 1);
    // Narrower than one card: one column, as wide as there is room.
    g = grid_columns(100.0f, 180.0f, 12.0f, 16.0f);
    CHECK(g.columns == 1);
    CHECK(g.card_w == doctest::Approx(68.0f));
}

TEST_CASE("grid: rows, visible cards and the sticky header") {
    // Two sections of 5 and 2 cards in 2 columns: card 100, gap 10, header 30.
    const std::vector<BrowseSection> sections = {{0, 0, 5}, {1, 5, 7}};
    GridRows rows;
    rows.build(sections, 2, 100.0f, 10.0f, 30.0f, true);
    // Section 0: header 0..30, rows at 30, 140, 250 (ends 350). Section 1: header 360..390, row
    // 390.
    CHECK(rows.section_top(0) == 0.0f);
    CHECK(rows.section_top(1) == doctest::Approx(360.0f));
    CHECK(rows.content_height() == doctest::Approx(490.0f));
    CHECK(rows.place(0).y == doctest::Approx(30.0f));
    CHECK(rows.place(3).y == doctest::Approx(140.0f));
    CHECK(rows.place(3).column == 1);
    CHECK(rows.place(4).y == doctest::Approx(250.0f));
    CHECK(rows.place(6).y == doctest::Approx(390.0f));
    CHECK(rows.place(6).column == 1);
    // The view 0..100 holds the first row only; 130..135 falls in a gap and holds nothing.
    GridRows::Range r = rows.visible(0.0f, 100.0f);
    CHECK(r.first == 0);
    CHECK(r.last == 2);
    r = rows.visible(131.0f, 139.0f);
    CHECK(r.first == r.last);
    r = rows.visible(200.0f, 400.0f);
    CHECK(r.first == 2);
    CHECK(r.last == 7);
    r = rows.visible(500.0f, 900.0f);
    CHECK(r.first == r.last);
    // Headers: none sticks at the top; section 0's sticks once scrolled; section 1's header
    // pushes it up as it arrives; then section 1's sticks.
    CHECK(rows.sticky(0.0f).section == -1);
    CHECK(rows.sticky(50.0f).section == 0);
    CHECK(rows.sticky(50.0f).y == doctest::Approx(50.0f));
    CHECK(rows.sticky(340.0f).section == 0);
    CHECK(rows.sticky(340.0f).y == doctest::Approx(330.0f));
    CHECK(rows.sticky(370.0f).section == 1);
    CHECK(rows.sticky(370.0f).y == doctest::Approx(370.0f));
    // Without headers nothing sticks and the cards start at the top.
    rows.build(sections, 2, 100.0f, 10.0f, 30.0f, false);
    CHECK(rows.sticky(200.0f).section == -1);
    CHECK(rows.place(0).y == 0.0f);
}

TEST_CASE("browse: composers sort by surname") {
    CHECK(composer_sort_key("Ludwig van Beethoven") == "beethoven ludwig van beethoven");
    CHECK(composer_sort_key("Bach") == "bach bach");
    CHECK(composer_sort_key("Martin Luther King Jr.") == "king martin luther king");
    CHECK(composer_sort_key("G. F. Händel (arr. Smith)") == "handel g. f. handel");
    CHECK(composer_sort_key("") == "");
    CHECK(composer_sort_key("Johann Strauss II") == "strauss johann strauss");
    CHECK(composer_sort_key("Johann Sebastian Bach") < composer_sort_key("Ludwig van Beethoven"));
}

TEST_CASE("browse: the level and hands filters, and their count for the chip") {
    std::vector<BrowseSong> s = five();
    s[0].left = s[0].right = true; // A: both hands, level 3
    s[1].right = true;             // B: right only, level 1
    s[2].left = true;              // C: left only, level 5
    s[4].left = s[4].right = true; // E: both, level 1 (D: none, not read)
    BrowseOrder o;
    CHECK(facet_count(o) == 0);
    o.levels = 1u << 0; // level 1
    CHECK(browse(s, o) == std::vector<std::uint32_t>{1, 4});
    CHECK(facet_count(o) == 1);
    o.levels = (1u << 2) | (1u << 4); // 3 and 5
    CHECK(browse(s, o) == std::vector<std::uint32_t>{0, 2});
    o.levels = kAllLevels;
    o.hands = BrowseHands::Both;
    CHECK(browse(s, o) == std::vector<std::uint32_t>{0, 4});
    o.hands = BrowseHands::RightOnly;
    CHECK(browse(s, o) == std::vector<std::uint32_t>{1});
    o.hands = BrowseHands::LeftOnly;
    o.levels = 1u << 4;
    CHECK(browse(s, o) == std::vector<std::uint32_t>{2});
    CHECK(facet_count(o) == 2);
}

TEST_CASE("browse: my groups, a song in each of its groups, the rest last") {
    const std::vector<BrowseSong> s = five();
    // Group 0: E, A, C; group 1: nothing that passes; group 2: B, A.
    const std::vector<std::uint32_t> offsets{0, 3, 4, 6};
    const std::vector<std::uint32_t> members{4, 0, 2, 9, 1, 0}; // 9: out of range, ignored
    BrowseOrder o;
    o.group = BrowseGroup::MyGroups;
    LibraryBrowser b;
    b.browse(s, o, kToday, {}, {offsets, members});
    CHECK(b.order() == std::vector<std::uint32_t>{0, 2, 4, 0, 1, 3});
    REQUIRE(b.sections().size() == 3);
    CHECK(b.sections()[0].key == 0);
    CHECK(b.sections()[1].key == 2);
    CHECK(b.sections()[2].key == kNoRank); // D is in no group
    CHECK(b.sections()[2].begin == 5);
    // The sort applies inside every group; the filter too.
    o.reversed = true;
    o.filter = BrowseFilter::Favourites;
    b.browse(s, o, kToday, {}, {offsets, members});
    for (const std::uint32_t k : b.order()) {
        CHECK(s[k].favourite);
    }
}
