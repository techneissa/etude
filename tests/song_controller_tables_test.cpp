// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: tables made ahead for a song, and the notes in view known at load.
#include "alloc_count.hpp"
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE("a song loaded with tables made ahead is used, and behaves like a plain load") {
    std::int64_t now = kSec;
    SongController plain;
    plain.set_mode(PracticeMode::Wait, now);
    plain.load(two_hand_song(), now);

    SongController ahead;
    ahead.set_mode(PracticeMode::Wait, now);
    LoadedSong song = two_hand_song();
    PreparedSong prepared = SongController::prepare(song.song, ahead.load_plan());
    REQUIRE(prepared.valid);
    CHECK(prepared.flow.note_count() == song.song.notes.size());
    ahead.load(std::move(song), now, std::move(prepared));
    // Taken over: the tables were moved into the evaluators, not built again.
    CHECK(prepared.flow.note_count() == 0);
    CHECK(prepared.wait.note_count() == 0);
    CHECK_FALSE(prepared.playback.valid()); // the playback's tables too

    CHECK(ahead.has_song());
    CHECK(ahead.hands_available() == plain.hands_available());
    CHECK(ahead.practice_hands() == plain.practice_hands());
    CHECK(ahead.bar_count() == plain.bar_count());
    CHECK(ahead.densest_window() == plain.densest_window());
    CHECK(ahead.densest_window() > 0);
    std::vector<VisibleNote> a;
    std::vector<VisibleNote> b;
    ahead.visible_notes(4.0, a);
    plain.visible_notes(4.0, b);
    CHECK_FALSE(a.empty());
    CHECK(a.size() == b.size());
    const auto got = first_chord(ahead, now);
    std::int64_t later = kSec;
    CHECK(got == first_chord(plain, later));
    CHECK(got.size() == 2);
    std::vector<SongSoundEvent> out;
    ahead.shutdown(now, out);
    plain.shutdown(later, out);
}

TEST_CASE("tables made for other hands or another key are left unused and the load still works") {
    std::int64_t now = kSec;
    SongController c;
    c.set_mode(PracticeMode::Wait, now);
    LoadedSong song = two_hand_song();
    SongLoadPlan plan = c.load_plan();
    plan.hands = HandFilter::Left; // the player asked for both
    PreparedSong prepared = SongController::prepare(song.song, plan);
    c.load(std::move(song), now, std::move(prepared));
    CHECK(prepared.flow.note_count() > 0); // not taken
    // The playback's tables do not depend on the hands or the key: they were built from this song,
    // so they are taken all the same.
    CHECK_FALSE(prepared.playback.valid());
    CHECK(c.practice_hands() == HandFilter::Both);
    CHECK(first_chord(c, now).size() == 2); // both hands' keys, not the left hand's only

    SongController d;
    d.set_mode(PracticeMode::Wait, now);
    LoadedSong other = two_hand_song();
    SongLoadPlan shifted = d.load_plan();
    shifted.transposition = Transposition(3, KeyRange{36, 96});
    PreparedSong keyed = SongController::prepare(other.song, shifted);
    d.load(std::move(other), now, std::move(keyed));
    CHECK(keyed.wait.note_count() > 0);
    std::int64_t t = kSec;
    const auto chord = first_chord(d, t);
    REQUIRE(chord.size() == 2);
    CHECK(chord[0].first == 48); // no shift: the key the song names

    // Tables of a song with other notes are not taken, even when asked for the same way.
    SongController e;
    PreparedSong foreign = SongController::prepare(two_hand_song(4).song, e.load_plan());
    e.load(two_hand_song(), now, std::move(foreign));
    CHECK(foreign.flow.note_count() > 0);
    CHECK(e.has_song());
    // The same number of notes with other content (every pitch a step up) is not taken either, and
    // neither are its note window and hands.
    SongController f;
    f.set_mode(PracticeMode::Wait, now);
    LoadedSong stepped = two_hand_song();
    for (Note& n : stepped.song.notes) {
        n.pitch = static_cast<std::uint8_t>(n.pitch + 1);
    }
    PreparedSong lookalike = SongController::prepare(stepped.song, f.load_plan());
    f.load(two_hand_song(), now, std::move(lookalike));
    CHECK(lookalike.flow.note_count() > 0);
    CHECK(lookalike.wait.note_count() > 0);
    CHECK(lookalike.playback.valid()); // built from other notes: not taken
    std::int64_t u = kSec;
    const auto first = first_chord(f, u);
    REQUIRE(first.size() == 2);
    CHECK(first[0].first == 48); // the song's own keys, not the lookalike's 49
    CHECK(first[1].first == 72);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    d.shutdown(t, out);
    e.shutdown(now, out);
    f.shutdown(u, out);
}

