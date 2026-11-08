// SPDX-License-Identifier: GPL-3.0-or-later
// Sends the song's MIDI at the time each message is due, from a thread of its own, so the timing
// does not depend on the frame loop. libremidi cannot send a message with a timestamp, so this
// thread sleeps until just before the next one is due, waits out the rest and hands it to
// `deliver`.
#pragma once

#include "etude/song_schedule.hpp"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace etude::app {

// submit() and stop() are for one thread (the frame thread); `deliver` runs on the sender's thread.
//
// The queue holds kQueueSize messages. A full queue drops its oldest note-on, never a note-off (see
// DueQueue); should a note-off be lost anyway, everything is stopped with the port reset messages.
class TimedSender {
  public:
    static constexpr std::size_t kQueueSize = 512;
    using Deliver = std::function<void(const PlaybackMessage&)>;

    explicit TimedSender(Deliver deliver);
    ~TimedSender();
    TimedSender(const TimedSender&) = delete;
    TimedSender& operator=(const TimedSender&) = delete;

    // Message: queued for its due time (at_ns, steady clock). CancelUnstarted: drops the queued
    // note-ons due after the event's time and their note-offs. StopAll: forgets the queue and
    // delivers All Notes Off and sustain off on every channel.
    void submit(const SongSoundEvent& event);

    // Delivers what is left in the queue at once, then ends the thread. Nothing is sent after it.
    void stop();

    // Note-ons dropped because the queue was full.
    std::uint64_t dropped_note_ons() const;

  private:
    void run();
    void stop_all_locked(std::unique_lock<std::mutex>& lock);

    Deliver deliver_;
    mutable std::mutex mutex_;
    // Held while anything goes out, so a reset and a message popped just before it can't
    // overtake each other. Lock order: send_mutex_, then mutex_.
    std::mutex send_mutex_;
    // Bumped by every reset; a message popped before a reset is dropped instead of sent after it.
    std::uint64_t epoch_ = 0;
    std::condition_variable wake_;
    DueQueue<kQueueSize> queue_;
    std::uint64_t seen_lost_offs_ = 0;
    bool stopping_ = false;
    bool stopped_ = false;
    std::thread thread_;
};

} // namespace etude::app
