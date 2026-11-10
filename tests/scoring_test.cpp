// SPDX-License-Identifier: GPL-3.0-or-later
// Matching and scoring: grade on the window edge, an extra press never steals a note, a wrong key
// is counted apart, a chord waits for every key, only the practised hand is graded, release, pedal
// and velocity 0 are not presses, a queued press at a seek, loop or song switch is not wrong, a run
// with no play is not saved, damaged run records are refused.
#include "etude/ear_training.hpp"
#include "etude/evaluator.hpp"
#include "etude/keyboard.hpp"
#include "etude/run_columns.hpp"
#include "etude/run_record.hpp"
#include "etude/run_score.hpp"
#include "etude/session_restore.hpp"
#include "etude/song_practice.hpp"
#include "etude/transport.hpp"
#include "history_store.hpp"
#include "runs_file.hpp"
#include "song_controller_test_support.hpp"
#include "song_practice_sync.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <functional>
#include <random>
#include <thread>
#include <tuple>

namespace {

using namespace etude;
using namespace etude::app;
using namespace song_test;
using OpKind = TransportOp::Kind;

constexpr std::int64_t sec(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }
constexpr std::int64_t ms(double m) { return static_cast<std::int64_t>(std::llround(m * 1e6)); }

struct N {
    double start;
    int pitch;
    Hand hand = Hand::Right;
    double dur = 0.25;
};

// Bars of 2 s each (4/4 at 120 BPM), so a note's bar is floor(start / 2) + 1.
Song song_of(const std::vector<N>& list, double tail_s = 2.0) {
    Song s;
    double end = 0.0;
    for (const N& n : list) {
        Note note;
        note.start_s = n.start;
        note.duration_s = n.dur;
        note.pitch = static_cast<std::uint8_t>(n.pitch);
        note.hand = n.hand;
        note.measure = static_cast<int>(std::floor(n.start / 2.0)) + 1;
        note.start_tick = static_cast<std::int64_t>(std::llround(n.start * 960.0));
        note.end_tick = note.start_tick + std::max<std::int64_t>(1, std::llround(n.dur * 960.0));
        s.notes.push_back(note);
        end = std::max(end, n.start + n.dur);
    }
    std::stable_sort(s.notes.begin(), s.notes.end(), [](const Note& a, const Note& b) {
        return a.start_s < b.start_s || (a.start_s == b.start_s && a.pitch < b.pitch);
    });
    s.duration_s = end + tail_s;
    for (int bar = 0; bar * 2.0 < s.duration_s; ++bar) {
        s.measures.push_back({.number = bar + 1, .start_tick = bar * 1920, .start_s = bar * 2.0});
    }
    return s;
}

Song song_of(std::initializer_list<N> list, double tail_s = 2.0) {
    return song_of(std::vector<N>(list), tail_s);
}

MidiEvent press(double wall_s, int key, int vel = 90) {
    return {.time_ns = sec(wall_s),
            .type = MidiEventType::NoteOn,
            .data1 = static_cast<std::uint8_t>(key),
            .data2 = static_cast<std::uint8_t>(vel)};
}

MidiEvent release(double wall_s, int key) {
    return {.time_ns = sec(wall_s),
            .type = MidiEventType::NoteOff,
            .data1 = static_cast<std::uint8_t>(key)};
}

MidiEvent cc(double wall_s, int number, int value, bool synthetic = false) {
    return {.time_ns = sec(wall_s),
            .type = MidiEventType::ControlChange,
            .data1 = static_cast<std::uint8_t>(number),
            .data2 = static_cast<std::uint8_t>(value),
            .synthetic = synthetic};
}

// A Transport and an evaluator driven like the app's frame loop. Play starts at wall time 1 s, so
// at 100% a note at song time s arrives at wall time 1 + s.
template <typename Evaluator, typename Options> struct Rig {
    static constexpr std::int64_t frame = 16'666'667;
    Song song;
    Transport transport;
    Evaluator eval;
    std::vector<MidiEvent> inbox;
    std::int64_t now = 0;

    explicit Rig(const Song& s, const Options& o = {}) : song(s) {
        transport.set_song(song);
        eval.set_song(song, o);
    }
    template <typename... E> void send(const E&... events) { (inbox.push_back(events), ...); }
    const EvaluationSummary& summary() const { return eval.summary(); }
    void play_at(double wall_s, int speed = 100) {
        transport.set_speed_percent(sec(wall_s), speed);
        transport.play(sec(wall_s));
        now = sec(wall_s);
        flush_ops();
    }
    void flush_ops() {
        std::vector<TransportOp> ops;
        transport.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
    }
    // Starts playing at wall time 1 s and runs to `until`.
    void begin(double until) {
        play_at(1.0);
        run_until(until);
    }
    void step() { step(now + frame); }
    void step(std::int64_t to) {
        now = to;
        transport.update(now);
        flush_ops();
        std::stable_sort(inbox.begin(), inbox.end(), [](const MidiEvent& a, const MidiEvent& b) {
            return a.time_ns < b.time_ns;
        });
        std::size_t used = 0;
        for (; used < inbox.size() && inbox[used].time_ns <= now; ++used) {
            eval.apply(inbox[used]);
        }
        inbox.erase(inbox.begin(), inbox.begin() + static_cast<std::ptrdiff_t>(used));
        if constexpr (std::is_same_v<Evaluator, WaitEvaluator>) {
            const WaitDirective d = eval.update(now);
            if (d.hold_now) {
                transport.hold(now);
            }
            if (d.release) {
                transport.release(now);
            }
            if (d.arm_hold) {
                transport.hold_at(now, d.hold_at_s);
            }
            flush_ops();
        } else {
            eval.update(now);
        }
    }
    void run_until(double wall_s) {
        const std::int64_t end = sec(wall_s);
        while (now + frame <= end) {
            step();
        }
        if (now < end) {
            step(end);
        }
    }
};

using FlowRig = Rig<FlowEvaluator, FlowOptions>;
using WaitRig = Rig<WaitEvaluator, WaitOptions>;

int hits(const EvaluationSummary& s) {
    return s.counts.perfect + s.counts.good + s.counts.early + s.counts.late;
}

const NoteResult* result_for(const EvaluationSummary& s, std::uint32_t note) {
    for (const NoteResult& r : s.notes) {
        if (r.note_index == note) {
            return &r;
        }
    }
    return nullptr;
}

// Flow Mode played from wall 1 s with these presses, run past the end of the song.
EvaluationSummary flow(const Song& song, std::initializer_list<MidiEvent> presses,
                       const FlowOptions& o = {}) {
    FlowRig rig(song, o);
    rig.play_at(1.0);
    rig.inbox.assign(presses);
    rig.run_until(2.0 + song.duration_s);
    return rig.summary();
}

// A copy of the keys a Wait evaluator is waiting for (waiting_keys() is a view that the next call
// replaces).
std::vector<int> keys_of(std::span<const int> keys) { return {keys.begin(), keys.end()}; }

// A song with what a frame of grading meets: runs of one key, chords and a held bass in both
// hands, the player a little early or late, one note missed and a wrong key now and then.
struct PlayedSong {
    Song song;
    std::vector<MidiEvent> presses; // wall time order
};

PlayedSong played_song() {
    std::vector<N> list;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-0.030, 0.030);
    double t = 0.5;
    for (int phrase = 0; phrase < 24; ++phrase) {
        for (int i = 0; i < 12; ++i, t += 0.060) { // a run on one key, 60 ms apart: one group
            list.push_back({t, 60, Hand::Right, 0.05});
        }
        for (int i = 0; i < 4; ++i, t += 0.4) { // a chord and its bass
            list.push_back({t, 64, Hand::Right, 0.3});
            list.push_back({t, 67, Hand::Right, 0.3});
            list.push_back({t, 48, Hand::Left, 0.6});
        }
        t += 0.3;
    }
    PlayedSong out{song_of(list), {}};
    int index = 0;
    for (const Note& n : out.song.notes) {
        if (++index % 17 == 0) {
            continue; // missed
        }
        out.presses.push_back(press(1.0 + n.start_s + jitter(rng), n.pitch));
        if (index % 11 == 0) {
            out.presses.push_back(press(1.0 + n.start_s + 0.01, n.pitch + 1)); // a wrong key
        }
    }
    std::stable_sort(out.presses.begin(), out.presses.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.time_ns < b.time_ns; });
    return out;
}

TEST_CASE("timeline: a press is placed in the song through the moves, at any speed") {
    TransportTimeline tl;
    tl.apply({.kind = OpKind::Move,
              .at_ns = sec(10),
              .end_ns = sec(12),
              .from_s = 0.0,
              .to_s = 1.0,
              .speed_percent = 50});
    tl.apply({.kind = OpKind::Hold, .at_ns = sec(12), .to_s = 1.0});
    CHECK(tl.song_at(sec(11)) == doctest::Approx(0.5));
    CHECK(tl.song_at(sec(15)) == doctest::Approx(1.0)); // held
    CHECK(tl.song_at(sec(5)) == doctest::Approx(0.0));  // before the first op
    CHECK(tl.rate_at(sec(11)) == doctest::Approx(0.5));
}