TEST_CASE("tables are compared by the fingerprint the loader carried, and a lookalike's are refused") {
    std::int64_t now = kSec;
    SongController c;
    c.set_mode(PracticeMode::Wait, now);
    LoadedSong song = two_hand_song();
    song.fingerprint = song_fingerprint(song.song); // as the loader thread sets it
    PreparedSong own = SongController::prepare(song.song, c.load_plan(), song.fingerprint);
    CHECK(own.fingerprint == song.fingerprint);
    c.load(std::move(song), now, std::move(own));
    CHECK(own.flow.note_count() == 0); // taken: the two numbers matched

    // Tables of the same notes a step up, the loaded song carrying its own fingerprint: refused.
    SongController d;
    d.set_mode(PracticeMode::Wait, now);
    LoadedSong stepped = two_hand_song();
    for (Note& n : stepped.song.notes) {
        n.pitch = static_cast<std::uint8_t>(n.pitch + 1);
    }
    PreparedSong lookalike = SongController::prepare(stepped.song, d.load_plan());
    LoadedSong real = two_hand_song();
    real.fingerprint = song_fingerprint(real.song);
    CHECK(real.fingerprint != lookalike.fingerprint);
    d.load(std::move(real), now, std::move(lookalike));
    CHECK(lookalike.flow.note_count() > 0); // not taken
    CHECK(d.song().fingerprint == song_fingerprint(d.song().song));
    std::int64_t t = kSec;
    const auto chord = first_chord(d, t);
    REQUIRE(chord.size() == 2);
    CHECK(chord[0].first == 48); // the song's own keys, not the lookalike's 49

    // A song built without a loader carries none: load() takes it, and the tables still match.
    SongController e;
    LoadedSong plain = two_hand_song();
    CHECK(plain.fingerprint == 0);
    PreparedSong made = SongController::prepare(plain.song, e.load_plan());
    e.load(std::move(plain), now, std::move(made));
    CHECK(made.flow.note_count() == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    d.shutdown(t, out);
    e.shutdown(now, out);
}

TEST_CASE("a restored session's hands and key are in the plan, so its tables are taken") {
    std::int64_t now = kSec;
    SongController c;
    c.set_keyboard(KeyRange{36, 96}, now);
    SessionState state;
    state.practice_hands = HandFilter::Right;
    state.transpose_semitones = 2;
    state.mode = PracticeMode::Wait;
    const SongLoadPlan plan = c.load_plan_for(state);
    CHECK(plan.hands == HandFilter::Right);
    CHECK(plan.transposition == Transposition(2, KeyRange{36, 96}));
    // Out-of-range semitones are clamped as the session would be.
    state.transpose_semitones = 99;
    CHECK(c.load_plan_for(state).transposition.semitones() == kMaxTransposeSemitones);

    state.transpose_semitones = 2;
    LoadedSong song = two_hand_song();
    PreparedSong prepared = SongController::prepare(song.song, plan);
    c.apply_session(state, now); // the hands and the key, before the song is in
    c.load(std::move(song), now, std::move(prepared));
    CHECK(prepared.flow.note_count() == 0); // taken
    CHECK(c.practice_hands() == HandFilter::Right);
    c.apply_session(state, now);
    CHECK(c.practice_hands() == HandFilter::Right);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("the most notes in view is known at load, and Wait Mode's key list allocates nothing") {
    std::int64_t now = kSec;
    SongController c;
    CHECK(c.densest_window() == 0);
    c.set_mode(PracticeMode::Wait, now);
    c.load(two_hand_song(), now);
    CHECK(c.densest_window() == densest_window(c.song().song, c.load_plan().ahead_s));
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 500 * kMs);
    REQUIRE(c.waiting());
    etude::test::start_counting_allocations();
    for (int i = 0; i < 50; ++i) {
        CHECK(c.waiting_keys().size() == 2);
        CHECK(c.expected_keys().size() == 2);
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    c.unload(now);
    CHECK(c.densest_window() == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

// ---- The octave-shift offer (core shift_offer.hpp, raised from SongController::update)
