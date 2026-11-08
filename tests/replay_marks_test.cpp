// SPDX-License-Identifier: GPL-3.0-or-later
// What a replay draws over the falling notes (replay_marks.hpp): the played notes as bars placed
// like the song's own, early below and late above, missed and wrong ones, and only what is in view.
#include "alloc_count.hpp"
#include "replay_test_support.hpp"

#include "etude/replay_marks.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace replay_test;

namespace {

ReplayTimeline timeline_of(const Fixture& f, const RunEvents& run) {
    return build_replay_timeline(run, {}, {f.notes, f.measures});
}

std::vector<ReplayBarMark> marks_at(const ReplayTimeline& t, double pos_ms, double ahead_s = 4.0,
                                    double behind_s = 0.25) {
    std::vector<ReplayBarMark> out;
    replay_bar_marks(t, pos_ms, ahead_s, behind_s, out);
    return out;
}

} // namespace

TEST_CASE("replay marks: a played note is placed like a falling note, from its press time") {
    const Fixture f = make_fixture();
    // The note at 1000 ms (key 74, half a second long), pressed on time.
    const ReplayTimeline t = timeline_of(f, run_of({perfect(f, 1)}));
    const auto marks = marks_at(t, 0.0, 4.0);
    REQUIRE(marks.size() == 1);
    CHECK(marks[0].key == 74);
    CHECK(marks[0].bottom == doctest::Approx(0.25f)); // 1 s of the 4 s above the line
    CHECK(marks[0].top == doctest::Approx(0.375f));   // and half a second more
    CHECK(marks[0].mark == ReplayMark::Perfect);
    CHECK(marks[0].lean == 0);
}

TEST_CASE("replay marks: early lies below its note and late above it, by the sign of the offset") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(
        f, run_of({press(1, 960, -40, NoteGrade::Early, 74, Hand::Right),
                   press(4, 3070, 70, NoteGrade::Late, 74, Hand::Right)}));
    const auto marks = marks_at(t, 0.0, 4.0);
    REQUIRE(marks.size() == 2);
    CHECK(marks[0].lean == -1);
    CHECK(marks[0].bottom < 0.25f); // nearer the hit line than the note at 1000 ms
    CHECK(marks[1].lean == 1);
    CHECK(marks[1].bottom > 3.0f / 4.0f); // above the note at 3000 ms
}

TEST_CASE("replay marks: a Good or Perfect press never leans, however far off") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(
        f, run_of({press(1, 1030, 30, NoteGrade::Good, 74, Hand::Right)}));
    CHECK(marks_at(t, 0.0)[0].lean == 0);
    // An Early grade with no offset to read (the note is not in the song) has no side to lean.
    const ReplayTimeline gone = timeline_of(
        f, run_of({press(99, 1000, -50, NoteGrade::Early, 74, Hand::Right)}));
    CHECK(marks_at(gone, 0.0)[0].lean == 0);
}

TEST_CASE("replay marks: a missed note is the hatched block of the note it was; a wrong key a short bar") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(
        f, run_of({miss(2, 0, 48, Hand::Left), wrong(1000, 61)}));
    const auto marks = marks_at(t, 0.0, 4.0);
    REQUIRE(marks.size() == 2);
    CHECK(marks[0].mark == ReplayMark::Missed);
    CHECK(marks[0].bottom == doctest::Approx(0.0f));
    CHECK(marks[0].top == doctest::Approx(0.25f)); // the note is a second long
    CHECK(marks[1].mark == ReplayMark::Wrong);
    CHECK(marks[1].top - marks[1].bottom ==
          doctest::Approx(kReplayWrongLengthMs / 4000.0f));
}

TEST_CASE("replay marks: only the bars in view, edges included") {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    for (int i = 0; i < 12; ++i) {
        events.push_back(perfect(f, i));
    }
    const ReplayTimeline t = timeline_of(f, run_of(events));
    // At 3000 ms with 1 s above the line: the stretch (2750 ms, 4000 ms).
    const auto marks = marks_at(t, 3000.0, 1.0, 0.25);
    for (const ReplayBarMark& m : marks) {
        CHECK(m.top > -0.25f);
        CHECK(m.bottom < 1.0f);
    }
    // The note at 2000 ms (key 72 and 48) ends at 2500 and 3000: the 48 ends after 2750 -> in view;
    // the 72 ended before it -> out. The note at 3000 (74) is in view, the one at 4000 is not.
    std::vector<int> keys;
    for (const ReplayBarMark& m : marks) {
        keys.push_back(m.key);
    }
    CHECK(keys == std::vector<int>{48, 74});
}

TEST_CASE("replay marks: a long note that began before the view still shows") {
    const Fixture f = make_fixture();
    // Note 2 starts at 0 and lasts a second; at 900 ms it is still sounding.
    const ReplayTimeline t = timeline_of(f, run_of({miss(2, 0, 48, Hand::Left), perfect(f, 7)}));
    const auto marks = marks_at(t, 900.0, 2.0, 0.0);
    REQUIRE(!marks.empty());
    CHECK(marks[0].key == 48);
    CHECK(marks[0].bottom < 0.0f);
    CHECK(marks[0].top > 0.0f);
}

TEST_CASE("replay marks: nothing for an empty replay, a bad window or a position that is not a number") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(f, run_of({perfect(f, 0)}));
    CHECK(marks_at(ReplayTimeline{}, 0.0).empty());
    CHECK(marks_at(t, 0.0, 0.0).empty());
    CHECK(marks_at(t, 0.0, -1.0).empty());
    CHECK(marks_at(t, std::nan(""), 4.0).empty());
}

TEST_CASE("replay marks: the list is cleared each call and reuses its storage") {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    for (int i = 0; i < 12; ++i) {
        events.push_back(perfect(f, i));
    }
    const ReplayTimeline t = timeline_of(f, run_of(events));
    std::vector<ReplayBarMark> out;
    out.reserve(64);
    replay_bar_marks(t, 0.0, 4.0, 0.25, out);
    const std::size_t first = out.size();
    CHECK(first > 0);
    etude::test::start_counting_allocations();
    replay_bar_marks(t, 4000.0, 4.0, 0.25, out);
    replay_bar_marks(t, 0.0, 4.0, 0.25, out);
    CHECK(etude::test::stop_counting_allocations() == 0);
    CHECK(out.size() == first);
}

TEST_CASE("replay marks: no more than the cap, the nearest the hit line kept") {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    for (int i = 0; i < 2000; ++i) {
        events.push_back(press(0, i, 0, NoteGrade::Perfect, 72, Hand::Right));
    }
    const ReplayTimeline t = timeline_of(f, run_of(events));
    const auto marks = marks_at(t, 0.0, 10.0, 0.0);
    CHECK(marks.size() == kMaxReplayMarks);
    CHECK(marks.front().bottom == doctest::Approx(0.0f));
}