TEST_CASE("flow: grade boundaries and the window edge, at the threshold and one nanosecond past") {
    // Window 80 ms: Perfect to 28 ms and Good to 56 ms (0.35 and 0.70 of it), Early or Late to
    // 80 ms. One note a second, one press each, error in ns (+ late).
    const std::pair<std::int64_t, NoteGrade> rows[] = {
        {ms(28), NoteGrade::Perfect},    {-ms(28), NoteGrade::Perfect},
        {ms(28) + 1, NoteGrade::Good},   {-ms(28) - 1, NoteGrade::Good},
        {ms(56), NoteGrade::Good},       {-ms(56), NoteGrade::Good},
        {ms(56) + 1, NoteGrade::Late},   {-ms(56) - 1, NoteGrade::Early},
        {ms(80), NoteGrade::Late},       {-ms(80), NoteGrade::Early},
        {ms(80) + 1, NoteGrade::Missed}, {-ms(80) - 1, NoteGrade::Missed},
    };
    std::vector<N> list;
    std::vector<MidiEvent> presses;
    for (std::size_t i = 0; i < std::size(rows); ++i) {
        const double t = 1.0 + static_cast<double>(i);
        list.push_back({t, 40 + static_cast<int>(i)}); // one key each
        presses.push_back(press(t + 1.0, 40 + static_cast<int>(i)));
        presses.back().time_ns += rows[i].first;
    }
    FlowRig rig(song_of(list), {.window_ms = 80.0});
    rig.play_at(1.0);
    rig.inbox = presses;
    rig.run_until(2.0 + static_cast<double>(std::size(rows)) + 2.0);
    for (std::size_t i = 0; i < std::size(rows); ++i) {
        CAPTURE(i);
        CHECK(result_for(rig.summary(), static_cast<std::uint32_t>(i))->grade == rows[i].second);
    }
    CHECK(rig.summary().wrong == 1); // one past the late edge; one past the early edge is early
}

TEST_CASE("flow: latency correction is subtracted from each press before grading") {
    // The player's lag of 60 ms: really on time.
    const EvaluationSummary s =
        flow(song_of({{1.0, 60}}), {press(2.060, 60)}, {.latency_ms = 60.0});
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 0)->error_ns == 0);
}

TEST_CASE("flow: the assignment maximises matches where nearest-first would lose one") {
    // Same key at song 1.0 and 1.1. Presses 70 ms and 170 ms after the first: nearest-first
    // gives the first press to the second note and leaves the first note missed.
    const EvaluationSummary s =
        flow(song_of({{1.0, 60}, {1.1, 60}}), {press(2.070, 60), press(2.170, 60)});
    CHECK(result_for(s, 0)->grade == NoteGrade::Late);
    CHECK(result_for(s, 1)->grade == NoteGrade::Late);
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 0);
}

TEST_CASE("flow: an extra press near a note never steals it; one after it is wrong") {
    const Song song = song_of({{1.0, 60}});
    // Early extra: the right key early, not wrong; the closer press grades the note.
    EvaluationSummary s = flow(song, {press(1.930, 60), press(2.005, 60)});
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 0)->error_ns == ms(5));
    CHECK(s.wrong == 0);
    CHECK(s.longest_combo == 1);
    // Late extra: wrong, and it breaks the combo.
    s = flow(song, {press(2.005, 60), press(2.070, 60)});
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(s.wrong == 1);
    CHECK(s.longest_combo == 1);
    CHECK(s.combo == 0);
}

TEST_CASE("flow: a wrong key is counted apart and resets the combo") {
    const EvaluationSummary s =
        flow(song_of({{1.0, 60}, {2.0, 62}, {3.0, 64}}),
             {press(2.0, 60), press(2.5, 61), press(3.0, 62), press(4.0, 64)});
    CHECK(hits(s) == 3);
    CHECK(s.wrong == 1);
    CHECK(s.longest_combo == 2);
    CHECK(s.combo == 2);
}

TEST_CASE("flow: a chord rolled inside the window is all hits") {
    const EvaluationSummary s =
        flow(song_of({{1.0, 60}, {1.0, 64}, {1.0, 67}, {1.0, 72}}),
             {press(1.940, 72), press(1.980, 67), press(2.025, 64), press(2.075, 60)});
    CHECK(hits(s) == 4);
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 0);
    CHECK(result_for(s, 0)->grade == NoteGrade::Late);  // 60, +75 ms
    CHECK(result_for(s, 3)->grade == NoteGrade::Early); // 72, -60 ms
}

TEST_CASE("flow: both hands on one key at once take one press and both are hits") {
    const EvaluationSummary s =
        flow(song_of({{1.0, 60, Hand::Left}, {1.0, 60, Hand::Right}}), {press(2.010, 60)});
    REQUIRE(s.notes.size() == 2);
    CHECK(s.notes[0].grade == NoteGrade::Perfect);
    CHECK(s.notes[1].grade == NoteGrade::Perfect);
    CHECK(s.left.hit == 1);
    CHECK(s.right.hit == 1);
    CHECK(s.wrong == 0);
}

TEST_CASE("flow: only the practised hand is graded; the piano's notes and drums never are") {
    Song song = song_of(
        {{1.0, 60, Hand::Right}, {1.0, 48, Hand::Left}, {2.0, 50, Hand::Unknown}, {1.5, 38}});
    for (Note& n : song.notes) {
        n.drum = n.pitch == 38; // a drum note is never graded, whatever the hand asked for
    }
    const EvaluationSummary s = flow(song, {press(2.0, 60)}, {.hands = HandFilter::Right});
    REQUIRE(s.notes.size() == 1);
    CHECK(s.notes[0].note_index == 1); // the right-hand C4 (sorted after the left-hand C3)
    CHECK(s.counts.missed == 0);
    CHECK(s.left.graded == 0);
    CHECK(s.right.accuracy().value() == 1.0);
}

TEST_CASE("flow: the expected key comes from the transposition") {
    // 95 + 2 = 97 folds down to 85; the untransposed key is wrong.
    const EvaluationSummary s =
        flow(song_of({{1.0, 60}, {2.0, 95}}), {press(2.0, 60), press(2.0, 62), press(3.0, 85)},
             {.transposition = Transposition(2, kRange61)});
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 0)->expected_pitch == 62);
    CHECK(result_for(s, 1)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 1)->expected_pitch == 85);
    CHECK(s.wrong == 1);
}

TEST_CASE("flow: an unplayable note is neither hit nor missed, and is counted") {
    const EvaluationSummary s =
        flow(song_of({{1.0, 60}, {2.0, 66}}), {press(2.0, 60)},
             {.transposition = Transposition(0, KeyRange{60, 64})}); // narrower than an octave
    CHECK(s.notes.size() == 1);
    CHECK(s.counts.missed == 0);
    CHECK(s.unplayable == 1);
}

TEST_CASE("flow: events stamped out of order across ports by a few ms grade the same") {
    FlowRig rig(song_of({{1.0, 60}, {1.0, 64}, {1.05, 60}}));
    rig.begin(2.06);
    // Port B's 64 (stamped 2.004) and second 60 (stamped 2.052) are delivered after port A's
    // later stamps.
    for (const MidiEvent& e :
         {press(2.001, 60), press(2.055, 60), press(2.004, 64), press(2.052, 60)}) {
        rig.eval.apply(e);
    }
    rig.run_until(4.0);
    CHECK(rig.summary().counts.missed == 0);
    CHECK(rig.summary().wrong == 1);
    CHECK(result_for(rig.summary(), 0)->error_ns == ms(1));
    CHECK(result_for(rig.summary(), 2)->error_ns == ms(2)); // the 2.052 press, not the later 2.055
}

TEST_CASE("flow: releases, the pedal, velocity-0 note-ons and synthetic events are not presses") {
    const Song song = song_of({{1.0, 60}});
    const EvaluationSummary s =
        flow(song, {press(2.0, 60, 0), release(2.01, 60), cc(2.02, kCcSustain, 127),
                    cc(2.03, kCcAllNotesOff, 0, true)});
    CHECK(s.counts.missed == 1);
    CHECK(s.wrong == 0);
    // Velocity 1 is the softest real press: it grades.
    CHECK(flow(song, {press(2.0, 60, 1)}).counts.perfect == 1);
}

TEST_CASE("flow: presses while paused or before play are ignored") {
    FlowRig rig(song_of({{1.0, 60}, {3.0, 62}}));
    rig.send(press(0.5, 70)); // before play
    rig.play_at(1.0);
    rig.send(press(2.0, 60));
    rig.run_until(2.5);
    rig.transport.pause(sec(2.5));
    rig.send(press(3.0, 71)); // noodling while paused
    rig.run_until(4.0);
    rig.play_at(4.0);
    rig.send(press(5.5, 62)); // song 3.0 at wall 4 + 1.5
    rig.run_until(7.0);
    CHECK(rig.summary().wrong == 0);
    CHECK(hits(rig.summary()) == 2);
}

TEST_CASE("flow: a seek drops undecided notes and presses, neither hit nor missed") {
    FlowRig rig(song_of({{1.0, 60}, {5.0, 62}}));
    rig.begin(2.02); // note 0 arrived at 2.0, its window still open
    rig.transport.seek(sec(2.02), 4.5);
    rig.send(press(2.03, 60)); // stamped after the seek: the old note is gone, so wrong
    rig.send(press(2.52, 62)); // song 5.0 arrives at 2.02 + 0.5
    rig.run_until(4.0);
    CHECK(result_for(rig.summary(), 0) == nullptr);
    CHECK(result_for(rig.summary(), 1)->grade == NoteGrade::Perfect);
    CHECK(rig.summary().counts.missed == 0);
    CHECK(rig.summary().wrong == 1);
}

