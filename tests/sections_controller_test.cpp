// SPDX-License-Identifier: GPL-3.0-or-later
// The sections controller without SDL: edits with undo, looping a section, the practice list
// running on the song's loop, auditioning, the saved session and the hand-path hint.
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

TEST_CASE("a marker at the playhead makes two sections, saved in the library") {
    Rig r;
    r.songs.seek(6.0, r.now);
    r.step(10);
    CHECK(r.view().available);
    CHECK(r.view().sections.empty());
    r.sections.add_marker_at_playhead(r.now);
    r.step(10);
    REQUIRE(r.view().sections.size() == 2);
    CHECK(r.view().markers.size() == 1);
    CHECK(r.view().markers[0] == doctest::Approx(6.0));
    CHECK(r.view().sections[1].label == "Bars 4\xE2\x80\x93" "8");
    REQUIRE(r.library.find(0x77) != nullptr);
    CHECK(r.library.find(0x77)->sections.size() == 2);
    CHECK(r.view().can_undo);
    CHECK_FALSE(r.sections.take_message().empty());
}

TEST_CASE("undo steps back through edits, up to twenty") {
    Rig r(32);
    for (int i = 1; i <= 25; ++i) {
        CHECK(r.sections.split_at(2.0 * i, r.now));
    }
    r.step(10);
    CHECK(r.view().sections.size() == 26);
    CHECK(r.view().undo_depth == 20);
    for (int i = 0; i < 20; ++i) {
        CHECK(r.sections.undo(r.now));
    }
    r.step(10);
    CHECK(r.view().sections.size() == 6);
    CHECK_FALSE(r.sections.undo(r.now));
    CHECK_FALSE(r.view().can_undo);
}

TEST_CASE("an edit that fails changes nothing and says why") {
    Rig r;
    CHECK(r.sections.split_at(6.0, r.now));
    CHECK_FALSE(r.sections.split_at(6.0, r.now)); // already a cut there
    CHECK_FALSE(r.sections.take_message().empty());
    CHECK_FALSE(r.sections.split_at(6.02, r.now)); // too short a section
    r.step(10);
    CHECK(r.view().sections.size() == 2);
    CHECK(r.view().undo_depth == 1);
}

TEST_CASE("renaming, merging and deleting go through the library") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.sections.split_at(8.0, r.now);
    r.step(10);
    REQUIRE(r.view().sections.size() == 3);
    const SectionId a = r.view().sections[0].id;
    const SectionId b = r.view().sections[1].id;
    CHECK(r.sections.rename(a, "  Intro "));
    r.step(10);
    CHECK(r.view().sections[0].label == "Intro");
    CHECK(r.sections.merge_with_next(a, r.now));
    r.step(10);
    REQUIRE(r.view().sections.size() == 2);
    CHECK(r.view().sections[0].id == a);
    CHECK(r.view().sections[0].label == "Intro");
    CHECK(r.view().sections[0].end_s == doctest::Approx(8.0));
    CHECK(r.sections.erase_section(b, r.now) == false); // gone with the merge
    CHECK(r.sections.undo(r.now));
    r.step(10);
    CHECK(r.view().sections.size() == 3);
    // The last section has nothing to merge with.
    CHECK_FALSE(r.sections.merge_with_next(r.view().sections.back().id, r.now));
}

TEST_CASE("the edit serial moves with every edit and every undo, and with nothing else") {
    Rig r;
    const std::uint32_t start = r.sections.edit_serial();
    // Read at once, not with the frame's view: a toast keeps it right after the edit.
    CHECK(r.sections.split_at(4.0, r.now));
    const std::uint32_t split = r.sections.edit_serial();
    CHECK(split != start);
    CHECK_FALSE(r.sections.split_at(4.0, r.now)); // refused: nothing changed
    CHECK(r.sections.edit_serial() == split);
    r.step(10);
    CHECK(r.sections.edit_serial() == split); // a frame, a seek or a loop are not edits
    r.songs.seek(2.0, r.now);
    r.sections.select_section(r.view().sections[0].id, r.now);
    r.step(10);
    CHECK(r.sections.edit_serial() == split);
    CHECK(r.sections.merge_with_next(r.view().sections[0].id, r.now));
    const std::uint32_t joined = r.sections.edit_serial();
    CHECK(joined != split);
    CHECK(r.sections.undo(r.now));
    const std::uint32_t undone = r.sections.edit_serial();
    CHECK(undone != joined);
    CHECK(undone != split); // never back to an earlier number
    // A song loaded is a new history: an Undo offered for the old song is gone.
    r.songs.load(song_of(4, 0x99), r.now);
    r.step(10);
    CHECK(r.sections.edit_serial() != undone);
}

