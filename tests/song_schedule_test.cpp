// SPDX-License-Identifier: GPL-3.0-or-later
// The song's scheduling rules (etude/song_schedule.hpp): what is due when, what a command
// cancels, how a full queue gives way, and what a piano gets when its port comes back.
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <bitset>
#include <vector>

using namespace etude;

namespace {

constexpr std::int64_t kMs = 1'000'000;

PlaybackMessage note_on(int key, std::int64_t due, int channel = 0) {
    PlaybackMessage m;
    m.kind = PlaybackMessage::Kind::NoteOn;
    m.channel = static_cast<std::uint8_t>(channel);
    m.data = static_cast<std::uint8_t>(key);
    m.value = 80;
    m.at_ns = due;
    return m;
}

PlaybackMessage note_off(int key, std::int64_t due, int channel = 0) {
    PlaybackMessage m = note_on(key, due, channel);
    m.kind = PlaybackMessage::Kind::NoteOff;
    m.value = 0;
    return m;
}

PlaybackMessage control(int controller, int value, std::int64_t due, int channel = 0) {
    PlaybackMessage m;
    m.kind = PlaybackMessage::Kind::ControlChange;
    m.channel = static_cast<std::uint8_t>(channel);
    m.data = static_cast<std::uint8_t>(controller);
    m.value = static_cast<std::uint16_t>(value);
    m.at_ns = due;
    return m;
}

template <std::size_t N> std::vector<PlaybackMessage> contents(const DueQueue<N>& q) {
    std::vector<PlaybackMessage> out;
    for (std::size_t i = 0; i < q.size(); ++i) {
        out.push_back(q[i]);
    }
    return out;
}

} // namespace

TEST_CASE("a queue gives messages back in due order, equal times in the order they came") {
    DueQueue<16> q;
    q.push(note_on(60, 30 * kMs));
    q.push(note_on(62, 10 * kMs));
    q.push(note_off(62, 10 * kMs));
    q.push(note_on(64, 20 * kMs));
    REQUIRE(q.size() == 4);
    CHECK(q.front().data == 62);
    CHECK(q.front().kind == PlaybackMessage::Kind::NoteOn);
    q.pop_front();
    CHECK(q.front().kind == PlaybackMessage::Kind::NoteOff); // after its own note-on
    q.pop_front();
    CHECK(q.front().data == 64);
    q.pop_front();
    CHECK(q.front().data == 60);
    q.pop_front();
    CHECK(q.empty());
}

TEST_CASE("a cancel drops the note-ons due after the command and the note-offs that end them") {
    DueQueue<32> q;
    q.push(note_on(60, 100 * kMs));    // started: due before the command
    q.push(note_on(64, 110 * kMs));    // not started
    q.push(note_off(64, 125 * kMs));   // ends the one not started
    q.push(note_off(60, 130 * kMs));   // ends the one that started: stays
    q.push(note_on(67, 120 * kMs));    // not started
    q.push(note_on(67, 100 * kMs, 1)); // another channel, same key: started
    const std::size_t removed = q.cancel_unstarted(105 * kMs);
    CHECK(removed == 3);
    const auto left = contents(q);
    REQUIRE(left.size() == 3);
    CHECK(left[0].data == 60);
    CHECK(left[0].kind == PlaybackMessage::Kind::NoteOn);
    CHECK(left[1].data == 67);
    CHECK(left[1].channel == 1);
    CHECK(left[2].kind == PlaybackMessage::Kind::NoteOff);
    CHECK(left[2].data == 60);
}

TEST_CASE(
    "a note due at the very instant of the command has not started; one a nanosecond before has") {
    DueQueue<8> q;
    q.push(note_on(60, 100 * kMs));
    CHECK(q.cancel_unstarted(100 * kMs - 1) == 1); // the command came first
    CHECK(q.empty());
    q.push(note_on(60, 100 * kMs - 1));
    CHECK(q.cancel_unstarted(100 * kMs) == 0); // it started first
    CHECK(q.size() == 1);
    q.push(note_on(62, 100 * kMs));
    CHECK(q.cancel_unstarted(100 * kMs) == 1); // a pause at its exact start: no note-on
    CHECK(q.size() == 1);
}