TEST_CASE("flow: accuracy is empty when nothing was graded, never 100%; finish decides all") {
    CHECK_FALSE(HandStats{}.accuracy().has_value());
    REQUIRE(HandStats{.graded = 4, .hit = 3}.accuracy().has_value());
    CHECK(*HandStats{.graded = 4, .hit = 3}.accuracy() == doctest::Approx(0.75));
    FlowRig rig(song_of({{1.0, 60}}));
    rig.begin(2.01);
    CHECK(rig.eval.pending_notes() == 1);
    rig.eval.finish();
    CHECK(rig.summary().counts.missed == 1);
    CHECK(rig.eval.pending_notes() == 0);
}

TEST_CASE("ear answers: a right answer is graded right and a wrong one wrong") {
    EarQuestion note{.kind = EarQuestionKind::Note, .first = 64, .second = 64}; // asks 64
    // {pressed key, any octave, right}
    for (const auto& [key, any_octave, right] :
         std::initializer_list<std::tuple<int, bool, bool>>{{64, false, true},
                                                            {65, false, false},
                                                            {52, false, false},
                                                            {52, true, true},
                                                            {76, true, true},
                                                            {53, true, false}}) {
        CHECK(check_pitch_answer(note, key, any_octave) == right);
    }
    // 60 then 67: the answer is the second note, or 7 semitones.
    const EarQuestion up{.kind = EarQuestionKind::Interval, .first = 60, .second = 67};
    CHECK(check_pitch_answer(up, 67));
    CHECK_FALSE(check_pitch_answer(up, 60));
}

TEST_CASE("wait: the song holds at a chord and goes on when all its keys are down at once") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {1.0, 67}, {2.0, 72}}));
    rig.begin(2.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 1.0);
    CHECK(rig.eval.waiting_for_s() == 1.0);
    CHECK(keys_of(rig.eval.waiting_keys()) == std::vector<int>{60, 64, 67});
    rig.send(press(2.5, 60), press(2.6, 64));
    rig.run_until(2.8);
    CHECK(rig.transport.held()); // two of three
    rig.send(press(2.8, 67));
    rig.run_until(2.9);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.transport.position_s() > 1.0);
    const EvaluationSummary& s = rig.summary();
    REQUIRE(s.notes.size() == 3);
    CHECK(s.counts.played == 3);
    CHECK(s.notes[0].grade == NoteGrade::Played);
    CHECK(s.notes[0].arrival_ns == sec(2.0));
    CHECK(s.notes[0].error_ns == ms(800)); // how long the song waited
    CHECK(rig.eval.waiting_for_s() == 2.0);
}

TEST_CASE("wait: a key let go, under the pedal, by velocity 0 or by All Notes Off does not count") {
    // Each row is sent after the chord {60, 64} starts waiting; 60 must be down with 64 for the
    // chord to play, so none of these lets the song go on. Pressing 60 again then plays it.
    const std::vector<MidiEvent> rows[] = {
        {press(2.5, 60), release(2.6, 60), press(2.7, 64)},
        {cc(2.5, kCcSustain, 63), press(2.6, 60), release(2.7, 60), press(2.8, 64)},
        {cc(2.5, kCcSustain, 64), press(2.6, 60), release(2.7, 60), press(2.8, 64)},
        {cc(2.5, kCcSustain, 127), press(2.6, 60), release(2.7, 60), press(2.8, 64)},
        {press(2.6, 60), press(2.7, 60, 0), press(2.8, 64)},
        {press(2.6, 60), cc(2.7, kCcAllNotesOff, 0, true), press(2.8, 64)},
    };
    for (const auto& row : rows) {
        WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}));
        rig.begin(2.5);
        rig.inbox = row;
        rig.run_until(3.0);
        CHECK(rig.transport.held());
        rig.send(press(3.0, 60));
        rig.run_until(3.1);
        CHECK_FALSE(rig.transport.held());
        CHECK(rig.summary().wrong == 0);
    }
}

TEST_CASE("wait: a key already held does not count until it is pressed again") {
    WaitRig rig(song_of({{1.0, 60}, {2.0, 60}, {4.0, 72}}));
    rig.begin(2.5);
    rig.send(press(2.5, 60)); // plays the first chord and stays down
    rig.run_until(4.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 2.0);
    rig.send(release(4.5, 60), press(4.6, 60));
    rig.run_until(4.7);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.summary().counts.played == 2);
}

TEST_CASE("wait: a press 150 ms early counts, 151 ms does not and is not wrong either") {
    for (const double early_ms : {150.0, 151.0}) {
        CAPTURE(early_ms);
        WaitRig rig(song_of({{1.0, 60}, {3.0, 72}}), {.early_ms = 150.0});
        rig.play_at(1.0);
        rig.send(press(2.0 - early_ms / 1000.0, 60));
        rig.run_until(2.7);
        const bool counted = early_ms <= 150.0;
        CHECK(rig.transport.held() == !counted);
        CHECK(rig.summary().counts.played == (counted ? 1 : 0));
        CHECK(rig.summary().wrong == 0);
    }
}

TEST_CASE("wait: wrong notes are counted in the waiting chord's bar and never block it") {
    WaitRig rig(song_of({{3.0, 60}, {5.0, 72}}));
    rig.begin(4.5);
    rig.send(press(4.5, 61), press(4.6, 60)); // 61 is still down
    rig.run_until(4.7);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.summary().wrong == 1);
    REQUIRE_FALSE(rig.summary().measures.empty());
    CHECK(rig.summary().measures[0].measure == 2);
    CHECK(rig.summary().measures[0].wrong == 1);
}

TEST_CASE("wait: only the practised hand is waited for") {
    Song song = song_of({{1.0, 72, Hand::Right},
                         {1.5, 38, Hand::Left},
                         {2.0, 48, Hand::Left},
                         {4.0, 50, Hand::Left}});
    song.notes[1].drum = true; // a drum is not waited for either
    WaitRig rig(song, {.hands = HandFilter::Left});
    rig.begin(3.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 2.0);
}

TEST_CASE("wait: a seek waits for the first chord after it and keys down must be pressed again") {
    WaitRig rig(song_of({{1.0, 60}, {2.0, 64}, {3.0, 60}, {5.0, 72}}));
    rig.begin(2.5);
    rig.send(press(2.5, 60)); // plays bar 1's chord, stays down
    rig.run_until(2.6);
    rig.transport.seek(sec(2.6), 2.5);
    rig.run_until(4.0);
    CHECK(rig.eval.waiting_for_s() == 3.0);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 3.0);
    rig.send(release(4.0, 60), press(4.1, 60));
    rig.run_until(4.2);
    CHECK_FALSE(rig.transport.held());
}

// A press still in the inbox when the state it was played under changes.

TEST_CASE("wait: a press still queued when the transposition changes is not marked wrong") {
    // C4 at song 3.0 arrives at wall 4.0. C4 is pressed 10 ms early, the player taps transpose +2
    // 5 ms later and the press is still inside the settle window.
    for (const bool transpose : {false, true}) {
        WaitRig rig(song_of({{3.0, 60}}));
        rig.play_at(1.0);
        rig.send(press(3.99, 60));
        rig.run_until(3.99);
        if (transpose) {
            rig.eval.set_transposition(Transposition(2, kRange61), sec(3.995));
        }
        for (const double t : {4.00, 4.01, 4.1}) {
            rig.run_until(t);
        }
        CHECK(rig.summary().counts.played == 1);
        CHECK(rig.summary().wrong == 0);
    }
}

TEST_CASE("wait: a press stamped after the transposition change is judged by the new mapping") {
    WaitRig rig(song_of({{3.0, 60}}));
    rig.begin(3.9);
    rig.eval.set_transposition(Transposition(2, kRange61), sec(3.9));
    rig.send(press(3.99, 60)); // the old key: the chord is on D4 now
    rig.run_until(4.1);
    CHECK(rig.summary().wrong == 1);
    CHECK(rig.summary().counts.played == 0);
    rig.send(press(4.2, 62));
    rig.run_until(4.4);
    CHECK(rig.summary().counts.played == 1);
}

TEST_CASE("wait: a press still queued when a seek lands is not marked wrong") {
    WaitRig rig(song_of({{3.0, 60}, {8.0, 64}}));
    rig.play_at(1.0);
    rig.send(press(3.99, 60));
    rig.run_until(3.99);
    rig.transport.seek(sec(3.995), 6.0); // the chord the press was for is now behind the position
    for (const double t : {4.00, 4.01, 4.1}) {
        rig.run_until(t);
    }
    CHECK(rig.summary().wrong == 0);
    CHECK(rig.summary().counts.played == 1);
    CHECK(rig.eval.waiting_for_s() == 8.0);
}

