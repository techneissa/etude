// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: the tables job frees its answers on the worker, stops between chunks, and follows a held stepper.
#include "alloc_count.hpp"
#include "practice_tables_job.hpp"
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"

#include "timing_factor.hpp"
#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

namespace {

// A song of about `n` notes in 4/4 at 240 BPM, a right-hand run with a chord now and then and a
// left-hand bass: enough targets that a build, or freeing its tables, is measurable.
LoadedSong long_song(std::size_t n) {
    Song s;
    s.tempo = make_tempo_map(480, 240.0);
    int melody = 72;
    for (std::int64_t step = 0; s.notes.size() < n; ++step) {
        auto add = [&](int pitch, Hand hand, int channel) {
            Note note;
            note.start_tick = step * 240;
            note.end_tick = note.start_tick + 230;
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.velocity = 80;
            note.hand = hand;
            note.channel = static_cast<std::uint8_t>(channel);
            note.track = static_cast<std::uint8_t>(channel);
            s.notes.push_back(note);
        };
        melody = 60 + static_cast<int>((step * 7) % 24);
        add(melody, Hand::Right, 0);
        if (step % 4 == 0) {
            add(melody - 4, Hand::Right, 0);
        }
        if (step % 2 == 0) {
            add(40 + static_cast<int>(step % 12), Hand::Left, 1);
        }
    }
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Long";
    out.hash = 0x10C0;
    out.library_path = "long.mid";
    return out;
}

} // namespace