TEST_CASE("a cancel makes what stays and was due later due at the command: the notes that started "
          "stop now") {
    DueQueue<16> q;
    q.push(note_on(60, 90 * kMs));     // started
    q.push(note_off(60, 112 * kMs));   // its end, due later: now
    q.push(note_on(64, 95 * kMs));     // started
    q.push(note_off(64, 98 * kMs));    // ended before the command: unchanged
    q.push(control(64, 0, 120 * kMs)); // a pedal release: now
    q.push(control(7, 90, 125 * kMs)); // a controller: now
    q.push(note_on(67, 130 * kMs));    // not started
    CHECK(q.cancel_unstarted(100 * kMs) == 1);
    REQUIRE(q.size() == 6);
    std::int64_t last = 0;
    for (std::size_t i = 0; i < q.size(); ++i) {
        CHECK(q[i].at_ns <= 100 * kMs);
        CHECK(q[i].at_ns >= last); // still in order
        last = q[i].at_ns;
    }
    CHECK(q[2].at_ns == 98 * kMs);
    CHECK(q[5].at_ns == 100 * kMs);
    // A cancel that finds everything due before it changes nothing.
    CHECK(q.cancel_unstarted(1'000 * kMs) == 0);
    CHECK(q[5].at_ns == 100 * kMs);
}

TEST_CASE("a cancel drops a pedal press that had not started and keeps everything else") {
    DueQueue<16> q;
    q.push(control(64, 127, 90 * kMs)); // started: stays
    q.push(control(64, 127,
                   110 * kMs)); // not started: dropped, or it would hold the pedal after the pause
    q.push(control(64, 0, 120 * kMs)); // a release always stays
    q.push(control(7, 90, 125 * kMs)); // a controller stays
    q.push(note_off(60, 130 * kMs));   // its note-on started before the command
    q.push(note_on(62, 111 * kMs));    // not started: dropped
    CHECK(q.cancel_unstarted(100 * kMs) == 2);
    REQUIRE(q.size() == 4);
    CHECK(q[0].value == 127);
    CHECK(q[1].value == 0);
    CHECK(q[2].data == 7);
    CHECK(q[3].kind == PlaybackMessage::Kind::NoteOff);
}

TEST_CASE("a note-on with velocity 0 counts as a note-off for a cancel") {
    DueQueue<8> q;
    q.push(note_on(60, 110 * kMs));
    PlaybackMessage end = note_on(60, 140 * kMs);
    end.value = 0;
    q.push(end);
    CHECK(q.cancel_unstarted(100 * kMs) == 2);
    CHECK(q.empty());
}

TEST_CASE("a full queue drops its oldest note-on, never a note-off") {
    DueQueue<4> q;
    q.push(note_off(40, 1 * kMs));
    q.push(note_on(60, 2 * kMs));
    q.push(note_on(62, 3 * kMs));
    q.push(note_off(41, 4 * kMs));
    REQUIRE(q.size() == 4);
    // A note-off arrives: the oldest note-on (60) makes room.
    CHECK(q.push(note_off(42, 5 * kMs)));
    CHECK(q.dropped_note_ons() == 1);
    const auto left = contents(q);
    REQUIRE(left.size() == 4);
    CHECK(left[0].data == 40);
    CHECK(left[1].data == 62);
    CHECK(left[2].data == 41);
    CHECK(left[3].data == 42);
    // Another note-on goes in the same way; the note-offs all stay.
    CHECK(q.push(note_on(70, 6 * kMs)));
    CHECK(q.dropped_note_ons() == 2);
    int offs = 0;
    for (const auto& m : contents(q)) {
        offs += ends_note(m) ? 1 : 0;
    }
    CHECK(offs == 3);
    CHECK(q.lost_note_offs() == 0);
}

TEST_CASE("a full queue with no note-on left drops a new note-on, then other messages, then counts "
          "a lost note-off") {
    DueQueue<3> q;
    q.push(control(64, 127, 1 * kMs));
    q.push(note_off(40, 2 * kMs));
    q.push(note_off(41, 3 * kMs));
    CHECK_FALSE(q.push(note_on(60, 4 * kMs))); // the new note-on is the one dropped
    CHECK(q.dropped_note_ons() == 1);
    CHECK(q.push(note_off(42, 5 * kMs))); // the controller makes room
    CHECK(q.dropped_other() == 1);
    CHECK(q.size() == 3);
    CHECK_FALSE(q.push(note_off(43, 6 * kMs))); // nothing but note-offs: reported for a panic
    CHECK(q.lost_note_offs() == 1);
}

TEST_CASE("a queue reuses the room at its front when its end is reached") {
    DueQueue<4> q;
    q.push(note_on(60, 1 * kMs));
    q.push(note_on(61, 2 * kMs));
    q.push(note_on(62, 3 * kMs));
    q.pop_front();
    q.pop_front();
    q.push(note_on(63, 4 * kMs));
    q.push(note_on(64, 5 * kMs)); // the end of the storage: the room freed at the front is used
    q.push(note_on(65, 6 * kMs));
    REQUIRE(q.size() == 4);
    CHECK(q.dropped_note_ons() == 0);
    CHECK(q[0].data == 62);
    CHECK(q[3].data == 65);
}

TEST_CASE("a piano whose port comes back is reset on all 16 channels, notes off before pedal") {
    const auto reset = port_reset_messages();
    REQUIRE(reset.size() == 32);
    for (std::size_t ch = 0; ch < 16; ++ch) {
        const PlaybackMessage& off = reset[2 * ch];
        const PlaybackMessage& pedal = reset[2 * ch + 1];
        CHECK(off.kind == PlaybackMessage::Kind::ControlChange);
        CHECK(off.channel == ch);
        CHECK(off.data == 123);
        CHECK(off.value == 0);
        CHECK(pedal.kind == PlaybackMessage::Kind::ControlChange);
        CHECK(pedal.channel == ch);
        CHECK(pedal.data == 64);
        CHECK(pedal.value == 0);
    }
}

TEST_CASE("only the port that dropped out is reset when it opens again") {
    PortReconnect watch;
    CHECK_FALSE(watch.reconnected("Yamaha PSR-E383")); // first connection
    watch.lost("Yamaha PSR-E383");
    CHECK_FALSE(watch.reconnected("Other keyboard"));
    CHECK(watch.reconnected("Yamaha PSR-E383"));
    CHECK_FALSE(watch.reconnected("Yamaha PSR-E383")); // once per drop-out
    watch.lost("Yamaha PSR-E383");
    watch.lost("Yamaha PSR-E383");
    CHECK(watch.reconnected("Yamaha PSR-E383"));
}

namespace {

// A fake output port: remembers what was sent to it and what it has sounding.
struct FakeOutPort {
    std::vector<PlaybackMessage> sent;
    std::bitset<128> sounding[16];
    bool pedal[16] = {};

    void send(const PlaybackMessage& m) {
        sent.push_back(m);
        if (m.kind == PlaybackMessage::Kind::NoteOn && m.value > 0) {
            sounding[m.channel].set(m.data);
        } else if (m.kind == PlaybackMessage::Kind::NoteOff) {
            sounding[m.channel].reset(m.data);
        } else if (m.kind == PlaybackMessage::Kind::ControlChange) {
            if (m.data == 64) {
                pedal[m.channel] = m.value >= 64;
            } else if (m.data == 123) {
                sounding[m.channel].reset();
            }
        }
    }
    bool silent() const {
        for (int ch = 0; ch < 16; ++ch) {
            if (sounding[ch].any() || pedal[ch]) {
                return false;
            }
        }
        return true;
    }
};

// What MidiOutput does when its port opens: on a return of the same port, the reset first.
void open_port(FakeOutPort& port, PortReconnect& watch, const char* name) {
    if (watch.reconnected(name)) {
        for (const PlaybackMessage& m : port_reset_messages()) {
            port.send(m);
        }
    }
}

} // namespace

TEST_CASE("a note held across a drop-out cannot stay on: the reset is the first thing sent") {
    PortReconnect watch;
    FakeOutPort port;
    open_port(port, watch, "Piano");
    CHECK(port.sent.empty()); // a first connection is not reset
    // The song held a chord and the pedal when the cable came out; the piano kept them.
    port.send(note_on(60, 0));
    port.send(note_on(64, 0, 5));
    port.send(control(64, 127, 0, 5));
    CHECK_FALSE(port.silent());
    watch.lost("Piano");
    port.sent.clear();
    open_port(port, watch, "Piano");
    REQUIRE(port.sent.size() == 32);
    CHECK(port.sent.front().data == 123);
    CHECK(port.silent());
    // Anything the app sends next arrives after the reset.
    port.send(note_on(72, 0));
    CHECK(port.sent.size() == 33);
    CHECK(port.sent.back().data == 72);
}
