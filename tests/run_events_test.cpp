// SPDX-License-Identifier: GPL-3.0-or-later
// What a run records note by note (run_events.hpp): the event of a result or a press, the fixed
// buffer and its cap, and when a run counts as one whole pass over the song.
#include "alloc_count.hpp"

#include "etude/run_events.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

using namespace etude;

namespace {

NoteResult hit(std::uint32_t note, double press_song_s, std::int64_t error_ns, int velocity,
               NoteGrade grade, Hand hand = Hand::Right, int pitch = 72) {
    NoteResult r;
    r.note_index = note;
    r.grade = grade;
    r.expected_pitch = pitch;
    r.hand = hand;
    r.song_s = press_song_s - static_cast<double>(error_ns) * 1e-9;
    r.press_song_s = press_song_s;
    r.timing_error_ns = error_ns;
    r.velocity = velocity;
    return r;
}

NoteResult miss(std::uint32_t note, double song_s, int pitch = 72, Hand hand = Hand::Right) {
    NoteResult r;
    r.note_index = note;
    r.grade = NoteGrade::Missed;
    r.expected_pitch = pitch;
    r.hand = hand;
    r.song_s = song_s;
    return r;
}

} // namespace

TEST_CASE("a press that graded a note records its time in song time, key, velocity, note, hand, "
          "grade and signed error") {
    const RunEvent e =
        make_run_event(hit(7, 1.2504, -18'400'000, 88, NoteGrade::Early, Hand::Left, 60));
    CHECK(e.kind == RunEventKind::Press);
    CHECK(e.song_ms == 1250);
    CHECK(e.error_ms == -18); // wall time, negative is early
    CHECK(e.key == 60);
    CHECK(e.velocity == 88);
    CHECK(e.note_index == 7);
    CHECK(e.hand == Hand::Left);
    CHECK(e.grade == NoteGrade::Early);
}

TEST_CASE("the error rounds to the nearest millisecond, away from zero at the half") {
    CHECK(make_run_event(hit(0, 1.0, 1'500'000, 64, NoteGrade::Good)).error_ms == 2);
    CHECK(make_run_event(hit(0, 1.0, -1'500'000, 64, NoteGrade::Good)).error_ms == -2);
    CHECK(make_run_event(hit(0, 1.0, 1'499'999, 64, NoteGrade::Good)).error_ms == 1);
    CHECK(make_run_event(hit(0, 1.0, 0, 64, NoteGrade::Perfect)).error_ms == 0);
}

TEST_CASE("a note nobody played is recorded at its own start, with the key it wanted") {
    const RunEvent e = make_run_event(miss(3, 2.5, 76, Hand::Left));
    CHECK(e.kind == RunEventKind::Missed);
    CHECK(e.song_ms == 2500);
    CHECK(e.key == 76);
    CHECK(e.velocity == 0);
    CHECK(e.note_index == 3);
    CHECK(e.hand == Hand::Left);
    CHECK(e.grade == NoteGrade::Missed);
    CHECK(e.error_ms == 0);
}

TEST_CASE("a wrong press has a key, a velocity and a time, and no note, hand or grade") {
    WrongPress p;
    p.key = 61;
    p.velocity = 33;
    p.song_s = 1.2;
    const RunEvent e = make_run_event(p);
    CHECK(e.kind == RunEventKind::Wrong);
    CHECK(e.key == 61);
    CHECK(e.velocity == 33);
    CHECK(e.song_ms == 1200);
    CHECK(e.note_index == -1);
    CHECK(e.hand == Hand::Unknown);
}

TEST_CASE("an event is forced into range however it was made") {
    NoteResult r = hit(1, std::numeric_limits<double>::quiet_NaN(), 0, 500, NoteGrade::Good);
    r.expected_pitch = 300;
    const RunEvent e = make_run_event(r);
    CHECK(e.song_ms == 0);
    CHECK(e.velocity == 127);
    CHECK(e.key == 127);
    RunEvent bad;
    bad.kind = static_cast<RunEventKind>(9);
    bad.hand = static_cast<Hand>(9);
    bad.grade = static_cast<NoteGrade>(99);
    bad.key = 200;
    bad.song_ms = -5;
    const RunEvent fixed = sanitized(bad);
    CHECK(fixed.kind == RunEventKind::Wrong);
    CHECK(fixed.key == 127);
    CHECK(fixed.song_ms == 0);
    CHECK(fixed.note_index == -1);
    CHECK(sanitized(fixed) == fixed); // idempotent
}

TEST_CASE("the recorder fills its buffer once: recording up to the cap allocates nothing") {
    RunEventRecorder recorder(64);
    CHECK(recorder.capacity() == 64);
    test::start_counting_allocations();
    for (std::uint32_t i = 0; i < 64; ++i) {
        if (i % 3 == 0) {
            recorder.add(miss(i, i * 0.5));
        } else if (i % 3 == 1) {
            recorder.add(hit(i, i * 0.5, 4'000'000, 90, NoteGrade::Perfect));
        } else {
            WrongPress w;
            w.key = 61;
            w.velocity = 50;
            w.song_s = i * 0.5;
            recorder.add_wrong(w);
        }
    }
    CHECK(test::stop_counting_allocations() == 0);
    CHECK(recorder.size() == 64);
    CHECK(recorder.dropped() == 0);
}

