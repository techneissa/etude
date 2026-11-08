// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/section_editing.hpp"

#include "section_fixtures.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace etude;
using namespace etude::testing;

namespace {

SongSections marked(const Song& song, std::initializer_list<double> times) {
    SongSections s;
    s.song_hash = 9;
    for (double t : times) {
        REQUIRE(s.add_marker(song, t).ok);
    }
    return s;
}

} // namespace

TEST_CASE("undo brings back the sections as they were before the last edit") {
    const Song song = bars_song(16);
    SectionHistory history;
    SongSections s = marked(song, {8.0});
    CHECK_FALSE(history.can_undo());
    history.push(s);
    REQUIRE(s.add_marker(song, 16.0).ok);
    history.push(s);
    REQUIRE(s.rename(s.sections[0].id, "Intro").ok);
    CHECK(history.depth() == 2);
    const auto back = history.undo(s);
    REQUIRE(back.has_value());
    CHECK(back->sections.size() == 3);
    CHECK(back->sections[0].name != "Intro");
    const auto first = history.undo(*back);
    REQUIRE(first.has_value());
    CHECK(first->sections.size() == 2);
    CHECK_FALSE(history.can_undo());
    CHECK_FALSE(history.undo(*first).has_value());
}

TEST_CASE("undo keeps only the last twenty edits") {
    const Song song = bars_song(32);
    SectionHistory history;
    SongSections s;
    s.song_hash = 9;
    for (int i = 0; i < 25; ++i) {
        history.push(s);
        REQUIRE(s.add_marker(song, 2.0 * (i + 1)).ok);
    }
    CHECK(history.depth() == kMaxSectionUndo);
    CHECK(kMaxSectionUndo == 20);
    // Twenty undos walk back; the oldest five states are gone, so the song is not empty again.
    SongSections now = s;
    int steps = 0;
    while (auto back = history.undo(now)) {
        now = *back;
        ++steps;
    }
    CHECK(steps == 20);
    CHECK(now.sections.size() == 6);  // 25 markers made 26 sections; 20 undone leaves 6
}

TEST_CASE("pushing an unchanged state twice records it once") {
    const Song song = bars_song(16);
    SectionHistory history;
    const SongSections s = marked(song, {8.0});
    history.push(s);
    history.push(s);
    CHECK(history.depth() == 1);
}

TEST_CASE("undo never lowers next_id, so an id is not reused") {
    const Song song = bars_song(16);
    SectionHistory history;
    SongSections s = marked(song, {8.0});
    history.push(s);
    REQUIRE(s.add_marker(song, 16.0).ok);  // makes id 3
    const SectionId made = s.sections.back().id;
    const auto back = history.undo(s);
    REQUIRE(back.has_value());
    CHECK(back->find(made) == nullptr);
    SongSections again = *back;
    REQUIRE(again.add_marker(song, 16.0).ok);
    CHECK(again.sections.back().id != made);
}

TEST_CASE("undo restores the practice list too") {
    const Song song = bars_song(16);
    SectionHistory history;
    SongSections s = marked(song, {8.0});
    PlanItem item;
    item.section = s.sections[0].id;
    REQUIRE(s.plan_append(item).ok);
    history.push(s);
    REQUIRE(s.erase(song, s.sections[0].id).ok);
    CHECK(s.plan.empty());
    const auto back = history.undo(s);
    REQUIRE(back.has_value());
    CHECK(back->plan.size() == 1);
}

