// SPDX-License-Identifier: GPL-3.0-or-later
// sections: binding (split from sections_test.cpp).
#include "etude/sections.hpp"
#include "section_fixtures.hpp"
#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "sections_test_support.hpp"

using namespace etude;
using namespace etude::testing;
using namespace sections_test_support;

// ---- Binding to a song

TEST_CASE("sections: fit_to_song closes gaps, trims the end and drops what lies past the song") {
    Song song = song16();
    SongSections s;
    s.song_hash = 7;
    Section a;
    a.id = 1;
    a.start_s = 2.0;
    a.end_s = 8.0;
    Section b;
    b.id = 2;
    b.start_s = 10.0;
    b.end_s = 20.0;
    Section c;
    c.id = 3;
    c.start_s = 40.0;
    c.end_s = 50.0;
    s.sections = {c, b, a};
    s.next_id = 4;
    s.fit_to_song(song);
    REQUIRE(s.sections.size() == 2);
    CHECK(s.sections[0].id == 1);
    CHECK(s.sections[0].start_s == 0.0);
    CHECK(s.sections[0].end_s == 10.0);
    CHECK(s.sections[1].id == 2);
    CHECK(s.sections[1].end_s == 32.0);
    CHECK(s.valid(song));
}

TEST_CASE("sections: fit_to_song drops overlapping and too-short sections") {
    Song song = song16();
    SongSections s;
    Section a;
    a.id = 1;
    a.start_s = 0.0;
    a.end_s = 10.0;
    Section overlap;
    overlap.id = 2;
    overlap.start_s = 5.0;
    overlap.end_s = 20.0;
    Section tiny;
    tiny.id = 3;
    tiny.start_s = 10.0;
    tiny.end_s = 10.01;
    Section d;
    d.id = 4;
    d.start_s = 10.0;
    d.end_s = 30.0;
    s.sections = {a, overlap, tiny, d};
    s.next_id = 5;
    s.fit_to_song(song);
    REQUIRE(s.sections.size() == 2);
    CHECK(s.sections[0].id == 1);
    CHECK(s.sections[1].id == 4);
    CHECK(s.sections[1].end_s == 32.0);
    CHECK(s.valid(song));
}

TEST_CASE("sections: fit_to_song on a song shorter than a section drops them all") {
    Song tiny;
    tiny.duration_s = 0.05;
    SongSections s;
    Section a;
    a.id = 1;
    a.start_s = 0.0;
    a.end_s = 0.1;
    s.sections = {a};
    s.next_id = 2;
    s.fit_to_song(tiny);
    CHECK(s.empty());
}

TEST_CASE("sections: fit_to_song keeps a custom name and cleans a bad one") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    s.sections[0].name = "Intro";
    s.sections[0].custom_name = true;
    s.sections[1].name = std::string("x\xFF") + std::string(100, 'y');
    s.sections[1].custom_name = true;
    s.fit_to_song(song);
    CHECK(s.sections[0].name == "Intro");
    CHECK(s.sections[1].name.size() == kMaxSectionNameBytes);
    CHECK(s.valid(song));
}

TEST_CASE("sections: valid() notices each broken invariant") {
    Song song = song16();
    const SongSections good = with_markers(song, {8.0, 16.0});
    CHECK(good.valid(song));

    SongSections gap = good;
    gap.sections[1].start_s = 9.0;
    CHECK_FALSE(gap.valid(song));

    SongSections late = good;
    late.sections.back().end_s = 31.0;
    CHECK_FALSE(late.valid(song));

    SongSections dup = good;
    dup.sections[1].id = dup.sections[0].id;
    CHECK_FALSE(dup.valid(song));

    SongSections zero_id = good;
    zero_id.sections[0].id = kNoSection;
    CHECK_FALSE(zero_id.valid(song));

    SongSections stale_next = good;
    stale_next.next_id = 1;
    CHECK_FALSE(stale_next.valid(song));

    SongSections bad_name = good;
    bad_name.sections[0].name = "x\xFF";
    CHECK_FALSE(bad_name.valid(song));

    SongSections wrong_measure = good;
    wrong_measure.sections[0].last_measure = 5;
    CHECK_FALSE(wrong_measure.valid(song));

    SongSections dangling = good;
    PlanItem item;
    item.section = 99;
    dangling.plan.push_back(item);
    CHECK_FALSE(dangling.valid(song));
}

