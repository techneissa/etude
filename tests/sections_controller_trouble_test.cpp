// SPDX-License-Identifier: GPL-3.0-or-later
// sections_controller: trouble (split from sections_controller_test.cpp).
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

// ---- The trouble-bar run
// -------------------------------------------------------------------------

namespace {

// A player who presses every practised note of a Flow run on time, so a pass through a bar is
// clean. Events are made from the notes the song's position passed in the last step and handed to
// the next one, as the app's stand-in player does.
class FlawlessPlayer {
  public:
    void step(Rig& r, std::int64_t ms) {
        r.now += ms * kMs;
        std::stable_sort(pending_.begin(), pending_.end(),
                         [](const MidiEvent& a, const MidiEvent& b) { return a.time_ns < b.time_ns; });
        std::vector<MidiEvent> due;
        std::size_t n = 0;
        while (n < pending_.size() && pending_[n].time_ns <= r.now) {
            due.push_back(pending_[n++]);
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
        r.songs.update(r.now, due);
        r.songs.take_sound_events(r.sent);
        r.sections.update(r.now);
        r.sent.clear();
        const double pos = r.songs.position_s();
        // After a wrap, a seek or a start the notes from the loop's start on are still to come.
        if ((last_ < 0.0 || pos < last_) && r.songs.loop_enabled()) {
            last_ = r.songs.loop_start_s() - 1e-6;
        }
        if (r.songs.playing() && last_ >= 0.0 && pos > last_ && pos - last_ < 0.5) {
            const auto& notes = r.songs.song().song.notes;
            const double rate = r.songs.speed_percent() / 100.0;
            for (const Note& note : notes) {
                if (note.start_s <= last_ || note.start_s > pos) {
                    continue;
                }
                const auto arrival =
                    r.now - static_cast<std::int64_t>((pos - note.start_s) / rate * 1e9);
                MidiEvent on;
                on.type = MidiEventType::NoteOn;
                on.data1 =
                    static_cast<std::uint8_t>(r.songs.transposition().expected_pitch(note).pitch);
                on.data2 = 80;
                on.time_ns = arrival;
                on.source = InputSource::Pointer;
                MidiEvent off = on;
                off.type = MidiEventType::NoteOff;
                off.data2 = 0;
                off.time_ns = arrival + 100 * kMs;
                pending_.push_back(on);
                pending_.push_back(off);
            }
        }
        last_ = pos;
    }
    void forget() { last_ = -1.0; }

  private:
    std::vector<MidiEvent> pending_;
    double last_ = -1.0;
};

// A results card naming the given bars, hottest first, as a run names them.
ResultsCard card_naming(std::initializer_list<int> bars) {
    ResultsCard c;
    for (const int m : bars) {
        CardBar& b = c.bars[static_cast<std::size_t>(c.bar_count++)];
        b.measure = m;
        b.written = m;
        b.graded = 5;
        b.missed = 2;
    }
    return c;
}

// Starts a run of `bars` in that order (One by one in the player's own order).
void start_run(Rig& r, std::initializer_list<int> bars) {
    const std::vector<int> chosen(bars);
    const Song& song = r.songs.song().song;
    const BarPractice practice =
        plan_bar_practice(chosen, song.measures, song.duration_s, BarPracticeKind::OneByOne, true);
    const TroubleBars named = trouble_bars(card_naming(bars), practice);
    REQUIRE(r.sections.practise_bars(practice, r.now, &named));
    r.step(10);
}

// Plays on until `done` holds, at most `limit_s` seconds.
template <class F> bool play_until(Rig& r, FlawlessPlayer& p, F&& done, double limit_s = 30.0) {
    for (std::int64_t t = 0; t < static_cast<std::int64_t>(limit_s * 1000.0); t += 10) {
        if (done()) {
            return true;
        }
        p.step(r, 10);
    }
    return done();
}

} // namespace

TEST_CASE("a trouble-bar run starts on the first bar of the order the player chose") {
    Rig r;
    start_run(r, {5, 2, 3});
    const TroubleRun& run = r.sections.run();
    REQUIRE(run.phase() == RunPhase::Playing);
    CHECK(run.count() == 3);
    CHECK(run.bar(0).written == 5);
    CHECK(run.bar(1).written == 2);
    CHECK(run.bar(2).written == 3);
    CHECK(run.bar(0).before_pct == 60); // 5 graded, 2 missed
    CHECK(r.view().run_active);
    CHECK(r.view().plan_running);
    // The list is in that order too, and bar 5 loops first.
    REQUIRE(r.view().plan.size() == 3);
    CHECK(r.songs.loop_start_s() == doctest::Approx(8.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(10.0));
    CHECK(r.songs.playing());
}

TEST_CASE("a bar that is not played cleanly keeps looping and counts its passes") {
    Rig r;
    start_run(r, {2, 3});
    r.run(5'000); // nothing is pressed: every pass has misses
    const TroubleRun& run = r.sections.run();
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.current() == 0);
    CHECK(run.progress(0).passes >= 1);
    CHECK(run.progress(0).clean_passes == 0);
    CHECK(run.progress(0).missed > 0);
    CHECK(r.songs.loop_start_s() == doctest::Approx(2.0)); // still the same bar
    CHECK(r.songs.playing());
}

TEST_CASE("a clean pass pauses at the check-in, with the bar's loop kept and its numbers counted") {
    Rig r;
    FlawlessPlayer player;
    start_run(r, {2, 3});
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    const TroubleRun& run = r.sections.run();
    CHECK(run.current() == 0);
    CHECK(run.progress(0).end == BarEnd::Cleared);
    CHECK(run.progress(0).clean_passes >= 1);
    CHECK(run.progress(0).accuracy_pct() == 100);
    CHECK_FALSE(r.songs.playing());
    CHECK(r.songs.loop_enabled());
    CHECK(r.songs.loop_start_s() == doctest::Approx(2.0));
    CHECK(r.view().plan_running);
    CHECK(r.view().run_active); // the band still shows its skip and end
    // It waits there.
    r.run(1'000);
    CHECK(run.phase() == RunPhase::CheckIn);
    CHECK_FALSE(r.songs.playing());
}

TEST_CASE("Next bar loops the following bar and plays; the last bar's Next ends the run") {
    Rig r;
    FlawlessPlayer player;
    start_run(r, {2, 3});
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    r.sections.run_command(RunCommand::Next, r.now);
    r.step(10);
    const TroubleRun& run = r.sections.run();
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.current() == 1);
    CHECK(r.view().plan_item == 1);
    CHECK(r.songs.loop_start_s() == doctest::Approx(4.0));
    CHECK(r.songs.loop_end_s() == doctest::Approx(6.0));
    CHECK(r.songs.playing());
    player.forget();
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    CHECK(run.current() == 1);
    r.sections.run_command(RunCommand::Next, r.now);
    r.step(10);
    CHECK(run.phase() == RunPhase::Done);
    CHECK(run.finished());
    CHECK(run.cleared() == 2);
    CHECK_FALSE(r.view().run_active);
    CHECK_FALSE(r.view().plan_running);
    CHECK_FALSE(r.songs.loop_enabled());
    CHECK_FALSE(r.songs.playing());
}

TEST_CASE("Again plays the same bar once more, from its start") {
    Rig r;
    FlawlessPlayer player;
    start_run(r, {2, 3});
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    r.sections.run_command(RunCommand::Again, r.now);
    r.step(10);
    const TroubleRun& run = r.sections.run();
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.current() == 0);
    CHECK(r.songs.playing());
    CHECK(r.songs.loop_start_s() == doctest::Approx(2.0));
    CHECK(r.songs.position_s() < 2.5);
    player.forget();
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    CHECK(run.progress(0).clean_passes >= 2); // the numbers kept adding up
}

TEST_CASE("Skip at the check-in passes over the next bar and plays the one after") {
    Rig r;
    FlawlessPlayer player;
    start_run(r, {2, 3, 5});
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    r.sections.run_command(RunCommand::Skip, r.now);
    r.step(10);
    const TroubleRun& run = r.sections.run();
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.current() == 2);
    CHECK(run.progress(1).end == BarEnd::Skipped);
    CHECK(r.songs.loop_start_s() == doctest::Approx(8.0));
    CHECK(r.songs.playing());
}