TEST_CASE("proposed cuts are the suggested boundaries the song does not have yet") {
    const Song song = bars_song(16);
    const SectionSuggestions sug = suggest_sections(song, {}, 9);
    REQUIRE(sug.sections.size() == 4);  // phrases of four bars
    SongSections none;
    none.song_hash = 9;
    auto cuts = proposed_cuts(sug, none);
    REQUIRE(cuts.size() == 3);
    CHECK(cuts[0].time_s == doctest::Approx(8.0));
    CHECK(cuts[0].measure == 5);
    CHECK(cuts[0].kind == SuggestionKind::Phrase);
    // One of them already exists.
    SongSections some = marked(song, {16.0});
    cuts = proposed_cuts(sug, some);
    REQUIRE(cuts.size() == 2);
    CHECK(cuts[0].time_s == doctest::Approx(8.0));
    CHECK(cuts[1].time_s == doctest::Approx(24.0));
    // Nothing left to propose once all are there.
    SongSections all = marked(song, {8.0, 16.0, 24.0});
    CHECK(proposed_cuts(sug, all).empty());
}

TEST_CASE("the edges of a trouble spot are trouble cuts, and can be listed alone") {
    SectionSuggestions sug;
    const auto add = [&](SuggestionKind kind, int first, int last, double a, double b) {
        SuggestedSection s;
        s.kind = kind;
        s.first_measure = first;
        s.last_measure = last;
        s.start_s = a;
        s.end_s = b;
        sug.sections.push_back(s);
    };
    add(SuggestionKind::Phrase, 1, 4, 0.0, 8.0);
    add(SuggestionKind::Trouble, 5, 6, 8.0, 12.0);
    add(SuggestionKind::Phrase, 7, 10, 12.0, 20.0);
    add(SuggestionKind::Phrase, 11, 14, 20.0, 28.0);
    SongSections none;
    const auto all = proposed_cuts(sug, none);
    REQUIRE(all.size() == 3);
    CHECK(all[0].kind == SuggestionKind::Trouble);
    CHECK(all[1].kind == SuggestionKind::Trouble);
    CHECK(all[2].kind == SuggestionKind::Phrase);
    const auto trouble = proposed_cuts(sug, none, true);
    REQUIRE(trouble.size() == 2);
    CHECK(trouble[0].time_s == 8.0);
    CHECK(trouble[1].time_s == 12.0);
}

TEST_CASE("accepting every proposed cut gives what apply_suggestions gives") {
    const Song song = bars_song(24);
    for (bool trouble_only : {false, true}) {
        const SectionSuggestions sug = suggest_sections(song, {}, 9);
        SongSections by_cuts;
        by_cuts.song_hash = 9;
        for (const ProposedCut& c : proposed_cuts(sug, by_cuts, trouble_only)) {
            REQUIRE(by_cuts.add_marker(song, c.time_s, false).ok);
        }
        SongSections by_apply;
        by_apply.song_hash = 9;
        apply_suggestions(by_apply, song, sug, trouble_only);
        CHECK(by_cuts.markers() == by_apply.markers());
    }
}

TEST_CASE("auditions: two seconds from the cursor, slid in when near an end") {
    const Song song = bars_song(16);  // 32 s
    AuditionRange r = audition_from(song, 10.0);
    CHECK(r.start_s == doctest::Approx(10.0));
    CHECK(r.end_s == doctest::Approx(12.0));
    r = audition_from(song, 31.0);
    CHECK(r.end_s == doctest::Approx(32.0 + 0.0));
    CHECK(r.end_s - r.start_s == doctest::Approx(kAuditionSeconds));
    r = audition_from(song, -3.0);
    CHECK(r.start_s == 0.0);
    CHECK(r.end_s == doctest::Approx(2.0));
    CHECK(audition_from(song, std::nan("")).valid() == false);
}

TEST_CASE("play across the cut: two seconds either side of it") {
    const Song song = bars_song(16);
    AuditionRange r = audition_across(song, 16.0);
    CHECK(r.start_s == doctest::Approx(14.0));
    CHECK(r.end_s == doctest::Approx(18.0));
    r = audition_across(song, 1.0);
    CHECK(r.start_s == 0.0);
    CHECK(r.end_s == doctest::Approx(4.0));
    r = audition_across(song, 32.0);
    CHECK(r.end_s == doctest::Approx(32.0));
    CHECK(r.start_s == doctest::Approx(28.0));
}