TEST_CASE("wait: a press still queued at a loop wrap belongs to the lap it was played in") {
    // Chords at 0 and 1 s, loop 0..2 s. A wrong key is pressed in the last frame before the wrap:
    // lap one's, not the next lap's, so lap two can still be clean.
    const auto make = [](WaitRig& rig) {
        REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
        rig.transport.set_loop_enabled(0, true);
        rig.begin(1.1);
        rig.send(press(1.1, 60), release(1.2, 60));
        rig.run_until(2.2);
        rig.send(press(2.2, 62), release(2.3, 62));
        rig.run_until(2.4);
    };
    const Song song = song_of({{0.0, 60}, {1.0, 62}});
    int steps = 0; // frames until the wrap
    {
        WaitRig probe(song);
        make(probe);
        for (; probe.transport.loop_count() == 0; ++steps) {
            probe.step();
        }
    }
    REQUIRE(steps > 1);
    WaitRig rig(song);
    make(rig);
    for (int i = 0; i < steps - 1; ++i) {
        rig.step();
    }
    REQUIRE(rig.transport.loop_count() == 0);
    MidiEvent stray = press(0.0, 61); // wrong, stamped at this frame, before the wrap
    stray.time_ns = rig.now;
    rig.eval.apply(stray);
    rig.step(); // the wrap lands with the stray press still queued
    REQUIRE(rig.transport.loop_count() == 1);
    for (int i = 0; i < 6; ++i) {
        rig.step();
    }
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    REQUIRE(!passes.empty());
    CHECK(passes[0].wrong == 1);
    CHECK_FALSE(passes[0].clean);
    CHECK(rig.summary().wrong == 1);
    CHECK(rig.eval.waiting_for_s() == 0.0); // lap two waits from the loop start again
}

TEST_CASE("flow: a press still queued at a loop wrap belongs to the lap it was played in") {
    // Notes at 0.5 and 1.5 s, loop 0..2 s, play from wall 1 s: the wrap is at wall 3 s. A wrong key
    // is stamped in the last frame before it and applied with the wrap: lap one's, so lap two is
    // clean.
    FlowRig rig(song_of({{0.5, 60}, {1.5, 62}}));
    REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
    rig.transport.set_loop_enabled(0, true);
    rig.play_at(1.0);
    rig.send(press(1.5, 60), press(2.5, 62));
    rig.run_until(2.99);
    REQUIRE(rig.transport.loop_count() == 0);
    MidiEvent stray = press(0.0, 61);
    stray.time_ns = rig.now;
    rig.eval.apply(stray);
    rig.send(press(3.5, 60), press(4.5, 62)); // lap two, played right
    rig.run_until(5.6);
    REQUIRE(rig.transport.loop_count() >= 1);
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    REQUIRE(passes.size() >= 2);
    CHECK(passes[0].wrong == 1);
    CHECK_FALSE(passes[0].clean);
    CHECK(passes[1].wrong == 0);
    CHECK(passes[1].clean);
}

TEST_CASE("wait: a press still queued when the song switches is not judged against the new song") {
    const Song other = song_of({{3.0, 64}, {6.0, 65}});
    WaitRig rig(song_of({{3.0, 60}}));
    rig.begin(3.99);
    rig.eval.apply(press(3.99, 60)); // right for the old song, still inside the settle window
    CHECK(rig.eval.press_pending(60));
    rig.song = other;
    rig.eval.set_song(other, WaitOptions{});
    CHECK_FALSE(rig.eval.press_pending(60));
    rig.run_until(4.00);
    rig.run_until(4.1);
    CHECK(rig.summary().wrong == 0);
    CHECK_FALSE(rig.eval.press_looks_wrong(60));
}

// Song changes mid-play, lost key-ups, early presses, tied strikes.

TEST_CASE("flow and wait: a song change through the Transport grades from the next play") {
    const Song next = song_of({{1.0, 64}, {3.0, 65}});
    FlowRig flow_rig(song_of({{1.0, 60}, {5.0, 62}}));
    flow_rig.begin(2.5);
    flow_rig.transport.set_song(next);
    flow_rig.song = next;
    flow_rig.eval.set_song(next, FlowOptions{});
    flow_rig.run_until(3.0);
    CHECK_FALSE(flow_rig.transport.playing());
    flow_rig.play_at(3.0);
    flow_rig.send(press(4.0, 64));
    flow_rig.run_until(7.0);
    CHECK(hits(flow_rig.summary()) == 1);
    CHECK(flow_rig.summary().wrong == 0);

    WaitRig rig(song_of({{1.0, 60}, {5.0, 62}}));
    rig.begin(2.5); // held at 1.0
    rig.transport.set_song(next);
    rig.song = next;
    rig.eval.set_song(next, WaitOptions{});
    rig.run_until(3.0);
    rig.play_at(3.0);
    rig.run_until(4.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 1.0);
    CHECK(rig.eval.waiting_for_s() == 1.0);
    rig.send(press(4.5, 64));
    rig.run_until(4.6);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.summary().counts.played == 1);
}

TEST_CASE("wait: a lost key-up does not swallow the next strike of that key") {
    WaitRig rig(song_of({{0.5, 60}, {1.0, 60}, {3.0, 72}}));
    rig.play_at(1.0);
    rig.send(press(1.5, 60)); // plays the chord at 0.5; its note-off never comes
    rig.run_until(2.5);
    CHECK(rig.transport.held());
    CHECK(rig.eval.waiting_for_s() == 1.0);
    rig.send(press(2.5, 60)); // a note-on for a key already down is a new strike
    rig.run_until(2.6);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.summary().counts.played == 2);
    CHECK(rig.summary().wrong == 0);
}

TEST_CASE("wait: release_all lets go of every key, for a lost connection") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}));
    rig.begin(2.5);
    rig.send(press(2.5, 60));
    rig.run_until(2.6);
    rig.eval.release_all(sec(2.6)); // the keyboard was unplugged with 60 down
    rig.send(press(2.7, 64));
    rig.run_until(2.8);
    CHECK(rig.transport.held()); // 60 is no longer down
    rig.send(press(2.8, 60));
    rig.run_until(2.9);
    CHECK_FALSE(rig.transport.held());
}

TEST_CASE("wait: a press while paused or before play never plays a chord") {
    WaitRig paused(song_of({{1.0, 60}, {3.0, 72}}));
    paused.begin(1.9); // song 0.9
    paused.transport.pause(sec(1.9));
    paused.send(press(1.91, 60));
    paused.run_until(2.5);
    paused.play_at(2.5);
    paused.run_until(3.0);
    CHECK(paused.transport.held());
    CHECK(paused.transport.position_s() == 1.0);
    CHECK(paused.eval.waiting_for_s() == 1.0);
    CHECK(paused.summary().counts.played == 0);

    WaitRig early(song_of({{0.0, 60}, {2.0, 72}}));
    early.send(press(0.5, 60)); // no play yet
    early.run_until(0.9);
    early.begin(1.5);
    CHECK(early.transport.held()); // within a frame of the start
    CHECK(early.eval.waiting_for_s() == 0.0);
    CHECK(early.summary().counts.played == 0);
}

TEST_CASE("flow and wait agree on one key struck for two notes 21 ms apart") {
    const Song close = song_of({{1.000, 60, Hand::Right}, {1.021, 60, Hand::Left}, {3.0, 72}});
    const EvaluationSummary s = flow(close, {press(2.0105, 60), press(4.0, 72)});
    CHECK(hits(s) == 3);
    CHECK(s.counts.missed == 0);
    WaitRig rig(close);
    rig.begin(2.5);
    rig.send(press(2.5, 60));
    rig.run_until(3.0);
    CHECK(rig.summary().counts.played == 2);
    CHECK(rig.eval.waiting_for_s() == 3.0);
}

TEST_CASE("flow and wait agree that one key 40 ms apart needs two strikes") {
    const Song apart = song_of({{1.000, 60}, {1.040, 60}, {3.0, 72}});
    const EvaluationSummary s = flow(apart, {press(2.02, 60)});
    CHECK(hits(s) == 1);
    CHECK(s.counts.missed == 2);
    WaitRig rig(apart);
    rig.begin(2.5);
    rig.send(press(2.5, 60));
    rig.run_until(3.0);
    CHECK(rig.summary().counts.played == 1);
    CHECK(rig.eval.waiting_for_s() == doctest::Approx(1.040));
}

TEST_CASE("wait: a key of a later chord pressed early is not wrong") {
    const Song song = song_of({{1.0, 60}, {3.0, 64}, {5.0, 72}});
    WaitRig rig(song);
    rig.begin(2.5); // held at 1.0
    rig.send(press(2.5, 64), release(2.6, 64));
    rig.run_until(2.7);
    CHECK(rig.summary().wrong == 0);
    CHECK(rig.transport.held()); // and it plays nothing
    CHECK(rig.eval.waiting_for_s() == 1.0);
    // Past the lookahead the later chord is a wrong key.
    WaitRig far(song, {.lookahead_s = 1.5});
    far.begin(2.5);
    far.send(press(2.5, 64));
    far.run_until(2.7);
    CHECK(far.summary().wrong == 1);
}

// Tables made ahead (FlowEvaluator::prepare).

// Plays `played` into a fresh Flow evaluator that `setup` has set up, and returns what it graded.
template <typename Setup> EvaluationSummary play_flow(const PlayedSong& played, Setup&& setup) {
    FlowRig rig(played.song);
    setup(rig.eval);
    rig.play_at(1.0);
    rig.inbox = played.presses;
    rig.run_until(1.0 + played.song.duration_s);
    rig.eval.finish();
    return rig.summary();
}