TEST_CASE("moving a marker keeps the sections and is one undo step") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.step(10);
    CHECK(r.sections.move_marker(0, 6.0, r.now));
    r.step(10);
    CHECK(r.view().markers[0] == doctest::Approx(6.0));
    CHECK_FALSE(r.sections.move_marker(0, 0.01, r.now));
    CHECK(r.sections.undo(r.now));
    r.step(10);
    CHECK(r.view().markers[0] == doctest::Approx(4.0));
}

TEST_CASE("selecting a section loops it, selecting it again ends the loop") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.step(10);
    const SectionId second = r.view().sections[1].id;
    r.sections.select_section(second, r.now);
    r.step(10);
    CHECK(r.songs.loop_enabled());
    CHECK(r.songs.loop_start_s() == doctest::Approx(4.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(r.songs.duration_s()));
    CHECK(r.songs.position_s() == doctest::Approx(4.0));
    CHECK(r.view().looping == second);
    r.sections.select_section(second, r.now);
    r.step(10);
    CHECK_FALSE(r.songs.loop_enabled());
    CHECK(r.view().looping == kNoSection);
}

TEST_CASE("an A-B loop that is not a section can be saved as one") {
    Rig r;
    r.songs.set_loop(4.0, 8.0, r.now);
    r.step(10);
    CHECK(r.view().loop_can_be_saved);
    r.sections.save_loop_as_section(r.now);
    r.step(10);
    CHECK(r.view().sections.size() == 3);
    CHECK_FALSE(r.view().loop_can_be_saved);
    CHECK(r.view().looping != kNoSection);
}

TEST_CASE("stepping moves to the next and previous section, wrapping") {
    Rig r;
    r.sections.split_at(4.0, r.now);
    r.sections.split_at(8.0, r.now);
    r.step(10);
    r.sections.step(1, r.now);
    r.step(10);
    CHECK(r.view().looping == r.view().sections[0].id); // from the playhead at 0: its section
    r.sections.step(1, r.now);
    r.step(10);
    CHECK(r.view().looping == r.view().sections[1].id);
    r.sections.step(-1, r.now);
    r.sections.step(-1, r.now);
    r.step(10);
    CHECK(r.view().looping == r.view().sections[2].id);
}

TEST_CASE("suggestions show as cuts, and accepting one makes it a section boundary") {
    Rig r(16);
    r.sections.set_suggestions(true);
    r.step(10);
    REQUIRE(r.view().suggestions_on);
    REQUIRE(r.view().cuts.size() == 3);
    CHECK(r.view().cuts[0].time_s == doctest::Approx(8.0));
    CHECK(r.sections.accept_cut(0, r.now));
    r.step(10);
    CHECK(r.view().markers.size() == 1);
    CHECK(r.view().cuts.size() == 2);
    CHECK(r.sections.accept_all_cuts(false, r.now));
    r.step(10);
    CHECK(r.view().sections.size() == 4);
    CHECK(r.view().cuts.empty());
    r.sections.set_suggestions(false);
    r.step(10);
    CHECK_FALSE(r.view().suggestions_on);
    // One undo takes back all the cuts accepted together.
    CHECK(r.sections.undo(r.now));
    r.step(10);
    CHECK(r.view().sections.size() == 2);
}

TEST_CASE("an audition sounds the song's notes even when the song is muted, and stops by itself") {
    Rig r;
    REQUIRE(r.songs.song_sound() == SongSound::Mute);
    r.songs.seek(1.0, r.now);
    r.step(10);
    r.sections.audition(2.0, 4.0, r.now);
    CHECK(r.sections.auditioning());
    r.run(1000);
    CHECK(r.note_ons > 0);
    CHECK(r.sections.auditioning());
    // The practice position and loop are untouched.
    CHECK(r.songs.position_s() == doctest::Approx(1.0));
    CHECK_FALSE(r.songs.loop_enabled());
    r.run(1500);
    CHECK_FALSE(r.sections.auditioning());
    r.run(500);
    CHECK(r.sounding.none());
    CHECK_FALSE(r.songs.needs_silence());
    // The run was not graded.
    CHECK(r.songs.take_finished_runs().empty());
}

