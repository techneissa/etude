// SPDX-License-Identifier: GPL-3.0-or-later
// When the song's sound is due and what cancels it. The song hands its sound to the audio thread
// or the MIDI sender before it is due (look_ahead.hpp), each message with the time it must start,
// so a slow frame cannot make a note late. Everything here is plain data and arithmetic: the clocks
// and threads that carry it out live in app/.
//
// Rules, each with a test in tests/song_schedule_test.cpp:
// - A message's due time is the time its note reaches the hit line (the exact time the song clock
//   gives it); nothing is added to it. How far ahead the song hands notes over is the look-ahead,
//   kSongSoundLeadNs by default.
// - A command that stops or moves the song queues a CancelUnstarted event with its time T. The
//   receiver drops every note-on due at or after T (the note has not started) with the note-off
//   queued to end it, and every sustain press due at or after T (the command's own messages lift
//   the pedal). What else is queued and due after T (note-offs of notes that started, a pedal
//   release, controllers) is due at T instead: it is sent at once, so what had started is stopped
//   now and nothing is left to fire later. Nothing is dropped but those pairs and presses.
// - A queue that is full drops its oldest note-on, never a note-off.
#pragma once

#include "etude/midi_event.hpp"
#include "etude/playback.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// How far ahead of its due time a note of the song is handed over. It must exceed the longest frame
// the song should survive without a late note; 30 ms covers a frame at 30 FPS.
inline constexpr std::int64_t kSongSoundLeadNs = 30'000'000;

// One thing the song asks of its sound, in the order it asked.
struct SongSoundEvent {
    enum class Kind : std::uint8_t {
        Message,         // `message` is due at message.at_ns (steady-clock ns)
        CancelUnstarted, // a command at message.at_ns: drop the notes due after it
        StopAll,         // stop every note the song started and forget whatever is queued
    };
    Kind kind = Kind::Message;
    PlaybackMessage message{};
};

constexpr bool starts_note(const PlaybackMessage& m) {
    return m.kind == PlaybackMessage::Kind::NoteOn && m.value > 0;
}
constexpr bool ends_note(const PlaybackMessage& m) {
    return m.kind == PlaybackMessage::Kind::NoteOff ||
           (m.kind == PlaybackMessage::Kind::NoteOn && m.value == 0);
}

constexpr bool presses_pedal(const PlaybackMessage& m) {
    return m.kind == PlaybackMessage::Kind::ControlChange && m.data == kCcSustain && m.value >= 64;
}

// Messages waiting for their due time, oldest first, in fixed storage: no allocation, no lock, no
// clock. One owner uses it (the audio thread, or the MIDI sender under its lock).
template <std::size_t Capacity> class DueQueue {
    static_assert(Capacity >= 2, "a queue needs room for a note-on and its note-off");

  public:
    bool empty() const { return head_ == tail_; }
    std::size_t size() const { return tail_ - head_; }
    const PlaybackMessage& front() const { return items_[head_]; }
    const PlaybackMessage& operator[](std::size_t i) const { return items_[head_ + i]; }
    void pop_front() {
        if (head_ != tail_) {
            ++head_;
        }
        if (head_ == tail_) {
            head_ = tail_ = 0;
        }
    }
    void clear() { head_ = tail_ = 0; }

    // Adds `m` in due order (equal times keep their arrival order). When full, the oldest queued
    // note-on goes first; with none, a new note-on is dropped, else the oldest message that is not
    // a note-off. Returns false when `m` itself was not queued.
    bool push(const PlaybackMessage& m) {
        if (tail_ == Capacity) {
            compact();
        }
        if (size() == Capacity && !make_room(m)) {
            return false;
        }
        std::size_t pos = tail_;
        while (pos > head_ && items_[pos - 1].at_ns > m.at_ns) {
            items_[pos] = items_[pos - 1];
            --pos;
        }
        items_[pos] = m;
        ++tail_;
        return true;
    }

    // Drops the note-ons and pedal presses due at or after `at_ns` and the note-offs queued to end
    // those notes; what is left and due after `at_ns` becomes due at `at_ns`. Returns how many
    // messages went.
    std::size_t cancel_unstarted(std::int64_t at_ns) {
        std::array<std::uint8_t, 16 * 128> dropped_on{};
        std::size_t write = head_;
        for (std::size_t read = head_; read < tail_; ++read) {
            const PlaybackMessage m = items_[read];
            const std::size_t key = static_cast<std::size_t>(m.channel & 15) * 128 + (m.data & 127);
            if (starts_note(m) && m.at_ns >= at_ns) {
                if (dropped_on[key] < 255) {
                    ++dropped_on[key];
                }
                continue;
            }
            if (presses_pedal(m) && m.at_ns >= at_ns) {
                continue;
            }
            if (ends_note(m) && dropped_on[key] > 0) {
                --dropped_on[key];
                continue;
            }
            items_[write] = m;
            items_[write].at_ns = std::min(m.at_ns, at_ns);
            ++write;
        }
        const std::size_t removed = tail_ - write;
        tail_ = write;
        if (head_ == tail_) {
            head_ = tail_ = 0;
        }
        return removed;
    }

    // Note-ons dropped because the queue was full.
    std::uint64_t dropped_note_ons() const { return dropped_note_ons_; }
    // Note-offs dropped because it was full of note-offs; the owner must stop everything it
    // started when this moves, or a note could stay on.
    std::uint64_t lost_note_offs() const { return lost_note_offs_; }

  private:
    void compact() {
        if (head_ == 0) {
            return;
        }
        for (std::size_t i = head_; i < tail_; ++i) {
            items_[i - head_] = items_[i];
        }
        tail_ -= head_;
        head_ = 0;
    }
    void erase(std::size_t index) {
        for (std::size_t i = index + 1; i < tail_; ++i) {
            items_[i - 1] = items_[i];
        }
        --tail_;
    }
    bool make_room(const PlaybackMessage& incoming) {
        for (std::size_t i = head_; i < tail_; ++i) {
            if (starts_note(items_[i])) {
                erase(i);
                ++dropped_note_ons_;
                return true;
            }
        }
        if (starts_note(incoming)) {
            ++dropped_note_ons_;
            return false;
        }
        for (std::size_t i = head_; i < tail_; ++i) {
            if (!ends_note(items_[i])) {
                erase(i);
                return true;
            }
        }
        if (ends_note(incoming)) {
            ++lost_note_offs_;
        }
        return false;
    }

    std::array<PlaybackMessage, Capacity> items_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::uint64_t dropped_note_ons_ = 0;
    std::uint64_t lost_note_offs_ = 0;
};

// What a piano is sent when its output port comes back after a drop-out: All Notes Off (CC123) then
// sustain off (CC64 0) on each of the 16 channels, channel 0 first. A note held across the
// drop-out cannot stay on, and the first thing the piano hears is this.
inline constexpr std::size_t kPortResetMessages = 32;
std::array<PlaybackMessage, kPortResetMessages> port_reset_messages();

// Tells a port that came back from one that is new: remembers the name of the output that dropped
// out, and reports it when a port of that name opens again.
class PortReconnect {
  public:
    void lost(std::string_view name) { lost_name_ = name; }
    // True once for the port that dropped out; false for any other, and for a first connection.
    bool reconnected(std::string_view name) {
        if (!lost_name_.empty() && lost_name_ == name) {
            lost_name_.clear();
            return true;
        }
        return false;
    }

  private:
    std::string lost_name_;
};

} // namespace etude
