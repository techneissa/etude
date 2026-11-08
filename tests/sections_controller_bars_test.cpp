// SPDX-License-Identifier: GPL-3.0-or-later
// sections_controller: bars (split from sections_controller_test.cpp).
#include "sections_controller.hpp"
#include "song_transport.hpp"
#include "etude/song_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <vector>

#include "sections_controller_test_support.hpp"

using namespace etude;
using namespace etude::app;
using namespace sections_controller_test_support;

// ---- The results card's bars --------------------------------------------------------------------

namespace {

BarPractice chosen_bars(Rig& r, std::initializer_list<int> bars, BarPracticeKind kind) {
    const std::vector<int> chosen(bars);
    const Song& song = r.songs.song().song;
    return plan_bar_practice(chosen, song.measures, song.duration_s, kind);
}

} // namespace

TEST_CASE("practising bars one by one makes a section of each, the list of them, and runs it") {
    Rig r;
    REQUIRE(r.sections.practise_bars(chosen_bars(r, {5, 2, 3}, BarPracticeKind::OneByOne), r.now));
    r.step(10);
    const ui::SectionsView& v = r.view();
    // Bars 2, 3 and 5 are sections now, in the list in song order, each until a pass is clean.
    REQUIRE(v.plan.size() == 3);
    const double starts[] = {2.0, 4.0, 8.0};
    for (std::size_t i = 0; i < 3; ++i) {
        const Section* s = v.data.find(v.plan[i].section);
        REQUIRE(s != nullptr);
        CHECK(s->start_s == doctest::Approx(starts[i]));
        CHECK(s->end_s == doctest::Approx(starts[i] + 2.0));
        CHECK(v.plan[i].item.repeats == 1);
        CHECK(v.plan[i].item.clean_passes_only);
    }
    CHECK(v.plan_running);
    CHECK(v.plan_item == 0);
    CHECK(r.songs.loop_enabled());
    CHECK(r.songs.loop_start_s() == doctest::Approx(2.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(4.0));
    CHECK(r.songs.playing());
}

TEST_CASE("practising bars is one undo step that gives the list and the cuts back") {
    Rig r;
    r.sections.plan_add_all(); // nothing to add: no sections yet
    const SongSections* none = r.library.find(0x77);
    const std::size_t before = none != nullptr ? none->sections.size() : 0;
    REQUIRE(r.sections.practise_bars(chosen_bars(r, {2, 5}, BarPracticeKind::OneByOne), r.now));
    r.step(10);
    CHECK(r.view().plan.size() == 2);
    CHECK(r.view().sections.size() > before);
    r.sections.plan_stop(r.now);
    REQUIRE(r.sections.undo(r.now));
    r.step(10);
    CHECK(r.view().plan.empty());
    CHECK(r.view().sections.size() == before);
}

TEST_CASE("practising bars replaces the list and reuses a section that is already that bar") {
    Rig r;
    r.sections.split_at(2.0, r.now);
    r.sections.split_at(4.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    const std::size_t sections_before = r.view().sections.size();
    SectionId bar2 = kNoSection;
    for (const ui::SectionRow& s : r.view().sections) {
        if (std::abs(s.start_s - 2.0) < 1e-6 && std::abs(s.end_s - 4.0) < 1e-6) {
            bar2 = s.id;
        }
    }
    REQUIRE(bar2 != kNoSection);
    REQUIRE(r.view().plan.size() == sections_before);
    REQUIRE(r.sections.practise_bars(chosen_bars(r, {2}, BarPracticeKind::OneByOne), r.now));
    r.step(10);
    REQUIRE(r.view().plan.size() == 1); // the old list is gone
    CHECK(r.view().plan[0].section == bar2);
    CHECK(r.view().sections.size() == sections_before); // no new cut
}

TEST_CASE("practising neighbouring bars together is one loop, kept as a one-item list") {
    Rig r;
    REQUIRE(r.sections.practise_bars(chosen_bars(r, {4, 3}, BarPracticeKind::Together), r.now));
    r.step(10);
    const ui::SectionsView& v = r.view();
    CHECK_FALSE(v.plan_running); // a plain loop, not the list
    CHECK(r.songs.loop_enabled());
    CHECK(r.songs.loop_start_s() == doctest::Approx(4.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(8.0));
    REQUIRE(v.plan.size() == 1);
    const Section* s = v.data.find(v.plan[0].section);
    REQUIRE(s != nullptr);
    CHECK(s->start_s == doctest::Approx(4.0));
    CHECK(s->end_s == doctest::Approx(8.0));
    CHECK(v.looping == s->id); // the band shows the section as the one looping
    CHECK(r.songs.playing());
}

TEST_CASE("practising bars together that are not neighbours runs a list that plays each once") {
    Rig r;
    REQUIRE(r.sections.practise_bars(chosen_bars(r, {2, 3, 6}, BarPracticeKind::Together), r.now));
    r.step(10);
    const ui::SectionsView& v = r.view();
    REQUIRE(v.plan.size() == 2); // bars 2-3, then bar 6
    CHECK(v.plan_running);
    for (const ui::PlanRow& row : v.plan) {
        CHECK(row.item.repeats == 1);
        CHECK_FALSE(row.item.clean_passes_only);
    }
    CHECK(r.songs.loop_start_s() == doctest::Approx(2.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(6.0));
    std::vector<int> items;
    for (int i = 0; i < 4000 && r.view().plan_running; ++i) {
        r.step(10);
        if (items.empty() || items.back() != r.view().plan_item) {
            items.push_back(r.view().plan_item);
        }
    }
    CHECK(items == std::vector<int>{0, 1, -1}); // each once, then the list is done
    CHECK_FALSE(r.view().plan_running);
}

TEST_CASE(
    "a stretch that crosses a cut of the song's own is played as the pieces between its cuts") {
    Rig r;
    r.sections.split_at(6.0, r.now); // inside bar 4 (6 to 8 is bar 4; the cut is at its start)
    r.sections.split_at(5.0, r.now); // and one inside bar 3 (4 to 6)
    r.step(10);
    REQUIRE(r.sections.practise_bars(chosen_bars(r, {3, 4}, BarPracticeKind::Together), r.now));
    r.step(10);
    const ui::SectionsView& v = r.view();
    // The loop is still the whole stretch, 4 to 8; the list holds its pieces: 4-5, 5-6, 6-8.
    CHECK(r.songs.loop_start_s() == doctest::Approx(4.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(8.0));
    REQUIRE(v.plan.size() == 3);
    const double ends[] = {5.0, 6.0, 8.0};
    for (std::size_t i = 0; i < 3; ++i) {
        const Section* s = v.data.find(v.plan[i].section);
        REQUIRE(s != nullptr);
        CHECK(s->end_s == doctest::Approx(ends[i]));
    }
}

TEST_CASE("practising no bar, or with no song, says so and changes nothing") {
    Rig r;
    CHECK_FALSE(r.sections.practise_bars(BarPractice{}, r.now));
    CHECK_FALSE(r.sections.take_message().empty());
    CHECK(r.view().plan.empty());
    SongController songs;
    SectionLibrary library;
    SectionsController sections(songs, library);
    BarPractice p;
    p.count = 1;
    p.loops[0] = {2.0, 4.0, 2, 2};
    CHECK_FALSE(sections.practise_bars(p, 0));
}

TEST_CASE("a running practice list stops, and says so, when its loop goes away") {
    // The section band's scrub and its seek off the sections go through SongPort::seek, which ends
    // the loop when the time is outside it; the loop toggle and the \ key clear it. The list has
    // nothing left to advance it then: it stops instead of claiming to run.
    for (int how = 0; how < 3; ++how) {
        CAPTURE(how);
        Rig r;
        SongTransport transport(r.songs);
        r.sections.split_at(4.0, r.now);
        r.sections.split_at(8.0, r.now);
        r.step(10);
        r.sections.plan_add_all();
        r.songs.set_speed(80, r.now);
        r.step(10);
        r.sections.plan_start(r.now);
        r.step(10);
        REQUIRE(r.view().plan_running);
        REQUIRE(r.songs.loop_enabled());
        (void)r.sections.take_message();
        // A seek inside the loop keeps it, and the list runs on.
        transport.seek(1.0, r.now);
        r.step(10);
        CHECK(r.view().plan_running);
        if (how == 0) {
            transport.seek(12.0, r.now); // a scrub past the section
        } else if (how == 1) {
            transport.clear_loop(r.now); // the band's loop toggle
        } else {
            r.songs.clear_loop(r.now); // the \ key
        }
        r.step(10);
        CHECK_FALSE(r.view().plan_running);
        CHECK(r.view().plan_item == -1);
        CHECK(r.sections.take_message() == "Practice list stopped: its loop ended");
        CHECK(r.songs.speed_percent() == 80); // the speed it started with
        CHECK_FALSE(r.songs.loop_enabled());
        // It stays stopped.
        r.step(10);
        CHECK_FALSE(r.view().plan_running);
    }
}

TEST_CASE("a practice list that finishes is not also reported as stopped") {
    Rig r(4);
    r.sections.split_at(4.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    r.sections.plan_start(r.now);
    r.step(10);
    REQUIRE(r.view().plan_running);
    (void)r.sections.take_message();
    std::vector<std::string> said;
    for (int i = 0; i < 4000 && r.view().plan_running; ++i) {
        r.step(10);
        if (std::string m = r.sections.take_message(); !m.empty()) {
            said.push_back(m);
        }
    }
    CHECK_FALSE(r.view().plan_running);
    CHECK(said == std::vector<std::string>{"Practice list finished"});
}

TEST_CASE("a refused loop or a piece that cannot be made leaves the practice list as it was") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    const std::vector<ui::PlanRow> before = r.view().plan;
    REQUIRE_FALSE(before.empty());
    const std::uint32_t revision = r.view().revision;
    // A loop under the shortest the transport takes: refused before anything is edited.
    BarPractice p;
    p.count = 1;
    p.loops[0] = {2.0, 2.0 + kMinLoopSeconds / 2.0, 2, 2};
    CHECK_FALSE(r.sections.practise_bars(p, r.now));
    CHECK_FALSE(r.sections.take_message().empty());
    r.step(10);
    CHECK(r.view().plan.size() == before.size());
    CHECK(r.view().revision == revision);
    CHECK(r.view().can_undo); // the earlier edits are still there; this attempt added none
}

TEST_CASE("practising bars says why when the song has no room for more sections") {
    Rig r(100);
    for (int i = 1; i < static_cast<int>(kMaxSectionsPerSong); ++i) {
        REQUIRE(r.sections.split_at(2.0 * i, r.now));
    }
    r.step(10);
    REQUIRE(r.view().sections.size() == kMaxSectionsPerSong);
    r.sections.plan_add(r.view().sections[0].id);
    r.step(10);
    REQUIRE(r.view().plan.size() == 1);
    const std::size_t undo_depth = r.view().undo_depth;
    // Bar 90 (178 to 180 s) lies inside the last section, so it needs two more.
    const Song& song = r.songs.song().song;
    const std::vector<int> chosen{90};
    const BarPractice p =
        plan_bar_practice(chosen, song.measures, song.duration_s, BarPracticeKind::OneByOne);
    REQUIRE(p.count == 1);
    CHECK_FALSE(r.sections.practise_bars(p, r.now));
    CHECK(r.sections.take_message() == "That is as many sections as a song can have");
    r.step(10);
    CHECK(r.view().plan.size() == 1);
    CHECK(r.view().undo_depth == undo_depth);
}
