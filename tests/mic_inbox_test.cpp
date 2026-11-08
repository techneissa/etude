// SPDX-License-Identifier: GPL-3.0-or-later
// The microphone's queue of detected notes: what comes out of it, and that taking at any point of a
// frame loses and repeats nothing.
#include "mic_inbox.hpp"

#include <doctest/doctest.h>

#include <deque>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

constexpr std::int64_t kMs = 1'000'000;

MicEventIn on(int note, std::int64_t onset_ns) {
    MicEventIn in;
    in.event.type = PitchEventType::NoteOn;
    in.event.note = note;
    in.onset_ns = onset_ns;
    in.sample_ns = onset_ns + 40 * kMs;
    return in;
}

MicEventIn not_sure(std::int64_t onset_ns) {
    MicEventIn in;
    in.event.type = PitchEventType::NotSure;
    in.onset_ns = onset_ns;
    in.sample_ns = onset_ns + 100 * kMs;
    return in;
}

// An expected-note verdict: the detector confirmed `heard` against the chord it was asked about.
MicEventIn expected(int note, std::int64_t onset_ns) {
    MicEventIn in;
    in.event.type = PitchEventType::Expected;
    in.event.verdict = ExpectedVerdict::Expected;
    in.event.note = note;
    in.onset_ns = onset_ns;
    in.sample_ns = onset_ns + 60 * kMs;
    in.expected_count = 1;
    in.expected[0] = static_cast<std::int8_t>(note);
    return in;
}

struct FakeSource {
    std::deque<MicEventIn> queue;
    bool pop(MicEventIn& out) {
        if (queue.empty()) {
            return false;
        }
        out = queue.front();
        queue.pop_front();
        return true;
    }
};

} // namespace

TEST_CASE("mic inbox: a note comes out once, stamped at its onset, tagged as the microphone") {
    MicInbox inbox;
    FakeSource src;
    src.queue.push_back(on(60, 100 * kMs));
    std::vector<MidiEvent> events;
    std::vector<UnsureMark> marks;
    inbox.take([&](MicEventIn& in) { return src.pop(in); }, 300 * kMs, events, marks);
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == MidiEventType::NoteOn);
    CHECK(events[0].data1 == 60);
    CHECK(events[0].time_ns == 100 * kMs);
    CHECK(events[0].source == InputSource::Microphone);
    CHECK(inbox.last_note() == 60);
    // A second take, later in the same frame, finds nothing new.
    events.clear();
    inbox.take([&](MicEventIn& in) { return src.pop(in); }, 310 * kMs, events, marks);
    CHECK(events.empty());
}

TEST_CASE("mic inbox: notes taken in two pulls come out in decided order, none twice") {
    MicInbox inbox;
    FakeSource src;
    std::vector<MidiEvent> events;
    std::vector<UnsureMark> marks;
    const auto pop = [&](MicEventIn& in) { return src.pop(in); };
    src.queue.push_back(on(60, 100 * kMs));
    inbox.take(pop, 200 * kMs, events, marks);
    src.queue.push_back(on(64, 150 * kMs));
    src.queue.push_back(on(67, 160 * kMs));
    inbox.take(pop, 210 * kMs, events, marks);
    REQUIRE(events.size() == 3);
    CHECK(events[0].data1 == 60);
    CHECK(events[1].data1 == 64);
    CHECK(events[2].data1 == 67);
    CHECK(inbox.pending_onsets().size() == 3);
}

TEST_CASE("mic inbox: a not-sure event is a marker with its onset, and presses nothing") {
    MicInbox inbox;
    FakeSource src;
    src.queue.push_back(on(60, 100 * kMs));
    src.queue.push_back(not_sure(400 * kMs));
    std::vector<MidiEvent> events;
    std::vector<UnsureMark> marks;
    inbox.take([&](MicEventIn& in) { return src.pop(in); }, 600 * kMs, events, marks);
    CHECK(events.size() == 1);
    REQUIRE(marks.size() == 1);
    CHECK(marks[0].onset_ns == 400 * kMs);
    CHECK(marks[0].last_heard == 60);
    CHECK(marks[0].at_ns == 600 * kMs);
    CHECK(inbox.unsure_seen() == 1);
}

TEST_CASE("mic inbox: a confirmed expected note is released by a later take, once") {
    MicInbox inbox;
    FakeSource src;
    const auto pop = [&](MicEventIn& in) { return src.pop(in); };
    std::vector<MidiEvent> events;
    std::vector<UnsureMark> marks;
    src.queue.push_back(expected(60, 100 * kMs));
    inbox.take(pop, 160 * kMs, events, marks);
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == MidiEventType::NoteOn);
    inbox.take(pop, 100 * kMs + kMicExpectedHoldNs - 1, events, marks);
    CHECK(events.size() == 1); // the hold has not ended
    inbox.take(pop, 100 * kMs + kMicExpectedHoldNs, events, marks);
    REQUIRE(events.size() == 2);
    CHECK(events[1].type == MidiEventType::NoteOff);
    CHECK(events[1].data1 == 60);
    inbox.take(pop, 5'000 * kMs, events, marks);
    CHECK(events.size() == 2);
}

TEST_CASE("mic inbox: closing the microphone releases the keys it holds, stamped at the close") {
    MicInbox inbox;
    FakeSource src;
    const auto pop = [&](MicEventIn& in) { return src.pop(in); };
    std::vector<MidiEvent> events;
    std::vector<UnsureMark> marks;
    src.queue.push_back(on(60, 100 * kMs));
    src.queue.push_back(on(64, 110 * kMs));
    inbox.take(pop, 200 * kMs, events, marks); // taken, then the input switches to MIDI
    inbox.release_all(250 * kMs);
    // The next take reads nothing (the pipeline is closed), only hands the releases out.
    inbox.flush(events);
    int down[128] = {};
    for (const MidiEvent& ev : events) {
        down[ev.data1] += ev.type == MidiEventType::NoteOn ? 1 : -1;
    }
    CHECK(down[60] == 0);
    CHECK(down[64] == 0);
    REQUIRE(events.size() == 4);
    CHECK(events[2].time_ns == 250 * kMs);
    CHECK(events[3].time_ns == 250 * kMs);
    // Handed out once.
    inbox.flush(events);
    CHECK(events.size() == 4);
    // A take hands them out too when one comes first.
    inbox.release_all(300 * kMs);
    CHECK(events.size() == 4); // (nothing was held any more)
    inbox.take(pop, 310 * kMs, events, marks);
    CHECK(events.size() == 4);
}
