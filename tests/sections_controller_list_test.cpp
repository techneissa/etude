// SPDX-License-Identifier: GPL-3.0-or-later
// sections_controller: list (split from sections_controller_test.cpp).
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

TEST_CASE("the practice list runs the sections in turn and finishes") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.sections.split_at(8.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    REQUIRE(r.view().plan.size() == 3);
    PlanItem item = r.view().plan[0].item;
    item.repeats = 2;
    r.sections.plan_set(0, item);
    item.repeats = 1;
    r.sections.plan_set(1, item);
    r.sections.plan_set(2, item);
    r.step(10);
    CHECK(r.view().plan[0].detail == "\xC3\x97" "2");
    r.sections.plan_start(r.now);
    r.step(10);
    CHECK(r.view().plan_running);
    CHECK(r.view().plan_item == 0);
    CHECK(r.songs.loop_enabled());
    CHECK(r.songs.loop_end_s() == doctest::Approx(4.0));
    CHECK(r.view().plan_status.starts_with("Section 1/3"));
    CHECK(r.view().plan_status.ends_with("pass 1/2"));
    // Play: nothing is played, so every pass is unclean but still counts (clean-only is off).
    std::vector<int> items;
    for (int i = 0; i < 4000 && r.view().plan_running; ++i) {
        r.step(10);
        if (items.empty() || items.back() != r.view().plan_item) {
            items.push_back(r.view().plan_item);
        }
    }
    CHECK(items == std::vector<int>{0, 1, 2, -1});
    CHECK_FALSE(r.view().plan_running);
    CHECK_FALSE(r.songs.loop_enabled());
    CHECK_FALSE(r.songs.playing());
}

TEST_CASE("the practice list can be skipped and stopped, and restores the speed") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.songs.set_speed(80, r.now);
    PlanItem item = r.view().plan.empty() ? PlanItem{} : r.view().plan[0].item;
    r.step(10);
    item = r.view().plan[0].item;
    item.speed_up.enabled = true;
    item.speed_up.start_percent = 60;
    r.sections.plan_set(0, item);
    r.sections.plan_start(r.now);
    r.step(10);
    CHECK(r.songs.speed_percent() == 60);
    r.sections.plan_skip(1, r.now);
    r.step(10);
    CHECK(r.view().plan_item == 1);
    CHECK(r.songs.speed_percent() == 80); // the second item has no speed-up
    r.sections.plan_skip(-1, r.now);
    r.step(10);
    CHECK(r.view().plan_item == 0);
    r.sections.plan_stop(r.now);
    r.step(10);
    CHECK_FALSE(r.view().plan_running);
    CHECK(r.songs.speed_percent() == 80);
    CHECK_FALSE(r.songs.loop_enabled());
}

TEST_CASE("editing sections while the list runs keeps it valid, and a merge keeps its place") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.sections.split_at(8.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    r.sections.plan_start(r.now);
    r.sections.plan_skip(1, r.now);
    r.step(10);
    REQUIRE(r.view().plan_item == 1);
    // Merge the running section into the one before it: the list follows to the survivor.
    const SectionId first = r.view().sections[0].id;
    CHECK(r.sections.merge_with_next(first, r.now));
    r.step(10);
    CHECK(r.view().plan_running);
    CHECK(r.songs.loop_start_s() == doctest::Approx(0.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(8.0));
    // Moving a marker retimes the loop at once.
    CHECK(r.sections.move_marker(0, 6.0, r.now));
    r.step(10);
    CHECK(r.songs.loop_end_s() == doctest::Approx(6.0));
}

TEST_CASE("the practice list status counts only the items left after a delete") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.sections.split_at(8.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    REQUIRE(r.view().plan.size() == 3);
    const SectionId second = r.view().sections[1].id;
    r.sections.plan_start(r.now);
    r.step(10);
    CHECK(r.view().plan_status.starts_with("Section 1/3"));
    REQUIRE(r.sections.erase_section(second, r.now));
    r.step(10);
    CHECK(r.view().plan_status.starts_with("Section 1/2"));
    r.sections.plan_skip(1, r.now); // the running list passes over the deleted item
    r.step(10);
    REQUIRE(r.view().plan_item == 2);
    CHECK(r.view().plan_status.starts_with("Section 2/2")); // was "Section 3/3" over two rows
}

TEST_CASE("deleting a section in a running list drops its item and the list skips it") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.sections.split_at(8.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    REQUIRE(r.view().plan.size() == 3);
    const SectionId first = r.view().sections[0].id;
    const SectionId second = r.view().sections[1].id;
    r.sections.plan_start(r.now);
    r.step(10);
    REQUIRE(r.view().plan_item == 0);
    // The middle section goes: the first takes its time and the saved list loses its item.
    REQUIRE(r.sections.erase_section(second, r.now));
    r.step(10);
    CHECK(r.view().plan_running);
    CHECK(r.view().plan.size() == 2);
    REQUIRE(r.view().sections.size() == 2);
    CHECK(r.view().sections[0].id == first);
    CHECK(r.songs.loop_end_s() == doctest::Approx(8.0)); // the running section grew at once
    // The running list goes on from the first item to the last without ever stopping on the
    // deleted one (its place in the running list, 1, is passed over), then finishes.
    std::vector<int> items{r.view().plan_item};
    for (int i = 0; i < 20000 && r.view().plan_running; ++i) {
        r.step(10);
        if (items.back() != r.view().plan_item) {
            items.push_back(r.view().plan_item);
        }
    }
    CHECK(items == std::vector<int>{0, 2, -1});
    CHECK_FALSE(r.view().plan_running);
    CHECK_FALSE(r.songs.loop_enabled());
}

TEST_CASE("the session keeps the looped section and the running list item") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.sections.split_at(8.0, r.now);
    r.step(10);
    r.sections.plan_add_all();
    r.step(10);
    r.sections.select_section(r.view().sections[1].id, r.now);
    r.step(10);
    SessionState saved;
    r.sections.save_into(saved);
    CHECK(saved.section_id == static_cast<int>(r.view().sections[1].id));
    CHECK(saved.plan_item == 0);
    r.sections.plan_start(r.now);
    r.sections.plan_skip(1, r.now);
    r.step(10);
    r.sections.save_into(saved);
    CHECK(saved.plan_item == 2);

    // A new run of the app: the library came back from the file, the song is loaded again.
    SongController songs2;
    SectionLibrary library2 = r.library;
    SectionsController sections2(songs2, library2);
    songs2.load(song_of(8), r.now);
    sections2.restore(saved, r.now);
    sections2.update(r.now);
    CHECK(sections2.view().plan_running);
    CHECK(sections2.view().plan_item == 1);
    CHECK(songs2.loop_enabled());
    songs2.shutdown(r.now, r.sent);
    r.songs.shutdown(r.now, r.sent);
}