TEST_CASE("past the cap the recorder keeps the first events, counts the rest as dropped, and the "
          "run is never complete") {
    RunEventRecorder recorder(4);
    for (std::uint32_t i = 0; i < 6; ++i) {
        recorder.add(hit(i, i * 0.5, 0, 90, NoteGrade::Perfect));
    }
    CHECK(recorder.size() == 4);
    CHECK(recorder.dropped() == 2);
    test::start_counting_allocations();
    recorder.add(hit(6, 3.0, 0, 90, NoteGrade::Perfect)); // a full buffer takes nothing more
    CHECK(test::stop_counting_allocations() == 0);
    CHECK(recorder.dropped() == 3);

    const RunEvents out = recorder.finish(4);
    REQUIRE(out.events.size() == 4);
    CHECK(out.events.front().note_index == 0);
    CHECK(out.events.back().note_index == 3); // the first four, not the last
    CHECK(out.dropped == 3);
    CHECK_FALSE(out.complete); // even though the four kept cover four expected notes
    // The next run starts empty and clean.
    CHECK(recorder.size() == 0);
    CHECK(recorder.dropped() == 0);
}

TEST_CASE("finish orders the events by song time and empties the recorder") {
    RunEventRecorder recorder(16);
    recorder.add(hit(2, 2.0, 0, 90, NoteGrade::Perfect));
    WrongPress w;
    w.key = 61;
    w.velocity = 40;
    w.song_s = 1.0;
    recorder.add_wrong(w);
    recorder.add(miss(1, 1.5));
    recorder.add(hit(0, 0.5, 0, 90, NoteGrade::Perfect));
    const RunEvents out = recorder.finish(3);
    REQUIRE(out.events.size() == 4);
    CHECK(out.events[0].song_ms == 500);
    CHECK(out.events[1].kind == RunEventKind::Wrong);
    CHECK(out.events[2].kind == RunEventKind::Missed);
    CHECK(out.events[3].song_ms == 2000);
    CHECK(out.expected_notes == 3);
    CHECK(out.complete); // wrong presses are not notes; the three notes are each there once
    CHECK(recorder.size() == 0);
}

TEST_CASE("handing a run over unordered and ordering it later gives what finish gives") {
    const auto fill = [](RunEventRecorder& recorder) {
        recorder.add(hit(2, 2.0, 0, 90, NoteGrade::Perfect));
        WrongPress w;
        w.key = 61;
        w.velocity = 40;
        w.song_s = 1.0;
        recorder.add_wrong(w);
        recorder.add(miss(1, 1.5));
        recorder.add(hit(0, 0.5, 0, 90, NoteGrade::Perfect));
    };
    RunEventRecorder whole(16);
    fill(whole);
    const RunEvents expected = whole.finish(3);

    RunEventRecorder later(16);
    fill(later);
    RunEvents out = later.hand_over(3);
    CHECK(out.unordered);
    CHECK_FALSE(out.complete); // not decided until it is ordered
    REQUIRE(out.events.size() == 4);
    CHECK(out.events.front().song_ms == 2000); // as the evaluator settled them
    CHECK(later.size() == 0);                  // the recorder is empty for the next run
    finish_ordering(out);
    CHECK_FALSE(out.unordered);
    CHECK(out == expected);
    CHECK(out.complete);
    const RunEvents once = out;
    finish_ordering(out); // ordering an ordered run changes nothing
    CHECK(out == once);

    // A run whose setup changed is never complete, however many notes it has.
    RunEventRecorder changed(16);
    fill(changed);
    RunEvents moved = changed.hand_over(3);
    moved.setup_changed = true;
    finish_ordering(moved);
    CHECK_FALSE(moved.complete);
    CHECK(moved.events == expected.events);
}

TEST_CASE("a run is complete only when every practised note has exactly one result") {
    const auto record = [](std::initializer_list<std::uint32_t> notes, std::size_t practised) {
        RunEventRecorder recorder(32);
        for (const std::uint32_t n : notes) {
            recorder.add(n % 2 == 0 ? hit(n, n * 0.5, 0, 80, NoteGrade::Good) : miss(n, n * 0.5));
        }
        return recorder.finish(practised);
    };
    CHECK(record({0, 1, 2, 3}, 4).complete);             // hits and misses both count as judged
    CHECK_FALSE(record({0, 1, 2}, 4).complete);          // a note never reached
    CHECK_FALSE(record({0, 1, 2, 3, 3}, 4).complete);    // a note judged twice
    CHECK_FALSE(record({0, 1, 2, 2}, 4).complete);       // four results, one note twice, one absent
    CHECK_FALSE(record({0, 1, 2, 3, 4, 5}, 4).complete); // a loop that went round again
    CHECK_FALSE(record({}, 0).complete);                 // nothing to judge is no run
    CHECK(record({5, 9, 11}, 3).complete);               // indices are the song's, not 0..n
}

TEST_CASE(
    "events the recorder was never handed are counted as dropped and the run is not complete") {
    RunEventRecorder recorder(16);
    recorder.add(hit(0, 0.5, 0, 90, NoteGrade::Perfect));
    recorder.add_dropped(0);
    recorder.add_dropped(-3);
    CHECK(recorder.dropped() == 0);
    recorder.add_dropped(8);
    recorder.add_dropped(std::numeric_limits<int>::max());
    CHECK(recorder.dropped() == std::numeric_limits<std::int32_t>::max()); // saturates
    const RunEvents out = recorder.finish(1);
    CHECK(out.dropped == std::numeric_limits<std::int32_t>::max());
    CHECK_FALSE(out.complete); // the one note is there once, but wrong notes are missing
}
