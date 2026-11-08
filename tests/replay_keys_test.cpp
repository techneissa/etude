// SPDX-License-Identifier: GPL-3.0-or-later
// The keys a replay lights (replay_keys.hpp): a played key lit in its hand for the block's drawn
// length, a wrong key marked as wrong, a missed note dark, dark again on a clear, mid-note keys lit
// where a paused replay stands.
#include "alloc_count.hpp"

#include "etude/replay_keys.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

ReplayNote note(int time_ms, int key, int velocity, Hand hand, ReplayMark mark = ReplayMark::Perfect,
                int length_ms = 500) {
    ReplayNote n;
    n.time_ms = time_ms;
    n.key = static_cast<std::uint8_t>(key);
    n.velocity = static_cast<std::uint8_t>(velocity);
    n.hand = hand;
    n.mark = mark;
    if (mark != ReplayMark::Wrong) {
        n.expected_start_ms = time_ms;
        n.expected_end_ms = time_ms + length_ms;
    }
    return n;
}

ReplayTimeline timeline_of(std::vector<ReplayNote> notes) {
    ReplayTimeline t;
    t.notes = std::move(notes);
    for (const ReplayNote& n : t.notes) {
        t.max_length_ms = std::max(t.max_length_ms, replay_length_ms(n));
    }
    return t;
}

} // namespace

TEST_CASE("replay keys: a passed note lights its key in its hand and lets go when its block ends") {
    ReplayKeys k;
    const ReplayNote n = note(1000, 60, 90, Hand::Left);
    k.step(std::span<const ReplayNote>(&n, 1), 1010);
    const ReplayLitKey& lit = k.lit()[60];
    CHECK(lit.on);
    CHECK_FALSE(lit.wrong);
    CHECK(lit.velocity == 90);
    CHECK(lit.hand == Hand::Left);
    CHECK(k.lit_count() == 1);
    k.step({}, 1499);
    CHECK(k.lit()[60].on);
    k.step({}, 1500); // the block's end: dark
    CHECK_FALSE(k.lit()[60].on);
    CHECK(k.lit_count() == 0);
}

TEST_CASE("replay keys: a wrong key is marked wrong for the length a wrong press is drawn") {
    ReplayKeys k;
    const ReplayNote n = note(0, 61, 55, Hand::Unknown, ReplayMark::Wrong);
    k.step(std::span<const ReplayNote>(&n, 1), 10);
    CHECK(k.lit()[61].on);
    CHECK(k.lit()[61].wrong);
    k.step({}, kReplayWrongLengthMs - 1);
    CHECK(k.lit()[61].on);
    k.step({}, kReplayWrongLengthMs);
    CHECK_FALSE(k.lit()[61].on);
}

TEST_CASE("replay keys: a missed note lights nothing, and a key struck again starts over") {
    ReplayKeys k;
    const ReplayNote notes[] = {note(0, 60, 0, Hand::Right, ReplayMark::Missed),
                                note(0, 62, 70, Hand::Right)};
    k.step(notes, 5);
    CHECK_FALSE(k.lit()[60].on);
    CHECK(k.lit()[62].on);
    const ReplayNote again = note(300, 62, 100, Hand::Left, ReplayMark::Perfect, 1000);
    k.step(std::span<const ReplayNote>(&again, 1), 300);
    CHECK(k.lit()[62].velocity == 100);
    CHECK(k.lit()[62].hand == Hand::Left);
    k.step({}, 1000); // the first block's end is past: the second holds the key
    CHECK(k.lit()[62].on);
    k.step({}, 1300);
    CHECK_FALSE(k.lit()[62].on);
}

TEST_CASE("replay keys: clear puts every key out") {
    ReplayKeys k;
    const ReplayNote notes[] = {note(0, 60, 80, Hand::Right), note(0, 64, 80, Hand::Left)};
    k.step(notes, 5);
    REQUIRE(k.lit_count() == 2);
    k.clear();
    CHECK(k.lit_count() == 0);
}

TEST_CASE("replay keys: seating at a position lights the keys that are mid-note there") {
    const ReplayTimeline t = timeline_of({note(0, 60, 80, Hand::Right, ReplayMark::Perfect, 4000),
                                          note(1000, 62, 80, Hand::Left),
                                          note(1200, 64, 80, Hand::Left, ReplayMark::Missed),
                                          note(1400, 65, 80, Hand::Right, ReplayMark::Perfect, 100),
                                          note(3000, 67, 80, Hand::Right)});
    ReplayKeys k;
    k.seat(t, 1450);
    CHECK(k.lit()[60].on);  // a long note from the start, still going
    CHECK(k.lit()[62].on);  // 1000..1500
    CHECK_FALSE(k.lit()[64].on); // missed
    CHECK(k.lit()[65].on);  // 1400..1500
    CHECK_FALSE(k.lit()[67].on); // not yet
    k.seat(t, 1600); // 62 and 65 are over
    CHECK(k.lit()[60].on);
    CHECK_FALSE(k.lit()[62].on);
    CHECK_FALSE(k.lit()[65].on);
    k.seat(t, 5000); // everything is over
    CHECK(k.lit_count() == 0);
    // Seating clears what was lit before.
    k.seat(t, 1450);
    k.seat(t, 3100);
    CHECK(k.lit()[67].on);
    CHECK(k.lit()[60].on);
    CHECK_FALSE(k.lit()[62].on);
    CHECK_FALSE(k.lit()[65].on);
}

TEST_CASE("replay keys: stepping and seating allocate nothing") {
    const ReplayTimeline t = timeline_of({note(0, 60, 80, Hand::Right), note(500, 62, 80, Hand::Left)});
    ReplayKeys k;
    etude::test::start_counting_allocations();
    const ReplayNote n = note(100, 61, 70, Hand::Right);
    k.step(std::span<const ReplayNote>(&n, 1), 120);
    k.seat(t, 300);
    k.clear();
    CHECK(etude::test::stop_counting_allocations() == 0);
}
