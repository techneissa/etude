// SPDX-License-Identifier: GPL-3.0-or-later
// The player's own sound in a replay (replay_sound.hpp): note-ons as the replay passes a key,
// note-offs when the block ends, silence on a seek or when the sound goes off.
#include "alloc_count.hpp"
#include "replay_test_support.hpp"

#include "etude/replay_request.hpp"
#include "etude/replay_sound.hpp"

#include <doctest/doctest.h>

#include <array>
#include <vector>

using namespace replay_test;

namespace {

ReplayNote note(int time_ms, int key, int velocity, ReplayMark mark = ReplayMark::Perfect,
                int length_ms = 500) {
    ReplayNote n;
    n.time_ms = time_ms;
    n.key = static_cast<std::uint8_t>(key);
    n.velocity = static_cast<std::uint8_t>(velocity);
    n.mark = mark;
    n.expected_start_ms = time_ms;
    n.expected_end_ms = time_ms + length_ms;
    return n;
}

std::vector<ReplaySoundEvent> taken(ReplaySound& s) {
    const auto span = s.take();
    return {span.begin(), span.end()};
}

// In the stream a listener gets, every key's events alternate note-on, note-off from a silent
// start, and end silent: no note-on twice without a note-off between, no note-off with nothing
// sounding, nothing left ringing.
void check_pairs(const std::vector<ReplaySoundEvent>& stream) {
    std::array<bool, 128> sounding{};
    for (const ReplaySoundEvent& e : stream) {
        REQUIRE(e.key < 128);
        bool& on = sounding[e.key];
        if (e.velocity > 0) {
            CHECK_FALSE(on); // a note-on over a sounding key
            on = true;
        } else {
            CHECK(on); // a note-off with nothing to let go
            on = false;
        }
    }
    for (int key = 0; key < 128; ++key) {
        CHECK_FALSE(sounding[static_cast<std::size_t>(key)]);
    }
}

} // namespace

TEST_CASE("replay sound: a passed note sounds at its velocity and lets go when its block ends") {
    ReplaySound s;
    const ReplayNote n = note(1000, 60, 90);
    s.step(std::span<const ReplayNote>(&n, 1), 1010);
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{60, 90}});
    CHECK(s.sounding(60));
    s.step({}, 1499);
    CHECK(taken(s).empty());
    s.step({}, 1500); // exactly at the end: let go
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{60, 0}});
    CHECK_FALSE(s.sounding(60));
}

TEST_CASE("replay sound: a missed note is silent, a wrong key sounds") {
    ReplaySound s;
    const ReplayNote notes[] = {note(0, 60, 0, ReplayMark::Missed), note(0, 61, 55, ReplayMark::Wrong, 200)};
    s.step(notes, 10);
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{61, 55}});
}

TEST_CASE("replay sound: a key struck again while it sounds is let go first") {
    ReplaySound s;
    const ReplayNote a = note(0, 60, 80, ReplayMark::Perfect, 1000);
    s.step(std::span<const ReplayNote>(&a, 1), 5);
    s.take();
    const ReplayNote b = note(300, 60, 70);
    s.step(std::span<const ReplayNote>(&b, 1), 310);
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{60, 0}, {60, 70}});
    // The second stroke's own end governs.
    s.step({}, 800);
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{60, 0}});
}

TEST_CASE("replay sound: silence lets go of everything that sounds, and only that") {
    ReplaySound s;
    const ReplayNote notes[] = {note(0, 60, 80), note(0, 64, 80)};
    s.step(notes, 5);
    s.take();
    s.silence();
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{60, 0}, {64, 0}});
    s.silence();
    CHECK(taken(s).empty());
}

TEST_CASE("replay sound: switched off, nothing is queued and what sounds is let go") {
    ReplaySound s;
    const ReplayNote n = note(0, 60, 80);
    s.step(std::span<const ReplayNote>(&n, 1), 5);
    s.take();
    s.set_on(false);
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{60, 0}});
    const ReplayNote m = note(100, 62, 80);
    s.step(std::span<const ReplayNote>(&m, 1), 110);
    CHECK(taken(s).empty());
    s.set_on(true);
    s.step(std::span<const ReplayNote>(&m, 1), 110);
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{62, 80}});
    s.set_on(true); // the same again changes nothing
    CHECK(taken(s).empty());
}

