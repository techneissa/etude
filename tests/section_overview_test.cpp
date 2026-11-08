// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/section_overview.hpp"

#include "section_fixtures.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace etude;
using namespace etude::testing;

namespace {

// 16 bars, 32 s; markers at 8 s and 16 s: three sections of 4, 4 and 8 bars.
struct Fixture {
    Song song = bars_song(16);
    SongSections sections;
    OverviewTrack track{100.0f, 740.0f, 32.0};  // 20 points per second

    Fixture() {
        sections.song_hash = 7;
        REQUIRE(sections.add_marker(song, 8.0).ok);
        REQUIRE(sections.add_marker(song, 16.0).ok);
    }
};

} // namespace

TEST_CASE("overview track maps time and x both ways and clamps") {
    const OverviewTrack t{100.0f, 740.0f, 32.0};
    CHECK(t.x_at(0.0) == doctest::Approx(100.0f));
    CHECK(t.x_at(16.0) == doctest::Approx(420.0f));
    CHECK(t.x_at(40.0) == doctest::Approx(740.0f));
    CHECK(t.time_at(420.0f) == doctest::Approx(16.0));
    CHECK(t.time_at(0.0f) == 0.0);
    CHECK(t.time_at(900.0f) == doctest::Approx(32.0));
    CHECK(OverviewTrack{}.time_at(5.0f) == 0.0);
    CHECK(OverviewTrack{0.0f, 10.0f, 0.0}.x_at(3.0) == 0.0f);
}

TEST_CASE("a touch near a marker grabs the marker, nearest first") {
    Fixture f;
    // Markers at x = 100 + 8*20 = 260 and 420.
    OverviewHit h = overview_hit(f.sections, f.track, 262.0f, 22.0f);
    CHECK(h.kind == OverviewHit::Kind::Marker);
    CHECK(h.marker == 0);
    h = overview_hit(f.sections, f.track, 281.0f, 22.0f);
    CHECK(h.kind == OverviewHit::Kind::Marker);
    h = overview_hit(f.sections, f.track, 283.0f, 22.0f);
    CHECK(h.kind == OverviewHit::Kind::Segment);
    h = overview_hit(f.sections, f.track, 418.0f, 22.0f);
    CHECK(h.kind == OverviewHit::Kind::Marker);
    CHECK(h.marker == 1);
}

TEST_CASE("two markers inside one touch zone: the closer one wins") {
    Fixture f;
    // Zone of 200 points reaches both markers; the touch is nearer the second.
    OverviewHit h = overview_hit(f.sections, f.track, 380.0f, 200.0f);
    CHECK(h.kind == OverviewHit::Kind::Marker);
    CHECK(h.marker == 1);
    h = overview_hit(f.sections, f.track, 300.0f, 200.0f);
    CHECK(h.marker == 0);
}

TEST_CASE("a touch between markers hits the section under it") {
    Fixture f;
    OverviewHit h = overview_hit(f.sections, f.track, 180.0f, 10.0f);
    CHECK(h.kind == OverviewHit::Kind::Segment);
    CHECK(h.section == f.sections.sections[0].id);
    h = overview_hit(f.sections, f.track, 340.0f, 10.0f);
    CHECK(h.section == f.sections.sections[1].id);
    h = overview_hit(f.sections, f.track, 600.0f, 10.0f);
    CHECK(h.section == f.sections.sections[2].id);
    // Past either end of the track: the section at that end.
    CHECK(overview_hit(f.sections, f.track, 5.0f, 10.0f).section == f.sections.sections[0].id);
    CHECK(overview_hit(f.sections, f.track, 900.0f, 10.0f).section == f.sections.sections[2].id);
}

TEST_CASE("a song with no sections, or no track, hits nothing") {
    Fixture f;
    SongSections none;
    CHECK(overview_hit(none, f.track, 300.0f, 22.0f).kind == OverviewHit::Kind::None);
    CHECK(overview_hit(f.sections, OverviewTrack{}, 300.0f, 22.0f).kind ==
          OverviewHit::Kind::None);
}