// ---- The saved playlist

namespace {

PlanItem item(SectionId id, int repeats = 1) {
    PlanItem p;
    p.section = id;
    p.repeats = repeats;
    return p;
}

std::vector<SectionId> plan_ids(const SongSections& s) {
    std::vector<SectionId> out;
    for (const PlanItem& p : s.plan) {
        out.push_back(p.section);
    }
    return out;
}

} // namespace

TEST_CASE("sections: playlist items are played in the order they were added") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    SectionId c = s.sections[2].id;
    REQUIRE(s.plan_append(item(c, 3)).ok);
    REQUIRE(s.plan_append(item(a)).ok);
    REQUIRE(s.plan_append(item(b, 0)).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{c, a, b});
    CHECK(s.plan[0].repeats == 3);
    CHECK(s.plan[2].repeats == 0);
    CHECK(s.valid(song));
}

TEST_CASE("sections: reordering the playlist moves one item") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    SectionId c = s.sections[2].id;
    for (SectionId id : {a, b, c}) {
        REQUIRE(s.plan_append(item(id)).ok);
    }
    REQUIRE(s.plan_move(0, 2).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{b, c, a});
    REQUIRE(s.plan_move(2, 0).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{a, b, c});
    REQUIRE(s.plan_move(1, 1).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{a, b, c});
    CHECK(s.plan_move(0, 3).error == EditError::NotFound);
    CHECK(s.plan_move(5, 0).error == EditError::NotFound);
    CHECK(plan_ids(s) == std::vector<SectionId>{a, b, c});
}

TEST_CASE("sections: the playlist survives renames, since it names ids") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SectionId b = s.sections[1].id;
    REQUIRE(s.plan_append(item(b)).ok);
    REQUIRE(s.rename(b, "Chorus").ok);
    REQUIRE(s.add_marker(song, 8.0).ok);
    REQUIRE(s.add_marker(song, 24.0).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{b});
    CHECK(s.valid(song));
}

TEST_CASE("sections: a playlist item for an unknown section is refused") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    CHECK(s.plan_append(item(99)).error == EditError::NotFound);
    CHECK(s.plan.empty());
    REQUIRE(s.plan_append(item(s.sections[0].id)).ok);
    CHECK(s.plan_set(0, item(99)).error == EditError::NotFound);
    CHECK(s.plan_set(4, item(s.sections[0].id)).error == EditError::NotFound);
}

TEST_CASE("sections: playlist items are clamped to their ranges") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    PlanItem p = item(s.sections[0].id, 500);
    p.speed_up = AutoSpeedUp{true, 62, 3, 90};
    REQUIRE(s.plan_append(p).ok);
    CHECK(s.plan[0].repeats == kMaxPlanRepeats);
    CHECK(s.plan[0].speed_up.start_percent == 60);
    CHECK(s.plan[0].speed_up.step_percent == 5);
    CHECK(s.plan[0].speed_up.max_percent == 90);

    PlanItem q = item(s.sections[0].id, -4);
    q.speed_up = AutoSpeedUp{true, 100, 80, 40};
    REQUIRE(s.plan_set(0, q).ok);
    CHECK(s.plan[0].repeats == 0);
    CHECK(s.plan[0].speed_up.step_percent == 25);
    CHECK(s.plan[0].speed_up.max_percent == 100); // never below the start
}

TEST_CASE("sections: the playlist has a size limit") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    for (std::size_t i = 0; i < kMaxPlanItems; ++i) {
        REQUIRE(s.plan_append(item(s.sections[0].id)).ok);
    }
    CHECK(s.plan_append(item(s.sections[0].id)).error == EditError::TooMany);
    CHECK(s.plan.size() == kMaxPlanItems);
}

