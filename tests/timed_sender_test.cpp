// SPDX-License-Identifier: GPL-3.0-or-later
// The MIDI sender: messages leave at their due time, a cancel keeps unstarted notes from leaving,
// and a full queue gives way as DueQueue says. Times are real, so the bounds are loose.
#include "timed_sender.hpp"
#include "timing_factor.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

constexpr std::int64_t kMs = 1'000'000;

std::int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Sent {
    PlaybackMessage message;
    std::int64_t at_ns = 0;
};

// Collects what the sender delivers, safe to read from the test thread.
class Collector {
  public:
    void operator()(const PlaybackMessage& m) {
        const std::lock_guard<std::mutex> lock(mutex_);
        sent_.push_back({m, steady_ns()});
        changed_.notify_all();
    }
    std::vector<Sent> snapshot() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return sent_;
    }
    // Waits until `count` messages arrived or `timeout_ms` passed.
    bool wait_for(std::size_t count, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                 [&] { return sent_.size() >= count; });
    }

  private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Sent> sent_;
};

SongSoundEvent message(PlaybackMessage::Kind kind, int key, std::int64_t due, int value = 80) {
    SongSoundEvent e;
    e.kind = SongSoundEvent::Kind::Message;
    e.message.kind = kind;
    e.message.data = static_cast<std::uint8_t>(key);
    e.message.value = static_cast<std::uint16_t>(value);
    e.message.at_ns = due;
    return e;
}

SongSoundEvent cancel(std::int64_t at) {
    SongSoundEvent e;
    e.kind = SongSoundEvent::Kind::CancelUnstarted;
    e.message.at_ns = at;
    return e;
}

} // namespace

TEST_CASE("messages leave in due order, none before its time and each soon after it") {
    Collector got;
    TimedSender sender([&](const PlaybackMessage& m) { got(m); });
    const std::int64_t t0 = steady_ns();
    // Handed over out of order, as the controller never does but a sort keeps right.
    sender.submit(message(PlaybackMessage::Kind::NoteOn, 62, t0 + 60 * kMs));
    sender.submit(message(PlaybackMessage::Kind::NoteOn, 60, t0 + 20 * kMs));
    sender.submit(message(PlaybackMessage::Kind::NoteOff, 60, t0 + 40 * kMs));
    REQUIRE(got.wait_for(3, 2000));
    const auto sent = got.snapshot();
    REQUIRE(sent.size() == 3);
    CHECK(sent[0].message.data == 60);
    CHECK(sent[1].message.kind == PlaybackMessage::Kind::NoteOff);
    CHECK(sent[2].message.data == 62);
    for (const Sent& s : sent) {
        CHECK(s.at_ns >= s.message.at_ns);           // never early
        CHECK_TIMING(s.at_ns - s.message.at_ns < 50 * kMs); // and not far behind, even on a busy machine
    }
    sender.stop();
}

TEST_CASE("a cancel keeps the notes that had not started from leaving") {
    Collector got;
    TimedSender sender([&](const PlaybackMessage& m) { got(m); });
    const std::int64_t t0 = steady_ns();
    sender.submit(message(PlaybackMessage::Kind::NoteOn, 60, t0 + 150 * kMs));
    sender.submit(message(PlaybackMessage::Kind::NoteOff, 60, t0 + 170 * kMs));
    sender.submit(message(PlaybackMessage::Kind::NoteOn, 64, t0 + 160 * kMs));
    sender.submit(cancel(t0 + 10 * kMs));
    // The command's own message goes out at once.
    sender.submit(message(PlaybackMessage::Kind::ControlChange, 123, t0 + 10 * kMs, 0));
    REQUIRE(got.wait_for(1, 2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto sent = got.snapshot();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].message.kind == PlaybackMessage::Kind::ControlChange);
    sender.stop();
}

TEST_CASE("stop delivers what is queued at once, then nothing more leaves") {
    Collector got;
    TimedSender sender([&](const PlaybackMessage& m) { got(m); });
    const std::int64_t t0 = steady_ns();
    sender.submit(message(PlaybackMessage::Kind::NoteOff, 60, t0 + 5'000 * kMs));
    sender.stop();
    CHECK(got.snapshot().size() == 1);
    sender.submit(message(PlaybackMessage::Kind::NoteOn, 62, t0));
    sender.stop(); // twice is harmless
    CHECK(got.snapshot().size() == 1);
}

TEST_CASE("StopAll forgets the queue and resets every channel") {
    Collector got;
    TimedSender sender([&](const PlaybackMessage& m) { got(m); });
    const std::int64_t t0 = steady_ns();
    sender.submit(message(PlaybackMessage::Kind::NoteOn, 60, t0 + 200 * kMs));
    SongSoundEvent stop;
    stop.kind = SongSoundEvent::Kind::StopAll;
    sender.submit(stop);
    REQUIRE(got.wait_for(32, 2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto sent = got.snapshot();
    CHECK(sent.size() == 32); // the reset, and not the note
    sender.stop();
}

TEST_CASE("a full queue drops note-ons and keeps the note-offs") {
    Collector got;
    TimedSender sender([&](const PlaybackMessage& m) { got(m); });
    const std::int64_t t0 = steady_ns() + 3'000 * kMs; // far enough that nothing leaves yet
    for (std::size_t i = 0; i < TimedSender::kQueueSize / 2; ++i) {
        sender.submit(message(PlaybackMessage::Kind::NoteOff, static_cast<int>(i % 128),
                              t0 + static_cast<std::int64_t>(i) * 1000));
    }
    for (std::size_t i = 0; i < TimedSender::kQueueSize; ++i) {
        sender.submit(message(PlaybackMessage::Kind::NoteOn, static_cast<int>(i % 128),
                              t0 + 1'000'000 + static_cast<std::int64_t>(i) * 1000));
    }
    CHECK(sender.dropped_note_ons() == TimedSender::kQueueSize / 2);
    sender.stop();
    const auto sent = got.snapshot();
    std::size_t offs = 0;
    for (const Sent& s : sent) {
        offs += s.message.kind == PlaybackMessage::Kind::NoteOff ? 1 : 0;
    }
    CHECK(offs == TimedSender::kQueueSize / 2);
    CHECK(sent.size() == TimedSender::kQueueSize);
}