TEST_CASE("a dragged marker snaps to bars and stays clear of its neighbours") {
    Fixture f;
    // Marker 0 sits at 8 s between [0,8) and [8,16). Bars are 2 s.
    CHECK(marker_drag_time(f.song, f.sections, 0, 10.9, true) == doctest::Approx(10.0));
    CHECK(marker_drag_time(f.song, f.sections, 0, 11.1, true) == doctest::Approx(12.0));
    CHECK(marker_drag_time(f.song, f.sections, 0, 10.9, false) == doctest::Approx(10.9));
    // Not past the next marker (16 s) nor before the song start, each section keeps its minimum.
    CHECK(marker_drag_time(f.song, f.sections, 0, 99.0, true) == doctest::Approx(14.0));
    CHECK(marker_drag_time(f.song, f.sections, 0, -5.0, true) == doctest::Approx(2.0));
    CHECK(marker_drag_time(f.song, f.sections, 0, 99.0, false) ==
          doctest::Approx(16.0 - kMinSectionSeconds).epsilon(1e-4));
}

TEST_CASE("marker_drag_time results are accepted by move_marker") {
    Fixture f;
    for (double raw : {-3.0, 0.2, 5.5, 9.4, 13.9, 15.99, 20.0, 31.0}) {
        for (bool snap : {true, false}) {
            SongSections s = f.sections;
            const double t = marker_drag_time(f.song, s, 0, raw, snap);
            const EditResult r = s.move_marker(f.song, 0, t, snap);
            INFO("raw " << raw << " snap " << snap << " -> " << t);
            CHECK(r.ok);
            CHECK(s.valid(f.song));
        }
    }
}

TEST_CASE("marker_drag_time falls back to the unsnapped time when no bar fits") {
    // Bars of 2 s, but the sections around the marker are only 1 s on each side: no bar start
    // lies inside the limits, so the held time is used.
    Song song = bars_song(16);
    SongSections s;
    s.song_hash = 7;
    REQUIRE(s.add_marker(song, 5.0, false).ok);
    REQUIRE(s.add_marker(song, 6.0, false).ok);  // sections [0,5) [5,6) [6,32)
    const double t = marker_drag_time(song, s, 0, 5.4, true);
    // Limits for marker 0 are (0 + min, 6 - min); bars 4 s and 6 s: 4 s fits.
    CHECK(t >= kMinSectionSeconds);
    CHECK(t <= 6.0 - kMinSectionSeconds);
    CHECK(s.move_marker(song, 0, t, false).ok);
    // Unknown marker: the raw time back.
    CHECK(marker_drag_time(song, s, 9, 3.3) == 3.3);
}

TEST_CASE("move_marker keeps ids and custom names and renames the default names") {
    Fixture f;
    const SectionId a = f.sections.sections[0].id;
    const SectionId b = f.sections.sections[1].id;
    REQUIRE(f.sections.rename(b, "Verse").ok);
    PlanItem item;
    item.section = b;
    REQUIRE(f.sections.plan_append(item).ok);
    const EditResult r = f.sections.move_marker(f.song, 0, 10.0);
    REQUIRE(r.ok);
    CHECK(r.id == b);
    CHECK(f.sections.sections[0].id == a);
    CHECK(f.sections.sections[0].end_s == doctest::Approx(10.0));
    CHECK(f.sections.sections[0].name == "Bars 1-5");
    CHECK(f.sections.sections[1].name == "Verse");
    CHECK(f.sections.sections[1].start_s == doctest::Approx(10.0));
    CHECK(f.sections.plan.size() == 1);
    CHECK(f.sections.valid(f.song));
}

TEST_CASE("move_marker refuses what would break a section") {
    Fixture f;
    const SongSections before = f.sections;
    CHECK(f.sections.move_marker(f.song, 5, 10.0).error == EditError::NotFound);
    CHECK(f.sections.move_marker(f.song, 0, std::nan("")).error == EditError::OutOfRange);
    CHECK(f.sections.move_marker(f.song, 0, 20.0).error == EditError::OutOfRange);
    CHECK(f.sections.move_marker(f.song, 0, 0.05, false).error == EditError::TooShort);
    CHECK(f.sections.move_marker(f.song, 1, 8.0 + 0.05, false).error == EditError::TooShort);
    CHECK(f.sections == before);
}