TEST_CASE("sections: deleting a section takes it out of the playlist") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    SectionId c = s.sections[2].id;
    for (SectionId id : {a, b, c, b}) {
        REQUIRE(s.plan_append(item(id)).ok);
    }
    REQUIRE(s.erase(song, b).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{a, c});
    CHECK(s.valid(song));
}

TEST_CASE("sections: merging moves playlist items to the surviving section") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0, 24.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    SectionId c = s.sections[2].id;
    SectionId d = s.sections[3].id;
    for (SectionId id : {a, b, c, d}) {
        REQUIRE(s.plan_append(item(id, 2)).ok);
    }
    std::array<SectionId, 2> pick{b, c};
    REQUIRE(s.merge(song, pick).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{a, b, d}); // b and c became one step
    CHECK(s.valid(song));
}

TEST_CASE("sections: removing a marker keeps the playlist valid") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    REQUIRE(s.plan_append(item(b)).ok);
    REQUIRE(s.plan_append(item(a)).ok);
    REQUIRE(s.remove_marker(song, 16.0).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{a});
    CHECK(s.valid(song));
}

// ---- The library

TEST_CASE("sections library: edit creates an entry, finds it again and ignores hash 0") {
    SectionLibrary lib;
    CHECK(lib.edit(0) == nullptr);
    CHECK(lib.find(5) == nullptr);
    SongSections* s = lib.edit(5);
    REQUIRE(s != nullptr);
    CHECK(s->song_hash == 5);
    CHECK(s->empty());
    Song song = song16();
    REQUIRE(s->add_marker(song, 16.0).ok);
    REQUIRE(lib.find(5) != nullptr);
    CHECK(lib.find(5)->sections.size() == 2);
    CHECK(lib.edit(5) == lib.edit(5));
    CHECK(lib.songs().size() == 1);
}

TEST_CASE("sections library: past the limit the song used longest ago is forgotten") {
    SectionLibrary lib;
    Song song = song16();
    for (std::uint64_t h = 1; h <= kMaxSongsWithSections; ++h) {
        REQUIRE(lib.edit(h)->add_marker(song, 16.0).ok);
    }
    REQUIRE(lib.songs().size() == kMaxSongsWithSections);
    lib.edit(1); // used again: hash 2 is now the oldest
    lib.edit(1000);
    CHECK(lib.songs().size() == kMaxSongsWithSections);
    CHECK(lib.find(1) != nullptr);
    CHECK(lib.find(2) == nullptr);
    CHECK(lib.find(1000) != nullptr);
    REQUIRE(lib.stale_names().size() == 1);
    CHECK(lib.stale_names()[0] == "sections.0000000000000002");
}

TEST_CASE("sections library: put replaces, and an entry with nothing in it is removed") {
    SectionLibrary lib;
    Song song = song16();
    SongSections s;
    s.song_hash = 9;
    REQUIRE(s.add_marker(song, 16.0).ok);
    lib.put(s);
    REQUIRE(lib.find(9) != nullptr);
    REQUIRE(s.add_marker(song, 8.0).ok);
    lib.put(s);
    CHECK(lib.songs().size() == 1);
    CHECK(lib.find(9)->sections.size() == 3);

    SongSections none;
    none.song_hash = 9;
    lib.put(none);
    CHECK(lib.find(9) == nullptr);
    CHECK(lib.stale_names() == std::vector<std::string>{"sections.0000000000000009"});

    SongSections anon;
    anon.sections = s.sections;
    lib.put(anon); // hash 0
    CHECK(lib.songs().empty());
}

TEST_CASE("sections library: erase forgets a song and marks its document section stale") {
    SectionLibrary lib;
    Song song = song16();
    REQUIRE(lib.edit(0xABCDEF)->add_marker(song, 16.0).ok);
    lib.erase(0xABCDEF);
    CHECK(lib.songs().empty());
    CHECK(lib.stale_names() == std::vector<std::string>{"sections.0000000000abcdef"});
    lib.erase(0xABCDEF); // not there: nothing more
    CHECK(lib.stale_names().size() == 1);
}