TEST_CASE("a saved selection or list item whose section is gone is dropped") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.step(10);
    SessionState saved;
    saved.section_id = 999;  // no such section
    saved.plan_item = 3;     // no such item
    SongController songs2;
    SectionLibrary library2 = r.library;
    SectionsController sections2(songs2, library2);
    songs2.load(song_of(8), r.now);
    sections2.restore(saved, r.now);
    sections2.update(r.now);
    CHECK_FALSE(songs2.loop_enabled());
    CHECK_FALSE(sections2.view().plan_running);
    SessionState again;
    sections2.save_into(again);
    CHECK(again.section_id == 0);
    CHECK(again.plan_item == 0);
    songs2.shutdown(r.now, r.sent);
}

TEST_CASE("sections damaged in the file are repaired when their song loads") {
    SongController songs;
    SectionLibrary library;
    SongSections bad;
    bad.song_hash = 0x77;
    Section a;
    a.id = 1;
    a.start_s = 0.0;
    a.end_s = 100.0;  // past the song's end
    Section b;
    b.id = 2;
    b.start_s = 100.0;
    b.end_s = 200.0;
    bad.sections = {a, b};
    bad.next_id = 3;
    library.put(bad);
    SectionsController sections(songs, library);
    songs.load(song_of(4), 1000 * kMs);
    sections.update(1000 * kMs);
    const SongSections* fixed = library.find(0x77);
    if (fixed != nullptr) {
        CHECK(fixed->valid(songs.song().song));
    }
    CHECK(sections.view().available);
    std::vector<SongSoundEvent> out;
    songs.shutdown(1000 * kMs, out);
}

TEST_CASE("no song, no sections") {
    SongController songs;
    SectionLibrary library;
    SectionsController sections(songs, library);
    sections.update(1000 * kMs);
    CHECK_FALSE(sections.view().available);
    CHECK_FALSE(sections.split_at(1.0, 1000 * kMs));
    sections.add_marker_at_playhead(1000 * kMs);
    sections.plan_start(1000 * kMs);
    CHECK(library.songs().empty());
}

TEST_CASE("the hand-path hint says when the history is loading or busy, and is back to normal after") {
    Rig r;
    r.step(10);
    const std::string normal = r.view().hand_hint;
    REQUIRE_FALSE(normal.empty());

    r.sections.set_history_note(SectionsController::HistoryNote::Loading);
    r.step(10);
    CHECK(r.view().hand_hint == "Loading practice history\xE2\x80\xA6");
    CHECK_FALSE(r.view().hand_can_apply); // no advice from a history that is not all there

    // A busy database is not "loading": the history may be complete, the new runs are what wait.
    r.sections.set_history_note(SectionsController::HistoryNote::Busy);
    r.step(10);
    CHECK(r.view().hand_hint == "Practice history is busy; new runs are kept and saved later.");
    CHECK_FALSE(r.view().hand_can_apply);

    // A file the program may not write says that, not "busy": waiting does not fix it.
    r.sections.set_history_note(SectionsController::HistoryNote::Unwritable);
    r.step(10);
    CHECK(r.view().hand_hint ==
          "Practice history can't be saved: Etude may not change its file or folder.");
    CHECK_FALSE(r.view().hand_can_apply);

    r.sections.set_history_note(SectionsController::HistoryNote::None);
    r.step(10);
    CHECK(r.view().hand_hint == normal);
    CHECK(r.view().hand_can_apply);
}

TEST_CASE("the hand-path hint starts on the right hand and one tap applies it") {
    Rig r;
    r.sections.add_marker_at_playhead(r.now); // at 0: refused; nothing changes
    r.step(10);
    CHECK_FALSE(r.view().hand_hint.empty());
    CHECK(r.view().hand_scope == "Whole song");
    REQUIRE(r.view().hand_can_apply);
    r.songs.set_practice_hands(HandFilter::Both, r.now);
    r.sections.apply_hand_path(r.now);
    r.step(10);
    CHECK(r.songs.practice_hands() == HandFilter::Right);
    CHECK(r.songs.speed_percent() == 60);
    // With a section looped the hint is about that section.
    r.sections.split_at(4.0, r.now);
    r.step(10);
    r.sections.select_section(r.view().sections[1].id, r.now);
    r.step(10);
    CHECK(r.view().hand_scope == "Bars 3\xE2\x80\x93" "8");
}