// What two runs must share to have graded the same.
std::vector<int> graded_as(const EvaluationSummary& s) {
    const auto& c = s.counts;
    std::vector<int> out{c.perfect, c.good,          c.early,
                         c.late,    c.missed,        c.played,
                         s.wrong,   s.longest_combo, static_cast<int>(s.notes.size())};
    for (const NoteResult& n : s.notes) {
        out.insert(out.end(),
                   {static_cast<int>(n.note_index), static_cast<int>(n.grade), n.expected_pitch});
    }
    return out;
}

TEST_CASE("flow: a song prepared ahead, even on another thread, grades exactly like a plain one") {
    const PlayedSong played = played_song();
    const auto expected = graded_as(play_flow(played, [](FlowEvaluator&) {}));
    FlowEvaluator::Prepared prepared;
    std::thread maker(
        [&] { prepared = FlowEvaluator::prepare(played.song, HandFilter::Both, Transposition{}); });
    maker.join();
    CHECK(prepared.note_count() == played.song.notes.size());
    const EvaluationSummary got = play_flow(played, [&](FlowEvaluator& e) {
        e.set_song(played.song, FlowOptions{}, std::move(prepared));
    });
    CHECK(got.notes.size() > 100);
    CHECK(graded_as(got) == expected);
}

TEST_CASE("flow: tables made for other hands, another key or another song are not used") {
    const PlayedSong played = played_song();
    const auto expected = graded_as(play_flow(played, [](FlowEvaluator&) {}));
    Song shifted = played.song; // same number of notes and bars: the content decides, not the size
    for (Note& n : shifted.notes) {
        n.pitch = static_cast<std::uint8_t>(n.pitch + 1);
    }
    const auto with = [&](FlowEvaluator::Prepared tables) {
        return graded_as(play_flow(played, [&](FlowEvaluator& e) {
            e.set_song(played.song, FlowOptions{}, std::move(tables));
        }));
    };
    CHECK(with(FlowEvaluator::prepare(played.song, HandFilter::Left, Transposition{})) == expected);
    CHECK(with(FlowEvaluator::prepare(played.song, HandFilter::Both,
                                      Transposition(2, KeyRange{36, 96}))) == expected);
    CHECK(with(FlowEvaluator::prepare(song_of({{1.0, 60}, {2.0, 62}}), HandFilter::Both,
                                      Transposition{})) == expected);
    CHECK(with(FlowEvaluator::prepare(shifted, HandFilter::Both, Transposition{})) == expected);

    // And the key the evaluator follows decides, as in set_song without tables.
    TranspositionSource source;
    source.set(Transposition(2, KeyRange{36, 96}), 0);
    const auto following = [&](bool prepared) {
        return graded_as(play_flow(played, [&](FlowEvaluator& e) {
            e.follow(&source);
            prepared
                ? e.set_song(played.song, FlowOptions{},
                             FlowEvaluator::prepare(played.song, HandFilter::Both, Transposition{}))
                : e.set_song(played.song, FlowOptions{});
        }));
    };
    CHECK(following(true) == following(false));
}

// Properties.

// A random song: single notes, chords of 2 to 4 keys, quick repeats of one key, trills, and both
// hands on one key at once. Two notes on one key either start together or at least 60 ms apart,
// as a real player needs to strike a key twice.
std::vector<N> random_song(std::mt19937& rng, int groups) {
    std::uniform_real_distribution<double> gap(0.05, 0.4);
    std::uniform_real_distribution<double> dur(0.05, 0.5);
    std::uniform_int_distribution<int> pitch(40, 90);
    std::uniform_int_distribution<int> pattern(0, 4);
    std::uniform_int_distribution<int> hand(0, 2);
    std::uniform_int_distribution<int> size(2, 4);
    const Hand hands[] = {Hand::Left, Hand::Right, Hand::Unknown};
    std::vector<N> out;
    std::array<double, 128> last;
    last.fill(-1.0);
    const auto add = [&](double t, int p, Hand h) {
        double& at = last[static_cast<std::size_t>(p)];
        if (at >= 0.0 && t != at && t - at < 0.060) {
            return;
        }
        at = t;
        out.push_back({t, p, h, dur(rng)});
    };
    double t = 0.5;
    for (int g = 0; g < groups; ++g, t += gap(rng)) {
        const Hand h = hands[hand(rng)];
        const int p = pitch(rng);
        switch (pattern(rng)) {
        case 0:
            add(t, p, h);
            break;
        case 1:
            for (int i = 0, n = size(rng); i < n; ++i) {
                add(t, std::min(127, p + i * 4), h);
            }
            break;
        case 2: // quick repeats
            add(t, p, h);
            add(t + 0.08, p, h);
            add(t + 0.16, p, h);
            t += 0.16;
            break;
        case 3: // trill
            for (int i = 0; i < 6; ++i) {
                add(t + i * 0.07, i % 2 == 0 ? p : p + 2, h);
            }
            t += 0.35;
            break;
        case 4: // both hands on one key
            add(t, p, Hand::Left);
            add(t, p, Hand::Right);
            break;
        }
    }
    return out;
}

TEST_CASE("property: a perfect player on random songs gets every note Perfect and no wrong") {
    for (unsigned seed = 1; seed <= 40; ++seed) {
        CAPTURE(seed);
        std::mt19937 rng(seed);
        const std::vector<N> notes = random_song(rng, 60);
        const int speed = std::array{25, 50, 75, 100, 125, 150}[seed % 6];
        const int semitones = static_cast<int>(seed % 25) - 12;
        // Pitches 40..102 shifted by up to an octave stay inside 0..127, so nothing folds.
        FlowRig rig(song_of(notes), {.transposition = Transposition(semitones, KeyRange{0, 127})});
        rig.play_at(1.0, speed);
        // One press per key and start, inside the Perfect window (28 ms) with room for rounding,
        // chords struck in random order.
        std::uniform_real_distribution<double> jitter(-0.025, 0.025);
        std::vector<MidiEvent> strikes;
        std::vector<std::pair<double, int>> seen;
        for (const N& n : notes) {
            if (std::find(seen.begin(), seen.end(), std::pair{n.start, n.pitch}) == seen.end()) {
                seen.push_back({n.start, n.pitch});
                strikes.push_back(
                    press(1.0 + n.start * 100.0 / speed + jitter(rng), n.pitch + semitones));
            }
        }
        std::shuffle(strikes.begin(), strikes.end(), rng);
        rig.inbox = strikes;
        rig.run_until(1.0 + rig.song.duration_s * 100.0 / speed + 1.0);
        rig.eval.finish();
        const EvaluationSummary& s = rig.summary();
        CHECK(s.notes.size() == notes.size());
        CHECK(s.counts.perfect == static_cast<int>(notes.size()));
        CHECK(s.wrong == 0);
        CHECK(s.longest_combo == static_cast<int>(notes.size()));
        CHECK(rig.eval.pending_notes() == 0);
        CHECK(rig.eval.pending_presses() == 0);
    }
}

TEST_CASE("property: a Wait Mode player who plays each chord when it stops never gets stuck") {
    for (unsigned seed = 1; seed <= 15; ++seed) {
        CAPTURE(seed);
        std::mt19937 rng(seed * 104729u);
        const std::vector<N> notes = random_song(rng, 30);
        WaitRig rig(song_of(notes));
        rig.play_at(1.0, 50 + static_cast<int>(seed % 3) * 50);
        std::vector<int> down;
        std::uniform_real_distribution<double> roll(0.0, 0.040);
        std::bernoulli_distribution lose(0.3); // a key-up lost on the way
        std::bernoulli_distribution peek(0.3); // a key of the next chord pressed early
        double struck = -1.0;                  // the chord already struck
        for (int guard = 0; !rig.transport.ended() && guard < 20000; ++guard) {
            rig.step();
            if (rig.transport.held() && rig.eval.waiting_for_s() != struck) {
                struck = rig.eval.waiting_for_s();
                const double t = static_cast<double>(rig.now) * 1e-9 + 0.010;
                for (int key : down) {
                    if (!lose(rng)) {
                        rig.send(release(t, key));
                    }
                }
                down = keys_of(rig.eval.waiting_keys());
                std::shuffle(down.begin(), down.end(), rng);
                for (int key : down) {
                    rig.send(press(t + 0.001 + roll(rng), key));
                }
                // The first key of the chord after this one, struck early and let go (or not).
                const auto later = std::find_if(notes.begin(), notes.end(), [&](const N& n) {
                    return n.start > struck + kChordSeconds + 1e-9;
                });
                if (later != notes.end() && peek(rng)) {
                    rig.send(press(t + 0.0005, later->pitch));
                    if (!lose(rng)) {
                        rig.send(release(t + 0.0008, later->pitch));
                    }
                }
            }
        }
        CHECK(rig.transport.ended());
        CHECK(rig.summary().counts.played == static_cast<int>(notes.size()));
        CHECK(rig.summary().wrong == 0);
    }
}

// Run records.