TEST_CASE("a section library compares by its songs") {
    Fixture f;
    SectionLibrary a;
    SectionLibrary b;
    CHECK(a == b);
    a.put(f.sections);
    CHECK_FALSE(a == b);
    b.put(f.sections);
    CHECK(a == b);
    SongSections changed = f.sections;
    REQUIRE(changed.rename(changed.sections[0].id, "Intro").ok);
    b.put(changed);
    CHECK_FALSE(a == b);
    // What a write would delete does not count.
    b.put(f.sections);
    b.erase(12345);
    CHECK(a == b);
}

TEST_CASE("section labels use an en dash for bars and keep custom names") {
    Fixture f;
    CHECK(section_label(f.sections.sections[1]) == "Bars 5\xE2\x80\x93" "8");
    CHECK(section_detail_text(f.sections.sections[1]) == "0:08\xE2\x80\x93" "0:16");
    REQUIRE(f.sections.rename(f.sections.sections[1].id, "Verse").ok);
    CHECK(section_label(f.sections.sections[1]) == "Verse");
    CHECK(section_detail_text(f.sections.sections[1]) ==
          "Bars 5\xE2\x80\x93" "8 \xC2\xB7 0:08\xE2\x80\x93" "0:16");
    // A one-bar section is "Bar N".
    Song song = bars_song(16);
    SongSections s;
    s.song_hash = 1;
    REQUIRE(s.add_marker(song, 2.0).ok);
    CHECK(section_label(s.sections[0]) == "Bar 1");
}

TEST_CASE("clock text is m:ss and tolerates junk") {
    CHECK(clock_text(0.0) == "0:00");
    CHECK(clock_text(59.9) == "0:59");
    CHECK(clock_text(61.0) == "1:01");
    CHECK(clock_text(-4.0) == "0:00");
    CHECK(clock_text(std::nan("")) == "0:00");
}

TEST_CASE("the practice list status reads Section n/m, bars, pass") {
    Fixture f;
    PlanStatus st;
    st.active = true;
    st.item_index = 1;
    st.item_count = 5;
    st.passes_done = 0;
    st.repeats = 3;
    CHECK(plan_status_text(st, &f.sections.sections[1]) ==
          "Section 2/5 \xC2\xB7 Bars 5\xE2\x80\x93" "8 \xC2\xB7 pass 1/3");
    st.passes_done = 2;
    CHECK(plan_status_text(st, &f.sections.sections[1]).ends_with("pass 3/3"));
    st.passes_done = 7; // never past the repeat count
    CHECK(plan_status_text(st, &f.sections.sections[1]).ends_with("pass 3/3"));
    st.repeats = 0;
    st.passes_done = 3;
    CHECK(plan_status_text(st, &f.sections.sections[1]).ends_with("pass 4"));
    CHECK(plan_status_text(st, nullptr) == "Section 2/5 \xC2\xB7 pass 4");
    st.active = false;
    CHECK(plan_status_text(st, &f.sections.sections[1]).empty());
}

TEST_CASE("a practice list item reads repeats, clean passes and speed-up") {
    PlanItem item;
    item.repeats = 3;
    CHECK(plan_item_text(item) == "\xC3\x97" "3");
    item.repeats = 0;
    item.clean_passes_only = true;
    CHECK(plan_item_text(item) == "\xC3\x97\xE2\x88\x9E \xC2\xB7 clean only");
    item.speed_up.enabled = true;
    item.speed_up.start_percent = 60;
    item.speed_up.max_percent = 100;
    item.speed_up.step_percent = 5;
    CHECK(plan_item_text(item) ==
          "\xC3\x97\xE2\x88\x9E \xC2\xB7 clean only \xC2\xB7 60\xE2\x80\x93" "100% +5");
}