TEST_CASE("sections library: the use counter never wraps") {
    SectionLibrary lib;
    SongSections a;
    a.song_hash = 1;
    a.last_used = 4'294'967'295u;
    Song song = song16();
    REQUIRE(a.add_marker(song, 16.0).ok);
    SongSections b = a;
    b.song_hash = 2;
    b.last_used = 4'294'967'290u;
    lib.put(a);
    lib.put(b);
    SongSections* c = lib.edit(3);
    REQUIRE(c != nullptr);
    CHECK(c->last_used > lib.find(1)->last_used);
    CHECK(lib.find(1)->last_used > lib.find(2)->last_used);
}

// ---- Audit follow-ups (issue #32)

TEST_CASE("sections: a speed-up step near INT_MAX is clamped before it is rounded") {
    PlanItem p;
    p.speed_up = AutoSpeedUp{true, 60, std::numeric_limits<int>::max(), 100};
    CHECK(sanitized(p).speed_up.step_percent == 25);
    p.speed_up.step_percent = std::numeric_limits<int>::max() - 1;
    CHECK(sanitized(p).speed_up.step_percent == 25);
    p.speed_up.step_percent = std::numeric_limits<int>::min();
    CHECK(sanitized(p).speed_up.step_percent == 5);
    p.speed_up =
        AutoSpeedUp{true, std::numeric_limits<int>::max(), 5, std::numeric_limits<int>::max()};
    PlanItem q = sanitized(p);
    CHECK(q.speed_up.start_percent == 200);
    CHECK(q.speed_up.max_percent == 200);
    p.repeats = std::numeric_limits<int>::max();
    CHECK(sanitized(p).repeats == kMaxPlanRepeats);
}

TEST_CASE("sections: merging unequal playlist items keeps the larger repeat count") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    SectionId c = s.sections[2].id;
    PlanItem first = item(a, 3);
    first.speed_up = AutoSpeedUp{true, 50, 5, 90};
    PlanItem second = item(b, 1);
    second.clean_passes_only = true;
    second.speed_up = AutoSpeedUp{true, 70, 10, 120};
    REQUIRE(s.plan_append(first).ok);
    REQUIRE(s.plan_append(second).ok);
    REQUIRE(s.plan_append(item(c, 2)).ok);

    std::array<SectionId, 2> pick{a, b};
    std::vector<SectionId> absorbed;
    REQUIRE(s.merge(song, pick, &absorbed).ok);
    CHECK(absorbed == std::vector<SectionId>{b});
    REQUIRE(s.plan.size() == 2);
    CHECK(s.plan[0].section == a);
    CHECK(s.plan[0].repeats == 3);      // the larger of 3 and 1
    CHECK(s.plan[0].clean_passes_only); // the second one was
    PlanItem want = first; // everything else, the speed-up included, is the first item's
    want.repeats = 3;
    want.clean_passes_only = true;
    CHECK(s.plan[0] == want);
    CHECK(s.plan[1].section == c);
    CHECK(s.valid(song));
}

TEST_CASE("sections: merging takes the second item's repeats when they are more, 0 is the most") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    REQUIRE(s.plan_append(item(a, 2)).ok);
    REQUIRE(s.plan_append(item(b, 5)).ok);
    std::array<SectionId, 2> pick{a, b};
    REQUIRE(s.merge(song, pick).ok);
    REQUIRE(s.plan.size() == 1);
    CHECK(s.plan[0].repeats == 5);
    CHECK_FALSE(s.plan[0].clean_passes_only);

    SongSections t = with_markers(song, {8.0});
    REQUIRE(t.plan_append(item(t.sections[0].id, 4)).ok);
    REQUIRE(t.plan_append(item(t.sections[1].id, 0)).ok);
    REQUIRE(t.remove_marker(song, 8.0).ok);
    REQUIRE(t.plan.size() == 1);
    CHECK(t.plan[0].repeats == 0); // until skipped
}