TEST_CASE("a repeating audition starts again at its start when a pass ends with the loop on") {
    Rig r;
    REQUIRE(r.songs.set_loop(1.0, 3.0, r.now));
    REQUIRE(r.songs.loop_enabled());
    r.sections.audition_repeating(2.0, 4.0, r.now);
    r.run(1000);
    CHECK(r.sections.auditioning());
    CHECK(r.sections.audition_position_s() > 2.5);
    // The pass (2 s) ended and the next one began at 2.0.
    r.run(1500);
    CHECK(r.sections.auditioning());
    CHECK(r.sections.audition_position_s() >= 2.0);
    CHECK(r.sections.audition_position_s() < 3.0);
    r.run(4000);
    CHECK(r.sections.auditioning());
    // The practice position and loop are untouched.
    CHECK(r.songs.loop_enabled());
}

TEST_CASE("a repeating audition stops after its pass with the loop off, or when it is turned off") {
    Rig off;
    off.sections.audition_repeating(2.0, 4.0, off.now);
    off.run(1000);
    CHECK(off.sections.auditioning());
    off.run(1500);
    CHECK_FALSE(off.sections.auditioning());
    off.run(500);
    CHECK(off.sounding.none());

    Rig mid;
    REQUIRE(mid.songs.set_loop(1.0, 3.0, mid.now));
    mid.sections.audition_repeating(2.0, 4.0, mid.now);
    mid.run(1000);
    mid.songs.clear_loop(mid.now);
    mid.run(500);
    CHECK(mid.sections.auditioning()); // the pass in progress finishes
    mid.run(1000);
    CHECK_FALSE(mid.sections.auditioning());
}

TEST_CASE("stopping a repeating audition restarts nothing") {
    Rig r;
    REQUIRE(r.songs.set_loop(1.0, 3.0, r.now));
    r.sections.audition_repeating(2.0, 4.0, r.now);
    r.run(500);
    r.sections.stop_audition(r.now);
    r.run(4000);
    CHECK_FALSE(r.sections.auditioning());
    CHECK(r.sounding.none());
    // A plain audition does not repeat either.
    r.sections.audition(2.0, 4.0, r.now);
    r.run(2500);
    CHECK_FALSE(r.sections.auditioning());
}

TEST_CASE("an audition can be stopped, replaced and ends on a new song") {
    Rig r;
    r.sections.audition(0.0, 6.0, r.now);
    r.run(300);
    r.sections.audition(8.0, 10.0, r.now);
    r.run(100);
    CHECK(r.sections.audition_position_s() >= 8.0);
    r.sections.stop_audition(r.now);
    r.run(100);
    CHECK_FALSE(r.sections.auditioning());
    CHECK(r.sounding.none());
    r.sections.audition(0.0, 6.0, r.now);
    r.run(100);
    r.songs.load(song_of(4, 0x99), r.now);
    r.run(100);
    CHECK_FALSE(r.sections.auditioning());
    CHECK(r.sounding.none());
}

TEST_CASE("the audition takes the song it is asked about, whichever songs were loaded before") {
    Rig r;
    r.sections.audition(0.0, 2.0, r.now); // the first song's audition builds nothing at install
    r.run(100);
    CHECK(r.sounding.test(72));
    r.sections.stop_audition(r.now);
    r.run(100);
    // Another song, every note a step up: its audition plays its own notes.
    LoadedSong other = song_of(4, 0x99);
    for (Note& n : other.song.notes) {
        n.pitch = static_cast<std::uint8_t>(n.pitch + 1);
    }
    r.songs.load(std::move(other), r.now);
    r.run(100);
    r.sections.audition(0.0, 2.0, r.now);
    r.run(100);
    CHECK(r.sounding.test(73));
    CHECK_FALSE(r.sounding.test(72));
    r.sections.stop_audition(r.now);
    // After the song is closed and another opened, the audition follows that one.
    r.songs.unload(r.now);
    r.songs.load(song_of(4, 0x55), r.now);
    r.run(100);
    r.sections.audition(0.0, 2.0, r.now);
    r.run(100);
    CHECK(r.sounding.test(72));
    CHECK_FALSE(r.sounding.test(73));
    r.sections.stop_audition(r.now);
    r.run(100);
    CHECK_FALSE(r.songs.needs_silence());
}