TEST_CASE("the band's skip gives up the bar being practised and goes on; end ends the run") {
    Rig r;
    start_run(r, {2, 3, 5});
    r.run(500);
    r.sections.plan_skip(1, r.now); // the band's skip and the . key
    r.step(10);
    const TroubleRun& run = r.sections.run();
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.current() == 1);
    CHECK(run.progress(0).end == BarEnd::Skipped);
    CHECK(r.view().plan_item == 1);
    CHECK(r.songs.loop_start_s() == doctest::Approx(4.0));
    r.sections.plan_skip(-1, r.now); // back is this bar again
    r.step(10);
    CHECK(run.current() == 1);
    CHECK(run.phase() == RunPhase::Playing);
    r.sections.plan_stop(r.now); // the band's end and the / key
    r.step(10);
    CHECK(run.phase() == RunPhase::Done);
    CHECK(run.ended());
    CHECK(run.progress(1).end == BarEnd::Ended);
    CHECK(run.progress(2).end == BarEnd::Waiting);
    CHECK_FALSE(r.view().run_active);
    CHECK_FALSE(r.view().plan_running);
    CHECK_FALSE(r.songs.loop_enabled());
    CHECK_FALSE(r.songs.playing());
}

TEST_CASE("the summary stays until it is dismissed, and a new song forgets it") {
    Rig r;
    start_run(r, {2, 3});
    r.sections.run_command(RunCommand::End, r.now);
    r.step(10);
    CHECK(r.sections.run().phase() == RunPhase::Done);
    r.step(1'000);
    CHECK(r.sections.run().phase() == RunPhase::Done);
    r.sections.run_dismiss();
    CHECK(r.sections.run().phase() == RunPhase::Idle);

    start_run(r, {2, 3});
    r.sections.run_command(RunCommand::End, r.now);
    r.step(10);
    r.songs.load(song_of(8, 0x78), r.now);
    r.step(10);
    CHECK(r.sections.run().phase() == RunPhase::Idle);
}

TEST_CASE("a run ends, and says so, when its loop goes away") {
    Rig r;
    start_run(r, {2, 3});
    r.run(200);
    (void)r.sections.take_message();
    r.songs.clear_loop(r.now); // the \ key
    r.step(10);
    CHECK(r.sections.run().phase() == RunPhase::Done);
    CHECK(r.sections.run().ended());
    CHECK(r.sections.take_message() == "Run ended: its loop was cleared");
    CHECK_FALSE(r.view().run_active);
    // The song is paused, as for the End button, so the summary is not dismissed by it playing on.
    CHECK_FALSE(r.songs.playing());
    r.step(2'000);
    CHECK_FALSE(r.songs.playing());
    CHECK(r.sections.run().phase() == RunPhase::Done);
}

TEST_CASE("a scrub that ends the loop during a bar also ends the run with the song paused") {
    Rig r;
    start_run(r, {2, 3});
    r.run(200);
    r.songs.seek(12.0, r.now); // outside the bar's loop: it is cleared
    r.step(10);
    CHECK(r.sections.run().phase() == RunPhase::Done);
    CHECK(r.sections.run().ended());
    CHECK_FALSE(r.songs.playing());
    r.step(1'000);
    CHECK(r.sections.run().phase() == RunPhase::Done);
}

TEST_CASE("the band's skip is the check-in's Skip, and has nothing to do after the last bar") {
    Rig r;
    FlawlessPlayer player;
    start_run(r, {2, 3, 5});
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    CHECK(r.view().run_can_skip);
    r.sections.plan_skip(1, r.now); // the band's skip and the . key
    r.step(10);
    const TroubleRun& run = r.sections.run();
    CHECK(run.current() == 2); // the next bar passed over, as the card's Skip does
    CHECK(run.progress(1).end == BarEnd::Skipped);
    player.forget();
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    CHECK(run.current() == 2);
    CHECK_FALSE(r.view().run_can_skip); // the last bar's check-in
    r.sections.plan_skip(1, r.now);
    r.step(10);
    CHECK(run.phase() == RunPhase::CheckIn); // nothing happened
    CHECK_FALSE(r.songs.playing());
}

TEST_CASE("starting the song again at the check-in is Again, and the check-in goes") {
    Rig r;
    FlawlessPlayer player;
    start_run(r, {2, 3});
    REQUIRE(play_until(r, player, [&] { return r.sections.run().phase() == RunPhase::CheckIn; }));
    r.songs.toggle_play(r.now); // the transport button, or the space key with no card
    r.step(10);
    CHECK(r.sections.run().phase() == RunPhase::Playing);
    CHECK(r.sections.run().current() == 0);
}

TEST_CASE("one bar made of two pieces by a cut is not a run, and the list still plays") {
    Rig r;
    r.sections.split_at(3.0, r.now); // a cut in the middle of bar 2 (2 s to 4 s)
    r.step(10);
    const std::vector<int> chosen{2};
    const Song& song = r.songs.song().song;
    const BarPractice practice =
        plan_bar_practice(chosen, song.measures, song.duration_s, BarPracticeKind::OneByOne, true);
    const TroubleBars named = trouble_bars(card_naming({2}), practice);
    REQUIRE(r.sections.practise_bars(practice, r.now, &named));
    r.step(10);
    CHECK(r.view().plan.size() == 2); // the pieces of the bar
    CHECK(r.view().plan_running);
    CHECK(r.sections.run().phase() == RunPhase::Idle); // items and bars no longer match
    CHECK_FALSE(r.view().run_active);
}

TEST_CASE("Together never starts a run") {
    Rig r;
    const std::vector<int> chosen{2, 3, 6};
    const Song& song = r.songs.song().song;
    const BarPractice practice =
        plan_bar_practice(chosen, song.measures, song.duration_s, BarPracticeKind::Together);
    const TroubleBars named = trouble_bars(card_naming({2, 3, 6}), practice);
    REQUIRE(r.sections.practise_bars(practice, r.now, &named));
    r.step(10);
    CHECK(r.sections.run().phase() == RunPhase::Idle);
    CHECK(r.view().plan_running);
}
