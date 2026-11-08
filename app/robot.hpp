// SPDX-License-Identifier: GPL-3.0-or-later
// The developer's stand-in player (--robot), for checking the practice modes on screen.
#pragma once

#include "song_controller.hpp"

#include "etude/midi_event.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace etude::app {

// Developer stand-in for a player (--robot), for checking the practice modes on screen: in Flow
// Mode it presses each practised note as it reaches the hit line, most on time, some late, and
// misses every eleventh; in Wait Mode it presses the waiting chord after 0.4 s.
class Robot {
  public:
    void update(const etude::app::SongController& songs, std::int64_t now,
                std::vector<MidiEvent>& out) {
        if (!songs.has_song()) {
            return;
        }
        const double pos = songs.position_s();
        if (songs.mode() == etude::PracticeMode::Wait) {
            if (songs.waiting()) {
                if (waiting_since_ == 0) {
                    waiting_since_ = now;
                } else if (now - waiting_since_ > 400'000'000 && !pressed_) {
                    for (const auto& k : songs.waiting_keys()) {
                        queue(k.key, now, now + 150'000'000);
                    }
                    pressed_ = true;
                }
            } else {
                waiting_since_ = 0;
                pressed_ = false;
            }
        } else if (songs.playing() && last_pos_ > -0.5 && pos > last_pos_ &&
                   pos - last_pos_ < 0.5) { // not across a seek
            const auto& notes = songs.song().song.notes;
            auto it =
                std::lower_bound(notes.begin(), notes.end(), last_pos_,
                                 [](const etude::Note& n, double s) { return n.start_s <= s; });
            const double rate = songs.speed_percent() / 100.0;
            for (; it != notes.end() && it->start_s <= pos; ++it) {
                const auto i = static_cast<std::size_t>(it - notes.begin());
                const etude::HandFilter h = songs.applied_hands();
                if (it->drum || it->accompaniment ||
                    (h == etude::HandFilter::Left && it->hand != etude::Hand::Left) ||
                    (h == etude::HandFilter::Right && it->hand != etude::Hand::Right)) {
                    continue;
                }
                if (i % 11 == 5) {
                    continue; // a miss
                }
                const std::int64_t late = i % 4 == 1   ? 40'000'000
                                          : i % 4 == 3 ? 62'000'000
                                                       : 0;
                const auto arrival =
                    now - static_cast<std::int64_t>((pos - it->start_s) / rate * 1e9);
                const int key = songs.transposition().expected_pitch(*it).pitch;
                const auto length =
                    static_cast<std::int64_t>(std::min(0.4, it->duration_s / rate * 0.8) * 1e9);
                if (key >= 0) {
                    queue(key, arrival + late,
                          arrival + late + std::max<std::int64_t>(length, 50'000'000));
                }
            }
        }
        last_pos_ = pos;
        std::stable_sort(
            pending_.begin(), pending_.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.time_ns < b.time_ns; });
        std::size_t n = 0;
        while (n < pending_.size() && pending_[n].time_ns <= now) {
            out.push_back(pending_[n]);
            ++n;
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
    }

  private:
    void queue(int key, std::int64_t on, std::int64_t off) {
        MidiEvent ev;
        ev.type = etude::MidiEventType::NoteOn;
        ev.data1 = static_cast<std::uint8_t>(key);
        ev.data2 = 80;
        ev.time_ns = on;
        ev.source = etude::InputSource::Pointer;
        pending_.push_back(ev);
        ev.type = etude::MidiEventType::NoteOff;
        ev.data2 = 0;
        ev.time_ns = off;
        pending_.push_back(ev);
    }

    std::vector<MidiEvent> pending_;
    double last_pos_ = -1.0;
    std::int64_t waiting_since_ = 0;
    bool pressed_ = false;
};

} // namespace etude::app