EvaluationSummary sample_summary() {
    EvaluationSummary s;
    s.counts = {.perfect = 30, .good = 8, .early = 2, .late = 3, .missed = 5, .played = 0};
    s.wrong = 4;
    s.unplayable = 1;
    s.combo = 7;
    s.longest_combo = 21;
    s.left = {.graded = 20, .hit = 18};
    s.right = {.graded = 28, .hit = 25};
    s.all = {.graded = 48, .hit = 43};
    s.measures = {{.measure = 1, .graded = 6, .missed = 0, .wrong = 0},
                  {.measure = 2, .graded = 6, .missed = 3, .wrong = 2},
                  {.measure = 4, .graded = 8, .missed = 1, .wrong = 0}};
    return s;
}

RunRecord sample_record() {
    RunContext c;
    c.song_hash = 0x0102030405060708ULL;
    c.song_title = "Für Elise";
    c.mode = RunMode::Flow;
    c.hands = HandFilter::Right;
    c.speed_percent = 80;
    c.transpose_semitones = -2;
    c.start_utc_s = 1'760'000'000;
    c.end_utc_s = 1'760'000'300;
    c.local_day = 20'373;
    c.duration_ms = 287'500;
    return make_run_record(sample_summary(), c);
}

TEST_CASE("run record: sanitized forces every field into its documented range") {
    RunRecord r = sample_record();
    r.speed_percent = 999;
    r.transpose_semitones = -40;
    r.duration_ms = -5;
    r.local_day = 2'000'000'000;
    r.wrong = -3;
    r.left.hit = 99; // more hits than graded notes
    r.all.graded = -1;
    r.counts.perfect = 2'000'000'000;
    const RunRecord s = sanitized(r);
    CHECK(s.speed_percent == 200);
    CHECK(s.transpose_semitones == -12);
    CHECK(s.duration_ms == 0);
    CHECK(s.local_day == kMaxRunDay);
    CHECK(s.wrong == 0);
    CHECK(s.left.hit == s.left.graded);
    CHECK(s.all.graded == 0);
    CHECK(s.counts.perfect == kMaxRunCount);
    CHECK(sanitized(s) == s);

    r = sample_record();
    const std::pair<int, int> speeds[] = {{83, 83}, {5000, 200}, {3, 25}}; // a whole percent
    for (const auto& [in, out] : speeds) {
        r.speed_percent = in;
        CHECK(sanitized(r).speed_percent == out);
    }
    r.duration_ms = kMaxRunDurationMs + 1;
    CHECK(sanitized(r).duration_ms == kMaxRunDurationMs);
}

TEST_CASE("run record: serialization round-trips a record exactly") {
    const RunRecord r = sample_record();
    RunRecord back;
    REQUIRE(parse_run_record(serialize_run_record(r), back) == RunParseStatus::Ok);
    CHECK(back == r);
}

TEST_CASE("run record: the version 1 layout is fixed") {
    // An independent description of the bytes, written from the format comment in run_record.hpp,
    // so a change of layout cannot pass by editing the serializer alone.
    std::string expect = "TR";
    expect += '\x01';
    expect += std::string(8, '\0'); // song hash
    expect += '\0';                 // title length
    expect += '\0';                 // mode Flow
    expect += '\0';                 // hands Both
    expect += '\x64';               // speed 100
    expect += std::string(5, '\0'); // transpose, start, end-start, day, duration
    expect += std::string(6, '\0'); // grade counts
    expect += std::string(4, '\0'); // wrong, unplayable, longest combo, clean passes
    expect += std::string(8, '\0'); // left, right, unknown, all
    expect += '\0';                 // bar count
    CHECK(serialize_run_record(RunRecord{}) == expect);
    CHECK(expect.size() == 39);

    RunRecord r;
    r.song_hash = 0x0102030405060708ULL;
    r.song_title = "ab";
    r.mode = RunMode::Wait;
    r.hands = HandFilter::Right;
    r.speed_percent = 150;
    r.transpose_semitones = -3; // zigzag 5
    r.start_utc_s = 300;        // zigzag 600: D8 04
    r.end_utc_s = 301;          // 1 after the start
    r.local_day = -1;           // zigzag 1
    r.duration_ms = 300;        // varint: AC 02
    r.counts.perfect = 128;     // varint: 80 01
    r.all = {.graded = 5, .hit = 4};
    r.measures = {{.measure = 10, .graded = 3, .missed = 1, .wrong = 0},
                  {.measure = 12, .graded = 3, .missed = 0, .wrong = 2}};
    std::string want = "TR";
    want += '\x01';
    want += "\x08\x07\x06\x05\x04\x03\x02\x01";
    want += '\x02';
    want += "ab";
    want += '\x01';     // Wait
    want += '\x02';     // Right
    want += "\x96\x01"; // 150
    want += '\x05';
    want += "\xD8\x04";
    want += '\x01';
    want += '\x01';
    want += "\xAC\x02";
    want += "\x80\x01";               // perfect
    want += std::string(5 + 4, '\0'); // good early late missed played, wrong unplayable combo clean
    want += std::string(6, '\0');     // left, right, unknown
    want += '\x05';                   // all.graded
    want += '\x04';                   // all.hit
    want += '\x02';                   // two bars
    want += std::string("\x0A\x03\x01\x00", 4); // bar 10: graded 3, missed 1, wrong 0
    want += std::string("\x02\x03\x00\x02", 4); // bar 12 as +2: graded 3, missed 0, wrong 2
    CHECK(serialize_run_record(r) == want);
}

TEST_CASE("run record: parse rejects what is not a record and leaves the output alone") {
    const RunRecord keep = sample_record();
    RunRecord out = keep;
    CHECK(parse_run_record("", out) == RunParseStatus::Truncated);
    CHECK(parse_run_record("T", out) == RunParseStatus::Truncated);
    CHECK(parse_run_record("XR" + std::string(40, 'x'), out) == RunParseStatus::BadMagic);
    std::string bytes = serialize_run_record(keep);
    bytes[2] = '\x02';
    CHECK(parse_run_record(bytes, out) == RunParseStatus::NewerVersion);
    bytes[2] = '\x00';
    CHECK(parse_run_record(bytes, out) == RunParseStatus::Corrupt);
    CHECK(parse_run_record(serialize_run_record(keep) + "x", out) == RunParseStatus::Corrupt);
    CHECK(out == keep);
}

TEST_CASE("run record: every strict prefix is rejected; bad enums and overlong numbers too") {
    const std::string good = serialize_run_record(sample_record());
    RunRecord out;
    for (std::size_t n = 0; n < good.size(); ++n) {
        CAPTURE(n);
        CHECK(parse_run_record(std::string_view(good).substr(0, n), out) != RunParseStatus::Ok);
    }
    const std::size_t mode_at = 3 + 8 + 1 + std::string("Für Elise").size();
    std::string bytes = good;
    bytes[mode_at] = '\x07';
    CHECK(parse_run_record(bytes, out) == RunParseStatus::Corrupt);
    bytes = good;
    bytes[mode_at + 1] = '\x09';
    CHECK(parse_run_record(bytes, out) == RunParseStatus::Corrupt);
    // A varint of 11 continuation bytes where the speed goes.
    bytes = good.substr(0, mode_at + 2) + std::string(11, '\x80') + good.substr(mode_at + 3);
    CHECK(parse_run_record(bytes, out) != RunParseStatus::Ok);
    // A title length that runs past the end.
    bytes = good;
    bytes[11] = '\x7f';
    CHECK(parse_run_record(bytes, out) != RunParseStatus::Ok);
    // A claim of millions of bars with no bytes behind it does not allocate or loop.
    bytes = serialize_run_record(RunRecord{});
    bytes.back() = '\xff';
    bytes += "\xff\xff\xff\x7f";
    CHECK(parse_run_record(bytes, out) == RunParseStatus::Corrupt);
}

// The old runs.dat, which only the one-time move into history.db reads: framing, damage and
// records from a newer build.

RunRecord sample_run(int n) {
    RunRecord r;
    r.song_hash = 0xabc0 + static_cast<std::uint64_t>(n);
    r.song_title = "Song " + std::to_string(n);
    r.mode = n % 2 == 0 ? RunMode::Flow : RunMode::Wait;
    r.hands = HandFilter::Right;
    r.speed_percent = 80;
    r.start_utc_s = 1'760'000'000 + n * 100;
    r.end_utc_s = r.start_utc_s + 60;
    r.local_day = 20373;
    r.duration_ms = 55'000;
    r.counts.perfect = 10 + n;
    r.counts.missed = 2;
    r.right = {12 + n, 10 + n};
    r.all = r.right;
    r.longest_combo = 7;
    r.measures = {{1, 4, 1, 0}, {2, 8, 1, 1}};
    return sanitized(r);
}

std::vector<std::string> raw_runs(int count) {
    std::vector<std::string> raw;
    for (int i = 0; i < count; ++i) {
        raw.push_back(serialize_run_record(sample_run(i)));
    }
    return raw;
}

TEST_CASE("runs file: a record's check catches a flipped byte, the rest are read") {
    const std::vector<std::string> raw = raw_runs(3);
    std::string bytes = encode_runs(raw);
    const std::size_t second = kRunsHeader.size() + 8 + raw[0].size() + 8 + 5;
    bytes[second] = static_cast<char>(bytes[second] ^ 0x5A);
    const RunsDecode d = decode_runs(bytes);
    CHECK(d.damaged == 1);
    REQUIRE(d.runs.size() == 2);
    CHECK(d.runs[0] == sample_run(0));
    CHECK(d.runs[1] == sample_run(2));
}

