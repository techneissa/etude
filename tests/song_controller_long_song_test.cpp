// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: a long song's change of hands or key comes into force with its tables.
#include "practice_tables_job.hpp"
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE("a long song's change of hands comes into force with its tables, not before") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_mode(PracticeMode::Wait, now);
    c.load(two_hand_song(), now);
    CHECK(c.tables_settled());
    const int rebuilds = c.evaluator_rebuilds();

    c.set_practice_hands(HandFilter::Left, now);
    CHECK(c.practice_hands() == HandFilter::Left); // asked for: the toggle and the sound follow at once
    CHECK_FALSE(c.tables_settled());
    CHECK(c.evaluator_rebuilds() == rebuilds); // the evaluators still hold the old hands' tables
    settle_tables(c, now);
    CHECK(c.tables_settled());
    CHECK(c.evaluator_rebuilds() == rebuilds + 1);
    CHECK(wait_keys(c, now) == std::vector<int>{48}); // the left hand's note only

    // And back: the right hand's notes again, a chord of one here (C5), with the left silent.
    std::int64_t later = kSec;
    SongController d;
    make_long(d);
    d.set_mode(PracticeMode::Wait, later);
    d.load(two_hand_song(), later);
    d.set_practice_hands(HandFilter::Right, later);
    settle_tables(d, later);
    CHECK(wait_keys(d, later) == std::vector<int>{72});
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    d.shutdown(later, out);
}

TEST_CASE("a long song's change of key comes into force with its tables: the notes, the sound and "
          "the grading together") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_keyboard(KeyRange{36, 96}, now);
    c.set_mode(PracticeMode::Flow, now);
    c.load(two_hand_song(), now);
    CHECK(c.transposition().semitones() == 0);

    c.player_set_transpose(2, now);
    CHECK(c.transposition().semitones() == 0); // what the screen shows and the grading judges by
    CHECK_FALSE(c.tables_settled());
    settle_tables(c, now);
    CHECK(c.tables_settled());
    CHECK(c.transposition().semitones() == 2);

    // The song's C5 is now asked for on D5 (74), its C3 on D3 (50).
    c.play(now);
    const std::array<MidiEvent, 1> press{note_on(74, now)};
    FakePiano piano;
    run(c, piano, now, now + 16 * kMs, press);
    run(c, piano, now, now + 600 * kMs);
    const EvaluationSummary* summary = c.summary();
    REQUIRE(summary != nullptr);
    CHECK(summary->counts.perfect == 1);
    CHECK(summary->wrong == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a run begun while a long song's tables are built is a run of the new hands") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_mode(PracticeMode::Flow, now);
    c.load(two_hand_song(), now);
    // Hands asked for and Play pressed before the worker has answered: the song plays on the old
    // tables for a frame or two.
    c.set_practice_hands(HandFilter::Left, now);
    c.play(now);
    const std::int64_t start = now;
    FakePiano piano;
    run(c, piano, now, now + 16 * kMs);
    settle_tables(c, now);
    for (int i = 0; i < 4 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    REQUIRE(c.tables_settled());
    CHECK(c.state() == TransportState::Playing);
    // The left hand's C3 at 2.0 s, pressed on time; the song ends at 4 s.
    const std::array<MidiEvent, 1> press{note_on(48, start + 2 * kSec)};
    run(c, piano, now, start + 2 * kSec - 16 * kMs);
    run(c, piano, now, start + 2 * kSec + 16 * kMs, press);
    run(c, piano, now, start + 6 * kSec);
    REQUIRE(c.state() == TransportState::Ended);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1); // a run of the left hand, not lost with the old tables
    CHECK(runs[0].record.hands == HandFilter::Left);
    CHECK(runs[0].record.all.hit >= 1);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a long song's tables for a change that was overtaken are dropped") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_mode(PracticeMode::Wait, now);
    c.load(two_hand_song(), now);
    c.set_keyboard(KeyRange{36, 96}, now);
    settle_tables(c, now);
    // Asked for in a row before the worker can answer: only the last is in force at the end.
    c.set_practice_hands(HandFilter::Left, now);
    c.player_set_transpose(3, now);
    c.set_practice_hands(HandFilter::Right, now);
    c.player_set_transpose(-1, now);
    c.set_practice_hands(HandFilter::Left, now);
    c.player_set_transpose(5, now);
    settle_tables(c, now);
    for (int i = 0; i < 4 && !c.tables_settled(); ++i) { // an answer made for an older ask
        settle_tables(c, now);
    }
    CHECK(c.tables_settled());
    CHECK(c.practice_hands() == HandFilter::Left);
    CHECK(c.transposition().semitones() == 5);

    SongController plain; // the same end state, built on the frame thread
    plain.set_mode(PracticeMode::Wait, now);
    plain.load(two_hand_song(), now);
    plain.set_keyboard(KeyRange{36, 96}, now);
    plain.set_practice_hands(HandFilter::Left, now);
    plain.player_set_transpose(5, now);
    std::int64_t a = now;
    std::int64_t b = now;
    const std::vector<int> keys = wait_keys(c, a);
    CHECK(keys == wait_keys(plain, b));
    CHECK_FALSE(keys.empty());
    std::vector<SongSoundEvent> out;
    c.shutdown(a, out);
    plain.shutdown(b, out);
}