TEST_CASE("a section auditions once from its start to its end") {
    const Song song = bars_song(16);
    const SongSections s = marked(song, {8.0});
    const AuditionRange r = audition_section(s.sections[1]);
    CHECK(r.start_s == doctest::Approx(8.0));
    CHECK(r.end_s == doctest::Approx(32.0));
    CHECK(r.valid());
}

TEST_CASE("an audition on a song shorter than two seconds plays all of it") {
    Song song = bars_song(1, 240);  // one bar of 4/4 at 240 BPM: 1 s
    const AuditionRange r = audition_from(song, 0.3);
    CHECK(r.start_s == doctest::Approx(0.0));
    CHECK(r.end_s == doctest::Approx(song.duration_s));
}

TEST_CASE("clear forgets every undo step") {
    const Song song = bars_song(16);
    SectionHistory history;
    SongSections s = marked(song, {8.0});
    history.push(s);
    REQUIRE(s.add_marker(song, 4.0).ok);
    history.push(s);
    REQUIRE(history.can_undo());
    CHECK(history.depth() == 2);
    history.clear();
    CHECK_FALSE(history.can_undo());
    CHECK(history.depth() == 0);
    CHECK_FALSE(history.undo(s).has_value());
    // It is usable again afterwards.
    history.push(s);
    CHECK(history.depth() == 1);
}

TEST_CASE("a tap plays from the point to the cut of the section it is in") {
    const Song song = bars_song(16);  // 32 s
    const SongSections s = marked(song, {6.0, 12.0});
    // Mid-section: to the next cut, not two seconds.
    AuditionRange r = audition_tap(song, s, 8.0);
    CHECK(r.start_s == doctest::Approx(8.0));
    CHECK(r.end_s == doctest::Approx(12.0));
    // The last section runs to the song's end.
    r = audition_tap(song, s, 20.0);
    CHECK(r.start_s == doctest::Approx(20.0));
    CHECK(r.end_s == doctest::Approx(32.0));
    // A point exactly on a cut belongs to the section that starts there.
    r = audition_tap(song, s, 12.0);
    CHECK(r.start_s == doctest::Approx(12.0));
    CHECK(r.end_s == doctest::Approx(32.0));
    r = audition_tap(song, s, 6.0);
    CHECK(r.start_s == doctest::Approx(6.0));
    CHECK(r.end_s == doctest::Approx(12.0));
}

TEST_CASE("a tap in a song with no cuts plays to the song's end") {
    const Song song = bars_song(16);
    const AuditionRange r = audition_tap(song, SongSections{}, 10.0);
    CHECK(r.start_s == doctest::Approx(10.0));
    CHECK(r.end_s == doctest::Approx(32.0));
}

TEST_CASE("a tap on a section's name plays the section from its start to its cut") {
    const Song song = bars_song(16);
    const SongSections s = marked(song, {6.0, 12.0});
    const AuditionRange r = audition_section(s.sections[1]);
    CHECK(r.start_s == doctest::Approx(6.0));
    CHECK(r.end_s == doctest::Approx(12.0));
}

TEST_CASE("a tap at the song's last instant, or out of range, still plays something") {
    const Song song = bars_song(16);
    const SongSections s = marked(song, {6.0});
    AuditionRange r = audition_tap(song, s, 32.0);  // nothing left after it: the short one
    CHECK(r.valid());
    CHECK(r.end_s == doctest::Approx(32.0));
    CHECK(r.end_s - r.start_s == doctest::Approx(kAuditionSeconds));
    r = audition_tap(song, s, -4.0);  // before the song: from its start
    CHECK(r.start_s == 0.0);
    CHECK(r.end_s == doctest::Approx(6.0));
    CHECK(audition_tap(song, s, std::nan("")).valid() == false);
}