TEST_CASE("a superseded answer is freed on the worker, not on the frame thread") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_mode(PracticeMode::Wait, now);
    c.load(long_song(6000), now);
    settle_tables(c, now);
    c.set_practice_hands(HandFilter::Left, now);
    c.wait_tables(); // the answer for the left hand is in, not taken
    // The ask changes: the update takes the answer, finds it stale and must not free it itself.
    etude::test::start_counting_allocations();
    c.set_practice_hands(HandFilter::Right, now);
    etude::test::stop_counting_allocations();
    const int frees = etude::test::frees_counted();
    CAPTURE(frees);
    CHECK(frees < 64); // thousands of targets and chords were in it
    settle_tables(c, now);
    for (int i = 0; i < 4 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    CHECK(c.tables_settled());
    CHECK(c.applied_hands() == HandFilter::Right);

    // The same for the answer nobody took when the ask came back to what was applied (A, B, A).
    c.set_practice_hands(HandFilter::Left, now);
    c.wait_tables();
    etude::test::start_counting_allocations();
    c.set_practice_hands(HandFilter::Right, now); // back: no pending change, the answer waits
    c.load(long_song(6000), now);                 // the next load drops it
    etude::test::stop_counting_allocations();
    // The load itself frees the old song; what matters is the answer's thousands are not in it.
    CHECK(etude::test::frees_counted() < 2000);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("the tables job frees a cancelled answer on its own thread") {
    const LoadedSong song = long_song(6000);
    FlowEvaluator holder;
    holder.set_song(song.song, FlowOptions{});
    PracticeTablesJob job;
    TablesRequest r;
    r.serial = 1;
    r.hands = HandFilter::Both;
    r.fingerprint = song_fingerprint(song.song);
    r.notes = holder.note_table();
    r.measures = holder.measure_table();
    job.request(std::move(r));
    job.wait_idle(); // the answer is in done_
    etude::test::start_counting_allocations();
    job.cancel();
    etude::test::stop_counting_allocations();
    CHECK(etude::test::frees_counted() < 8);
    job.wait_idle();
    CHECK_FALSE(job.take().has_value());
}

TEST_CASE("a change of grading while the tables of another change are built builds nothing here") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_mode(PracticeMode::Flow, now);
    c.load(long_song(6000), now);
    settle_tables(c, now);
    const std::uint64_t built = c.evaluator_builds();
    c.hold_tables(true);
    c.set_practice_hands(HandFilter::Left, now);
    SongOptions moved = c.options();
    moved.latency_ms = 35.0;
    c.set_options(moved, now);
    CHECK(c.options().latency_ms == 35.0);
    CHECK(c.evaluator_builds() == built);            // the tables were kept, not rebuilt inline
    CHECK(c.applied_hands() == HandFilter::Both);   // the swap is still to come
    CHECK_FALSE(c.tables_settled());
    c.hold_tables(false);
    settle_tables(c, now);
    for (int i = 0; i < 4 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    CHECK(c.tables_settled());
    CHECK(c.applied_hands() == HandFilter::Left);
    CHECK(c.evaluator_builds() == built); // the swap took the worker's tables, under the new options
    CHECK(c.options().latency_ms == 35.0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("until the swap, the notes, the sound and the grading all follow the old hands and key") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_keyboard(KeyRange{36, 96}, now);
    c.set_mode(PracticeMode::Flow, now);
    c.load(two_hand_song(), now);
    c.set_song_sound(SongSound::OtherHand, now); // the hand not practised sounds
    c.hold_tables(true);
    c.set_practice_hands(HandFilter::Left, now);
    c.player_set_transpose(2, now);
    // Asked for: what the toggles and the card show. In force: nothing yet.
    CHECK(c.practice_hands() == HandFilter::Left);
    CHECK(c.applied_hands() == HandFilter::Both);
    CHECK(c.transposition().semitones() == 0);
    c.play(now);
    const std::int64_t start = now;
    FakePiano piano;
    // C5 pressed on time: under the old hands and key it is a hit, and nothing is muted-out the
    // other way round (both hands practised: the other-hand sound has nobody to play).
    const std::array<MidiEvent, 1> press{note_on(72, start)};
    run(c, piano, now, start + 16 * kMs, press);
    run(c, piano, now, start + 600 * kMs);
    REQUIRE(c.summary() != nullptr);
    CHECK(c.summary()->counts.perfect == 1);
    CHECK(piano.note_ons == 0); // the right hand does not sound until the left is what is practised
    CHECK_FALSE(c.tables_settled());

    c.hold_tables(false);
    settle_tables(c, now);
    for (int i = 0; i < 4 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    REQUIRE(c.tables_settled());
    CHECK(c.applied_hands() == HandFilter::Left);
    CHECK(c.transposition().semitones() == 2);
    run(c, piano, now, start + 2 * kSec); // the right hand's notes now sound, the left's are the player's
    CHECK(piano.note_ons > 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("flow and wait: a table build asked to stop does so between its chunks") {
    const LoadedSong song = long_song(20000);
    FlowEvaluator holder;
    holder.set_song(song.song, FlowOptions{});
    const std::uint64_t fingerprint = song_fingerprint(song.song);

    int polls = 0;
    const FlowEvaluator::AbortCheck stop_third = [&] { return ++polls >= 3; };
    FlowEvaluator::Prepared flow = FlowEvaluator::prepare(
        holder.note_table(), holder.measure_table(), HandFilter::Both, Transposition{}, fingerprint,
        stop_third);
    CHECK(flow.note_count() == 0); // nothing built
    CHECK(polls >= 3);             // asked every few thousand notes, stopped at the third (the
    CHECK(polls <= 4);             // caller asks once more to see it)

    polls = 0;
    WaitEvaluator::Prepared wait = WaitEvaluator::prepare(
        holder.note_table(), HandFilter::Both, Transposition{}, fingerprint, stop_third);
    CHECK(wait.note_count() == 0);
    CHECK(polls >= 3);
    CHECK(polls <= 4);

    // Never told to stop, the same call builds: the chunks change nothing.
    const FlowEvaluator::AbortCheck never = [] { return false; };
    FlowEvaluator::Prepared whole = FlowEvaluator::prepare(
        holder.note_table(), holder.measure_table(), HandFilter::Both, Transposition{}, fingerprint,
        never);
    CHECK(whole.note_count() == song.song.notes.size());
}

TEST_CASE("a build in flight when the job is cancelled stops at its next check and is dropped") {
    const LoadedSong song = long_song(60000);
    FlowEvaluator holder;
    holder.set_song(song.song, FlowOptions{});
    // The worker is parked in its first check until the cancel is in: no clock decides whether the
    // build was still running when the stop came.
    std::mutex m;
    std::condition_variable cv;
    bool in_check = false;
    bool released = false;
    int checks_after_release = 0;
    PracticeTablesJob job;
    job.set_check_hook([&] {
        std::unique_lock lock(m);
        if (!in_check) {
            in_check = true;
            cv.notify_all();
            cv.wait(lock, [&] { return released; });
        } else if (released) {
            ++checks_after_release;
        }
    });
    TablesRequest r;
    r.serial = 1;
    r.hands = HandFilter::Right;
    r.fingerprint = song_fingerprint(song.song);
    r.notes = holder.note_table();
    r.measures = holder.measure_table();
    job.request(std::move(r));
    {
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return in_check; });
    }
    job.cancel(); // what the destructor does to a running build too: asks it to stop
    {
        const std::lock_guard lock(m);
        released = true;
    }
    cv.notify_all();
    job.wait_idle();
    CHECK(job.stopped_builds() == 1);          // the build ended early, not at its end
    CHECK(checks_after_release <= 1);          // at the first check after the stop: no chunk more
    CHECK_FALSE(job.take().has_value());       // and its answer was not kept
}

TEST_CASE("a transposition stepper held faster than the worker answers lands on its last key, "
          "installing keys in order") {
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_keyboard(KeyRange{24, 108}, now);
    c.set_mode(PracticeMode::Flow, now);
    c.load(long_song(8000), now);
    int last_installed = 0;
    int installs = 0;
    for (int tap = 1; tap <= 12; ++tap) {
        c.player_set_transpose(tap, now);
        CHECK(c.transpose() == tap); // the stepper's key follows the tap at once
        if (tap % 3 == 0) {
            c.wait_tables(); // a build finishes while the stepper is still held
        }
        for (int frame = 0; frame < 2; ++frame) {
            c.update(now += 8 * kMs, {});
            const int in_force = c.transposition().semitones();
            CHECK(in_force >= last_installed); // never an older key after a newer one
            if (in_force != last_installed) {
                ++installs;
            }
            last_installed = in_force;
        }
    }
    for (int i = 0; i < 12 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    REQUIRE(c.tables_settled());
    CHECK(c.transposition().semitones() == 12); // the last one tapped
    CHECK(installs >= 1);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a stepper held faster than a build lasts gets keys installed before the taps stop") {
    using Clock = std::chrono::steady_clock;
    std::int64_t now = kSec;
    SongController c;
    make_long(c);
    c.set_keyboard(KeyRange{24, 108}, now);
    c.set_mode(PracticeMode::Flow, now);
    c.load(long_song(40000), now);
    // Taps every 200 us, much faster than a build of 40,000 notes: an aborted build never ends.
    const auto begin = Clock::now();
    int tap = 0;
    bool installed_while_held = false;
    while (!installed_while_held && Clock::now() - begin < std::chrono::seconds(60)) { // the window only bounds a hang
        c.player_set_transpose(1 + (++tap % 2), now);
        c.update(now += 1 * kMs, {});
        installed_while_held = c.transposition().semitones() != 0;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    CHECK(installed_while_held);
    c.player_set_transpose(5, now); // the last tap
    for (int i = 0; i < 12 && !c.tables_settled(); ++i) {
        settle_tables(c, now);
    }
    REQUIRE(c.tables_settled());
    CHECK(c.transposition().semitones() == 5);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}