TEST_CASE("an answer that is in when the ask has changed is not used") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_keyboard(KeyRange{36, 96}, now);
    c.set_mode(PracticeMode::Wait, now);
    c.load(two_hand_song(), now);
    settle_tables(c, now);
    // The worker's answer for the left hand and a key up is ready, but no update has taken it.
    c.set_practice_hands(HandFilter::Left, now);
    c.player_set_transpose(2, now);
    c.wait_tables();
    // Held, so the worker cannot answer the hands change alone (the right hand at the old key)
    // between the two asks: that answer is rightly installed, and would put +2 in force.
    c.hold_tables(true);
    // The ask changes: that answer no longer fits what is asked for, and is dropped for a new one.
    c.set_practice_hands(HandFilter::Right, now);
    c.player_set_transpose(-2, now);
    CHECK_FALSE(c.tables_settled());
    CHECK(c.transposition().semitones() == 0); // neither key was ever in force
    c.hold_tables(false);
    settle_tables(c, now);
    for (int i = 0; i < 4 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    CHECK(c.tables_settled());
    CHECK(c.practice_hands() == HandFilter::Right);
    CHECK(c.transposition().semitones() == -2);
    CHECK(wait_keys(c, now) == std::vector<int>{70}); // C5 two semitones down
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a long song and a short one end in the same state after the same random asks") {
    for (unsigned seed = 1; seed <= 16; ++seed) {
        std::mt19937 rng(seed);
        std::int64_t now = kSec;
        SongController async;
        make_long(async);
        SongController plain;
        for (SongController* c : {&async, &plain}) {
            c->set_keyboard(KeyRange{36, 96}, now);
            c->set_mode(PracticeMode::Wait, now);
            c->load(two_hand_song(4), now);
        }
        for (int step = 0; step < 12; ++step) {
            const HandFilter hands = static_cast<HandFilter>(rng() % 3);
            const int semitones = static_cast<int>(rng() % 9) - 4;
            const bool hands_first = rng() % 2 == 0;
            for (SongController* c : {&async, &plain}) {
                if (hands_first) {
                    c->set_practice_hands(hands, now);
                    c->player_set_transpose(semitones, now);
                } else {
                    c->player_set_transpose(semitones, now);
                    c->set_practice_hands(hands, now);
                }
            }
            if (rng() % 3 == 0) {
                settle_tables(async, now); // sometimes the answer comes between two asks
            }
        }
        settle_tables(async, now);
        for (int i = 0; i < 4 && !async.tables_settled(); ++i) {
            settle_tables(async, now);
        }
        CAPTURE(seed);
        CHECK(async.tables_settled());
        CHECK(async.practice_hands() == plain.practice_hands());
        CHECK(async.transposition() == plain.transposition());
        std::int64_t a = now;
        std::int64_t b = now;
        CHECK(wait_keys(async, a) == wait_keys(plain, b));
        std::vector<SongSoundEvent> out;
        async.shutdown(a, out);
        plain.shutdown(b, out);
    }
}