TEST_CASE("replay sound: a very short block still sounds for the least length") {
    ReplaySound s;
    const ReplayNote n = note(0, 60, 80, ReplayMark::Perfect, 5);
    s.step(std::span<const ReplayNote>(&n, 1), 1);
    s.take();
    s.step({}, kMinReplayLengthMs - 1);
    CHECK(taken(s).empty());
    s.step({}, kMinReplayLengthMs);
    CHECK(taken(s) == std::vector<ReplaySoundEvent>{{60, 0}});
}

TEST_CASE("replay sound: a step that passes more notes than the queue holds drops note-ons, never note-offs") {
    ReplaySound s;
    std::vector<ReplayNote> many;
    for (int i = 0; i < 128; ++i) {
        many.push_back(note(0, i, 80));
    }
    for (int round = 0; round < 6; ++round) {
        s.step(many, 5);
    }
    const auto first = taken(s);
    CHECK(first.size() <= ReplaySound::kMaxEvents);
    s.silence();
    const auto offs = taken(s);
    for (int key = 0; key < 128; ++key) {
        CHECK_FALSE(s.sounding(key));
    }
    for (const ReplaySoundEvent& e : offs) {
        CHECK(e.velocity == 0);
    }
    // What a listener gets, the two takes together, pairs up on every key.
    std::vector<ReplaySoundEvent> stream = first;
    stream.insert(stream.end(), offs.begin(), offs.end());
    check_pairs(stream);
}

TEST_CASE("replay sound: taken every step, a crowd of notes still pairs on every key") {
    ReplaySound s;
    std::vector<ReplayNote> many;
    for (int i = 0; i < 128; ++i) {
        many.push_back(note(0, i, 80, ReplayMark::Perfect, 100 + i));
    }
    std::vector<ReplaySoundEvent> stream;
    for (int round = 0; round < 40; ++round) {
        s.step(many, round * 50); // every key struck again every 50 ms, each held 100+ ms
        const auto got = taken(s);
        stream.insert(stream.end(), got.begin(), got.end());
    }
    s.silence();
    const auto offs = taken(s);
    stream.insert(stream.end(), offs.begin(), offs.end());
    check_pairs(stream);
}

TEST_CASE("replay sound: stepping allocates nothing") {
    ReplaySound s;
    const ReplayNote n = note(0, 60, 80);
    etude::test::start_counting_allocations();
    for (int i = 0; i < 100; ++i) {
        s.step(std::span<const ReplayNote>(&n, 1), i * 10);
        s.take();
    }
    s.silence();
    s.set_on(false);
    CHECK(etude::test::stop_counting_allocations() == 0);
}

TEST_CASE("replay request: only the latest request's answer about the open song applies") {
    ReplayRequestGate g;
    CHECK_FALSE(g.pending());
    CHECK_FALSE(g.accepts(1, 0xabc, 0xabc)); // nothing was asked
    g.begin(7, 0xabc);
    CHECK(g.pending());
    CHECK(g.accepts(7, 0xabc, 0xabc));
    CHECK_FALSE(g.accepts(6, 0xabc, 0xabc)); // an older request
    CHECK_FALSE(g.accepts(8, 0xabc, 0xabc)); // one never made
    CHECK_FALSE(g.accepts(7, 0xdef, 0xabc)); // about another song
    CHECK_FALSE(g.accepts(7, 0xabc, 0xdef)); // the open song changed meanwhile
    g.begin(9, 0xabc); // a newer request replaces it
    CHECK_FALSE(g.accepts(7, 0xabc, 0xabc));
    CHECK(g.accepts(9, 0xabc, 0xabc));
    g.cancel();
    CHECK_FALSE(g.accepts(9, 0xabc, 0xabc));
    g.begin(10, 0); // a song with no content hash has no replay
    CHECK_FALSE(g.accepts(10, 0, 0));
}