TEST_CASE("sections: merging keeps playlist items that are not next to each other apart") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0});
    SectionId a = s.sections[0].id;
    SectionId b = s.sections[1].id;
    SectionId c = s.sections[2].id;
    REQUIRE(s.plan_append(item(a, 2)).ok);
    REQUIRE(s.plan_append(item(c, 1)).ok);
    REQUIRE(s.plan_append(item(b, 3)).ok);
    std::array<SectionId, 2> pick{a, b};
    REQUIRE(s.merge(song, pick).ok);
    CHECK(plan_ids(s) == std::vector<SectionId>{a, c, a});
    CHECK(s.plan[0].repeats == 2);
    CHECK(s.plan[2].repeats == 3);
}

TEST_CASE("sections: normalize drops non-finite times before it sorts") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    SongSections s;
    s.next_id = 1;
    auto section = [](SectionId id, double a, double b) {
        Section x;
        x.id = id;
        x.start_s = a;
        x.end_s = b;
        return x;
    };
    s.sections = {section(1, 20.0, 30.0), section(2, nan, 5.0),   section(3, 0.0, 10.0),
                  section(4, 10.0, nan),  section(5, 10.0, 20.0), section(6, 3.0, 4.0)};
    s.plan = {item(2), item(5), item(3)};
    s.normalize();
    REQUIRE(s.sections.size() == 3);
    CHECK(s.sections[0].id == 3);
    CHECK(s.sections[1].id == 5);
    CHECK(s.sections[2].id == 1);
    CHECK(s.next_id == 6);                              // id 6 was too short to keep
    CHECK(plan_ids(s) == std::vector<SectionId>{5, 3}); // the item on a dropped section goes
    CHECK(s.well_formed());
}

TEST_CASE("sections: normalize drops repeated ids, overlaps and short sections, and renames") {
    SongSections s;
    auto section = [](SectionId id, double a, double b, std::string name = {}) {
        Section x;
        x.id = id;
        x.start_s = a;
        x.end_s = b;
        x.custom_name = !name.empty();
        x.name = std::move(name);
        return x;
    };
    s.sections = {section(2, 5.0, 9.0, "  Verse\t1 "),
                  section(2, 9.0, 12.0),
                  section(0, 12.0, 20.0),
                  section(3, 8.0, 15.0),
                  section(4, 15.0, 15.0),
                  section(5, 15.0, 20.0)};
    s.next_id = 1;
    s.normalize();
    REQUIRE(s.sections.size() == 2);
    CHECK(s.sections[0].id == 2);
    CHECK(s.sections[0].name == "Verse 1");
    CHECK(s.sections[1].id == 5);
    CHECK(s.sections[1].name == "Section 2");
    CHECK(s.next_id == 6);
}

TEST_CASE("sections: plan_clear empties the playlist and leaves the sections") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0});
    REQUIRE(s.plan_append(item(s.sections[0].id)).ok);
    REQUIRE(s.plan_append(item(s.sections[1].id)).ok);
    s.plan_clear();
    CHECK(s.plan.empty());
    CHECK(s.sections.size() == 2);
    CHECK(s.valid(song));
    s.plan_clear(); // already empty
    CHECK(s.plan.empty());
}

TEST_CASE("sections library: equal when the songs match, whatever is stale") {
    Song song = song16();
    SectionLibrary a;
    SectionLibrary b;
    CHECK(a == b);
    REQUIRE(a.edit(5)->add_marker(song, 16.0).ok);
    CHECK_FALSE(a == b);
    REQUIRE(b.edit(5)->add_marker(song, 16.0).ok);
    CHECK(a == b);
    const SectionLibrary copy = a;
    CHECK(copy == a);
    // What the next write would delete is not part of the comparison.
    b.stale_names().push_back("sections.00000000000000ff");
    CHECK(a == b);
    // A different marker, or a different song, is a different library.
    REQUIRE(b.edit(5)->add_marker(song, 8.0).ok);
    CHECK_FALSE(a == b);
    SectionLibrary c;
    REQUIRE(c.edit(6)->add_marker(song, 16.0).ok);
    CHECK_FALSE(a == c);
}