TEST_CASE("runs file: input that is not a runs file is flagged and gives no runs") {
    const RunsDecode d = decode_runs("garbage, not runs");
    CHECK(d.bad_header);
    CHECK(d.runs.empty());
    CHECK_FALSE(decode_runs("").bad_header); // nothing at all is an empty history
}

TEST_CASE("runs file: a file cut off inside a record keeps every whole record before it") {
    const std::vector<std::string> raw = raw_runs(4);
    const std::string bytes = encode_runs(raw);
    std::vector<std::size_t> boundaries{kRunsHeader.size()};
    for (const std::string& r : raw) {
        boundaries.push_back(boundaries.back() + 8 + r.size());
    }
    for (std::size_t cut = kRunsHeader.size(); cut < bytes.size(); ++cut) {
        const RunsDecode d = decode_runs(std::string_view(bytes).substr(0, cut));
        CHECK_FALSE(d.bad_header);
        CHECK(d.runs.size() <= 4);
        for (std::size_t i = 0; i < d.runs.size(); ++i) {
            CHECK(d.runs[i] == sample_run(static_cast<int>(i)));
        }
        // Exactly at a record boundary nothing is cut; anywhere else the tail is.
        CHECK(d.truncated ==
              (std::find(boundaries.begin(), boundaries.end(), cut) == boundaries.end()));
    }
}

TEST_CASE("runs file: records from a newer build are kept byte for byte") {
    std::string future = serialize_run_record(sample_run(0));
    future[2] = 9; // version byte
    const std::vector<std::string> raw{serialize_run_record(sample_run(1)), future};
    const RunsDecode d = decode_runs(encode_runs(raw));
    CHECK(d.runs.size() == 1);
    CHECK(d.newer == 1);
    CHECK(d.damaged == 0);
    CHECK(d.kept == raw);
}

// RunRecord <-> the flat columns a database row holds.

TEST_CASE("run columns: every field survives the round trip; an invalid enum row is refused") {
    const RunRecord r{.song_hash = 0xfedcba9876543210ULL, // high bit set: negative as a column
                      .song_title = "Gymnopédie No. 1",
                      .mode = RunMode::Wait,
                      .hands = HandFilter::Left,
                      .speed_percent = 75,
                      .transpose_semitones = -3,
                      .start_utc_s = 1'760'000'000,
                      .end_utc_s = 1'760'000'090,
                      .local_day = 20373,
                      .duration_ms = 88'000,
                      .counts = {10, 20, 3, 4, 5, 6},
                      .wrong = 7,
                      .unplayable = 8,
                      .longest_combo = 9,
                      .clean_passes = 2,
                      .left = {30, 25},
                      .right = {12, 11},
                      .unknown = {3, 1},
                      .all = {45, 37},
                      .measures = {{0, 2, 1, 0}, {1, 8, 2, 1}, {5, 6, 0, 3}}};
    REQUIRE(sanitized(r) == r); // the fixture is in range
    const std::optional<RunRecord> back = from_columns(to_columns(r));
    REQUIRE(back.has_value());
    CHECK(*back == r);
    const std::pair<RunCol, int> bad[] = {
        {RunCol::Mode, 2}, {RunCol::Hands, 3}, {RunCol::Hands, -1}};
    for (const auto& [col, value] : bad) {
        RunColumns c = to_columns(r);
        c[col] = value;
        CHECK_FALSE(from_columns(c).has_value());
    }
}

TEST_CASE("song practice: sanitized forces every field into its range") {
    SongPractice p;
    p.speed_percent = 5000;
    p.count_in_bars = -3;
    p.hands = static_cast<HandFilter>(9);
    p.transpose_semitones = -40;
    const SongPractice c = sanitized(p);
    CHECK(c.speed_percent == kMaxSpeedPercent);
    CHECK(c.count_in_bars == 0);
    CHECK(c.hands == HandFilter::Both);
    CHECK(c.transpose_semitones == -kMaxTransposeSemitones);
    CHECK(sanitized(c) == c);

    SongPractice q;
    q.speed_percent = 1;
    q.count_in_bars = 99;
    q.hands = HandFilter::Right;
    q.transpose_semitones = 99;
    const SongPractice d = sanitized(q);
    CHECK(d.speed_percent == kMinSpeedPercent);
    CHECK(d.count_in_bars == kMaxCountInBars);
    CHECK(d.hands == HandFilter::Right);
    CHECK(d.transpose_semitones == kMaxTransposeSemitones);
}

// A song's speed, count-in, hands and transposition in the history: restored when it is opened,
// kept per song across a switch and a restart, applied late when the history had not loaded, the
// player's change winning over a late arrival.

namespace fs = std::filesystem;
using namespace std::chrono_literals;