TEST_CASE("tables asked for a long song are dropped when another song comes in") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_mode(PracticeMode::Wait, now);
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Left, now);
    c.load(two_hand_song(3, false, 60), now); // another song while the build runs
    c.wait_tables();
    c.update(now += 16 * kMs, {});
    // The hands asked for apply to the new song, built for it, never from the old song's tables.
    CHECK(c.practice_hands() == HandFilter::Left);
    for (int i = 0; i < 4 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    CHECK(c.tables_settled());
    SongController plain;
    plain.set_mode(PracticeMode::Wait, now);
    plain.load(two_hand_song(3, false, 60), now);
    plain.set_practice_hands(HandFilter::Left, now);
    std::int64_t a = now;
    std::int64_t b = now;
    CHECK(wait_keys(c, a) == wait_keys(plain, b));

    // An unload while a build runs leaves nothing pending.
    c.set_practice_hands(HandFilter::Right, a);
    c.unload(a);
    c.wait_tables();
    c.update(a += 16 * kMs, {});
    CHECK(c.tables_settled());
    std::vector<SongSoundEvent> out;
    c.shutdown(a, out);
    plain.shutdown(b, out);
}

TEST_CASE("a small song builds its tables at once, as before") {
    std::int64_t now = kSec;
    SongController c;
    c.set_mode(PracticeMode::Wait, now);
    c.load(two_hand_song(), now);
    const int rebuilds = c.evaluator_rebuilds();
    c.set_practice_hands(HandFilter::Left, now);
    CHECK(c.tables_settled());
    CHECK(c.evaluator_rebuilds() == rebuilds + 1); // in force on the call itself
    c.set_keyboard(KeyRange{36, 96}, now);
    c.player_set_transpose(2, now);
    CHECK(c.transposition().semitones() == 2);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("the tables job answers the newest request, and drops what a cancel ended") {
    const LoadedSong song = two_hand_song();
    FlowEvaluator holder;
    holder.set_song(song.song, FlowOptions{});
    const std::uint64_t fingerprint = song_fingerprint(song.song);
    auto request = [&](std::uint64_t serial, HandFilter hands, int semitones) {
        TablesRequest r;
        r.serial = serial;
        r.hands = hands;
        r.transposition = Transposition(semitones, KeyRange{36, 96});
        r.fingerprint = fingerprint;
        r.notes = holder.note_table();
        r.measures = holder.measure_table();
        return r;
    };
    PracticeTablesJob job;
    job.request(request(1, HandFilter::Left, 1));
    job.request(request(2, HandFilter::Right, 2));
    job.request(request(3, HandFilter::Both, 3));
    job.wait_idle();
    std::optional<TablesResult> got = job.take();
    REQUIRE(got.has_value());
    CHECK(got->serial == 3); // the newest; the older ones were replaced or overwritten
    CHECK(got->hands == HandFilter::Both);
    CHECK(got->transposition == Transposition(3, KeyRange{36, 96}));
    CHECK(got->flow.note_count() == song.song.notes.size());
    CHECK(got->wait.note_count() == song.song.notes.size());
    CHECK_FALSE(job.take().has_value()); // taken once

    job.request(request(4, HandFilter::Left, 0));
    job.cancel();
    job.wait_idle();
    CHECK_FALSE(job.take().has_value()); // cancelled: never answered
    CHECK_FALSE(job.busy());

    // Tables a swap replaced are freed on the job's thread.
    job.discard(FlowEvaluator::Prepared{}, WaitEvaluator::Prepared{});
    job.wait_idle();
}

// ---- Review round: dropped answers, grading changes and the gap, quitting with a build in flight