TEST_CASE("a tap a hair below a cut plays the section after it") {
    const Song song = bars_song(16);
    const SongSections s = marked(song, {6.0, 12.0});
    AuditionRange r = audition_tap(song, s, 6.0 - 1e-9);
    CHECK(r.start_s == doctest::Approx(6.0));
    CHECK(r.end_s == doctest::Approx(12.0));
    r = audition_tap(song, s, 12.0 - 1e-9);
    CHECK(r.start_s == doctest::Approx(12.0));
    CHECK(r.end_s == doctest::Approx(32.0));
    // Well below the cut it still plays to the cut.
    r = audition_tap(song, s, 5.5);
    CHECK(r.end_s == doctest::Approx(6.0));
}

TEST_CASE("a tap past the song's end plays its last two seconds") {
    const Song song = bars_song(16);
    const SongSections s = marked(song, {6.0});
    const AuditionRange r = audition_tap(song, s, 40.0);
    CHECK(r.start_s == doctest::Approx(30.0));
    CHECK(r.end_s == doctest::Approx(32.0));
}

TEST_CASE("a tap or a double tap while the music plays stops it; a double tap otherwise zooms") {
    CHECK(tap_play(true, false) == TapPlay::Stop);
    CHECK(tap_play(true, true) == TapPlay::Stop);
    CHECK(tap_play(false, false) == TapPlay::Play);
    CHECK(tap_play(false, true) == TapPlay::Leave);
}

TEST_CASE("Play with nothing selected covers ten seconds from the cursor") {
    const Song song = bars_song(16);  // 32 s
    const AuditionRange r = audition_idle(song, 8.0);
    CHECK(r.start_s == doctest::Approx(8.0));
    CHECK(r.end_s == doctest::Approx(8.0 + kIdlePlaySeconds));
    CHECK(kIdlePlaySeconds == doctest::Approx(10.0));
    // The song's cuts do not shorten it: the range takes no sections.
    CHECK(audition_idle(song, 8.0).end_s == doctest::Approx(18.0));
}

TEST_CASE("Play with nothing selected is cut at the song's end") {
    const Song song = bars_song(16);
    AuditionRange r = audition_idle(song, 25.0);
    CHECK(r.start_s == doctest::Approx(25.0));
    CHECK(r.end_s == doctest::Approx(32.0));
    // Exactly ten seconds left, and a cursor before the song or past it.
    CHECK(audition_idle(song, 22.0).end_s == doctest::Approx(32.0));
    r = audition_idle(song, -3.0);
    CHECK(r.start_s == 0.0);
    CHECK(r.end_s == doctest::Approx(10.0));
    // Under half a second left is nothing to play (a repeat would restart every frame).
    CHECK_FALSE(audition_idle(song, 31.999).valid());
    CHECK_FALSE(audition_idle(song, 32.0 - kIdleMinSeconds + 0.01).valid());
    CHECK(audition_idle(song, 32.0 - kIdleMinSeconds).valid());
    // Nothing left after the last instant, and no number: nothing to play.
    CHECK_FALSE(audition_idle(song, 32.0).valid());
    CHECK_FALSE(audition_idle(song, 50.0).valid());
    CHECK_FALSE(audition_idle(song, std::nan("")).valid());
}

TEST_CASE("a repeating audition starts again when a pass ends with the loop on") {
    CHECK(audition_repeat(true, false, true) == RepeatStep::Continue);
    CHECK(audition_repeat(true, true, true) == RepeatStep::Again);
}

TEST_CASE("turning the loop off lets the pass finish and stop") {
    // Mid-pass nothing changes, whatever the loop does.
    CHECK(audition_repeat(true, false, false) == RepeatStep::Continue);
    // Once the pass ends there is no new one.
    CHECK(audition_repeat(true, true, false) == RepeatStep::Done);
}

TEST_CASE("a stopped audition never repeats") {
    // Play, a tap or closing the editor ends the repeating at once, loop on or off, mid-pass or not.
    CHECK(audition_repeat(false, false, true) == RepeatStep::Done);
    CHECK(audition_repeat(false, true, true) == RepeatStep::Done);
    CHECK(audition_repeat(false, false, false) == RepeatStep::Done);
}