struct TempDir {
    fs::path path =
        fs::temp_directory_path() / ("etude-practice-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// What a session of the app has: a store, a controller and the sync between them, on one clock.
struct SyncRig {
    HistoryStore history;
    SongController songs;
    SongPracticeSync sync;
    std::int64_t now = kSec;

    static HistoryOptions quiet() {
        HistoryOptions o;
        o.log = [](std::string_view) {};
        o.sync_commits = false;
        return o;
    }

    explicit SyncRig(const fs::path& dir, bool wait_for_history = true)
        : history(dir, quiet()), sync(history, songs) {
        SongOptions o;
        o.default_count_in_bars = 1;
        songs.set_options(o, now);
        history.open();
        if (wait_for_history) {
            history_arrives();
        }
    }
    ~SyncRig() {
        std::vector<SongSoundEvent> out;
        songs.shutdown(now, out);
    }
    void open(std::uint64_t hash, const std::string& path) {
        sync.song_closing();
        LoadedSong song = two_hand_song(2, hash);
        song.library_path = path;
        songs.load(std::move(song), now);
        sync.song_opened(false, now);
    }
    // Frames go by for `ms`, one update each 100 ms.
    void frames(int ms) {
        for (int t = 0; t < ms; t += 100) {
            now += 100 * kMs;
            sync.update(now);
        }
    }
    // The history thread has loaded: the frame that takes it in.
    void history_arrives() {
        REQUIRE(history.wait_until_settled(10s));
        history.poll();
        now += 16 * kMs;
        sync.update(now);
    }
    // Sets the speed, then lets the saving settle.
    void set_speed(int percent) {
        songs.set_speed(percent, now);
        frames(600);
    }
};

TEST_CASE("song practice sync: a song's choices survive a restart") {
    TempDir dir;
    SongPractice changed;
    changed.speed_percent = 70;
    changed.count_in_bars = 2;
    changed.hands = HandFilter::Left;
    changed.transpose_semitones = 3;
    {
        SyncRig rig(dir.path);
        rig.open(0xA1, "a.mid");
        CHECK(rig.songs.practice() == SongPractice{});
        rig.songs.set_speed(70, rig.now);
        rig.songs.set_count_in_bars(2);
        rig.songs.set_practice_hands(HandFilter::Left, rig.now);
        rig.songs.set_transpose(3, rig.now);
        rig.frames(600);
        REQUIRE(rig.history.flush());
        CHECK(rig.history.song_practice().at(0xA1) == changed);
    }
    SyncRig again(dir.path);
    again.open(0xA1, "a.mid");
    CHECK(again.songs.practice() == changed);
}

TEST_CASE("song practice sync: a section chip's loop is for that open and is never saved") {
    TempDir dir;
    {
        SyncRig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.set_speed(70);
        const int saves = rig.sync.saves();
        rig.sync.open_with_loop(0xA1, 2.0, 4.0, 0);
        rig.open(0xA1, "a.mid");
        REQUIRE(rig.songs.practice().loop_enabled);
        rig.frames(600);
        CHECK(rig.sync.saves() == saves); // the loop alone writes nothing
        rig.set_speed(80); // a change of the player's on top of it is saved, loop-free
        REQUIRE(rig.history.flush());
        const SongPractice row = rig.history.song_practice().at(0xA1);
        CHECK(row.speed_percent == 80);
        CHECK_FALSE(row.loop_enabled);
        CHECK(row.loop_end_s == 0.0);
        rig.open(0xA1, "a.mid"); // a plain open afterwards does not loop
        CHECK_FALSE(rig.songs.practice().has_loop());
        CHECK(rig.songs.speed_percent() == 80);
    }
    SyncRig again(dir.path);
    again.open(0xA1, "a.mid");
    CHECK_FALSE(again.songs.practice().has_loop());
    CHECK(again.songs.speed_percent() == 80);
}

TEST_CASE("song practice sync: a renamed or moved file keeps its choices (the hash is the key)") {
    TempDir dir;
    SyncRig rig(dir.path);
    rig.open(0xA1, "old/name.mid");
    rig.set_speed(60);
    rig.open(0xB2, "b.mid");
    rig.open(0xA1, "elsewhere/new name.mid");
    CHECK(rig.songs.speed_percent() == 60);
}

TEST_CASE("song practice sync: nothing is written while the history is still loading") {
    TempDir dir;
    SyncRig late(dir.path, false);
    late.open(0xA1, "a.mid");
    late.songs.set_speed(120, late.now);
    late.frames(1000);
    CHECK(late.history.song_practice().count(0xA1) == 0);
    late.history_arrives(); // no row for it: the player's change is kept and then saved
    CHECK(late.songs.speed_percent() == 120);
    late.frames(600);
    CHECK(late.history.song_practice().at(0xA1).speed_percent == 120);
}

TEST_CASE("song practice sync: the options of this run are not saved") {
    TempDir dir;
    {
        SyncRig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.set_speed(66);
        REQUIRE(rig.history.flush());
    }
    SyncRig rig(dir.path);
    rig.open(0xA1, "a.mid");
    rig.songs.set_speed(30, rig.now); // --speed 30
    rig.sync.adopt();
    rig.frames(1000);
    CHECK(rig.history.song_practice().at(0xA1).speed_percent == 66);
    rig.set_speed(35); // a later change is
    CHECK(rig.history.song_practice().at(0xA1).speed_percent == 35);
}

TEST_CASE("song practice sync: a song without a hash is left alone") {
    TempDir dir;
    SyncRig rig(dir.path);
    rig.open(0, "nohash.mid");
    rig.songs.set_speed(77, rig.now);
    rig.frames(1000);
    CHECK(rig.history.song_practice().empty());
}

// The song controller: presses stamped before a seek, a transposition or a song switch, and the
// frame's events handed over with a loop wrap.

TEST_CASE("host: a press stamped before a seek, a transposition or a song switch is not wrong") {
    struct Row {
        std::vector<std::pair<double, int>> song; // a chord at 1 s, the next far on
        std::vector<std::pair<int, int>> presses; // {key, ms before the change}
        std::function<void(SongController&, std::int64_t)> change;
        int played; // what the waiting chord has by the end
    };
    const Row rows[] = {
        {{{1.0, 60}, {8.0, 64}},
         {{60, 6}},
         [](SongController& c, std::int64_t now) { c.seek(7.0, now); },
         1},
        {{{1.0, 60}, {1.0, 64}, {8.0, 67}},
         {{60, 14}, {64, 6}},
         [](SongController& c, std::int64_t now) { c.set_transpose(2, now); },
         2},
        {{{1.0, 60}, {8.0, 62}},
         {{60, 6}},
         [](SongController& c, std::int64_t now) {
             c.load(notes_song({{1.0, 64}, {8.0, 65}}, 0x78), now);
         },
         0},
    };
    for (const Row& row : rows) {
        SongController c;
        Host host(c);
        std::int64_t now = kSec;
        c.load(notes_song(row.song), now);
        c.set_mode(PracticeMode::Wait, now);
        c.play(now);
        host.frames(now, now + 2500 * kMs);
        REQUIRE(c.waiting());
        for (const auto& [key, before_ms] : row.presses) {
            host.queue.push_back(note_on(key, now - before_ms * kMs));
        }
        row.change(c, now); // a tap in the frame the presses were drained in
        host.finish_frame(now + 16 * kMs);
        now += 16 * kMs;
        host.frames(now, now + 200 * kMs);
        REQUIRE(c.summary() != nullptr);
        CHECK(c.summary()->wrong == 0);
        CHECK(c.summary()->counts.played == row.played);
        if (row.played == 0) { // the song was switched: the first run is over and kept
            const std::vector<FinishedRun> runs = c.take_finished_runs();
            REQUIRE(runs.size() == 1);
            CHECK(runs[0].record.wrong == 0);
            CHECK(runs[0].record.all.hit >= 1);
        }
    }
}

TEST_CASE(
    "host: a press stamped before a loop wrap, handed over in the wrap's frame, is lap one's") {
    SongController c;
    Host host(c);
    std::int64_t now = kSec;
    c.load(notes_song({{0.0, 60}, {1.0, 62}, {3.0, 65}}), now);
    c.set_mode(PracticeMode::Wait, now);
    REQUIRE(c.set_loop(0.0, 2.0, now));
    c.play(now);
    host.frames(now, now + 100 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(60, now));
    host.frames(now, now + 1500 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(62, now));
    std::vector<LoopPass> passes;
    while (c.position_s() <= 1.984 || c.waiting()) {
        host.frames(now, now + 16 * kMs);
        for (const LoopPass& p : c.take_loop_passes()) {
            passes.push_back(p);
        }
        REQUIRE(now < 20 * kSec);
    }
    REQUIRE(passes.empty()); // the next frame crosses the loop's end
    // In the frame where the loop wraps, update() applies the press with the frame's events
    // before the clock's ops, not after.
    const std::array<MidiEvent, 1> stray{note_on(61, now)}; // wrong, stamped before the wrap
    now += 16 * kMs;
    c.update(now, stray);
    REQUIRE(c.position_s() < 0.5); // wrapped in that frame
    host.frames(now, now + 200 * kMs);
    for (const LoopPass& p : c.take_loop_passes()) {
        passes.push_back(p);
    }
    REQUIRE(!passes.empty());
    CHECK(passes[0].wrong == 1);
    CHECK_FALSE(passes[0].clean);
}

// Which runs are saved, and how a run is scored and kept.

TEST_CASE(
    "a run where nothing was played is not saved; one of only wrong presses is, with no hits") {
    SongController c;
    FakePiano piano;
    std::vector<SongSoundEvent> out;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.play(now);
    run(c, piano, now, now + 6 * kSec);
    CHECK(c.state() == TransportState::Ended);
    CHECK(c.take_finished_runs().empty());
    c.shutdown(now, out);
    CHECK(c.take_finished_runs().empty());

    SongController wrong_run;
    wrong_run.load(two_hand_song(), now);
    wrong_run.play(now);
    run(wrong_run, piano, now, now + 1 * kSec);
    const std::array<MidiEvent, 1> wrong{note_on(61, now)}; // C#4 is in no bar: matches no note
    run(wrong_run, piano, now, now + 16 * kMs, wrong);
    run(wrong_run, piano, now, now + 6 * kSec);
    REQUIRE(wrong_run.state() == TransportState::Ended);
    const auto runs = wrong_run.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].record.wrong >= 1);
    CHECK(runs[0].record.all.hit == 0);
    CHECK(runs[0].record.all.graded > 0);
    wrong_run.shutdown(now, out);
}

// Plays the first `count` right-hand notes of two_hand_song() on time, half a second apart.
void play_notes(SongController& c, FakePiano& piano, std::int64_t& now, int count) {
    for (int i = 0; i < count; ++i) {
        const MidiEvent ev = press_with(72 + (i % 4) * 2, 80, now);
        run(c, piano, now, now + 500 * kMs, std::span(&ev, 1));
    }
    run(c, piano, now, now + 100 * kMs);
}

LoadedSong rated(LoadedSong song, int half_stars) {
    SongDifficulty d;
    d.bars = {3.0f};
    d.numbers = {1};
    d.half_stars = half_stars;
    song.difficulty = std::make_shared<const SongDifficulty>(std::move(d));
    return song;
}

TEST_CASE("a run ended by opening another song is scored with the song it was played on") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    c.load(rated(two_hand_song(), 2), now); // a one-star song, two bars
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    play_notes(c, piano, now, 4);
    const int old_bars = static_cast<int>(c.song().song.measures.size());
    // The next song is a four-star one with other bars; it is loaded after this run ends.
    c.load(rated(two_hand_song(4), 8), now);
    const std::vector<FinishedRun> runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    const RunRecord& r = runs[0].record;
    REQUIRE(r.all.graded > 0);
    RunRecord bare = r;
    bare.score = 0;
    bare.covered_percent = 0;
    ScoreContext old_song;
    old_song.song_notes = runs[0].events.expected_notes;
    old_song.song_bars = old_bars;
    old_song.difficulty_half_stars = 2;
    ScoreContext new_song = old_song;
    new_song.song_bars = 4;
    new_song.difficulty_half_stars = 8;
    CHECK(r.score > 0);
    CHECK(r.score == scored(bare, old_song).score);
    CHECK(r.covered_percent == scored(bare, old_song).covered_percent);
    CHECK(scored(bare, new_song).score != r.score); // the song loaded after would say otherwise
}

TEST_CASE("a run keeps the profile it was played under") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    // Plays three notes of a fresh song and ends the run by opening another; the run's profile.
    const auto finished_run = [&] {
        c.load(two_hand_song(), now);
        c.set_practice_hands(HandFilter::Right, now);
        c.play(now);
        play_notes(c, piano, now, 3);
    };
    CHECK(c.profile_id() == kDefaultProfileId);
    c.set_profile(2, now);
    finished_run();
    c.load(two_hand_song(), now); // ends the run; the profile has not changed, so it is profile 2's
    std::vector<FinishedRun> runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].profile_id == 2);

    // Switching profile in the middle of a run ends it under the old one.
    finished_run();
    c.set_profile(3, now);
    CHECK(c.profile_id() == 3);
    runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].profile_id == 2);

    // And the next run is the new profile's.
    c.set_profile(3, now); // no change: nothing ends
    finished_run();
    c.load(two_hand_song(), now);
    runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].profile_id == 3);
    c.set_profile(0, now); // not a profile: ignored
    CHECK(c.profile_id() == 3);
}

} // namespace
