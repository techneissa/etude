// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the harmony_*test.cpp files (split from harmony_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/harmony.hpp"
#include "etude/musicxml.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace harmony_test_support {

using namespace etude;

constexpr std::int64_t kQuarter = 480;

// A song of `bars` bars in `numerator`/`denominator`, notes added by bar and beat (in quarter notes
// from the bar's start). The song is read the way the loaders leave it: sorted, with its bars.
class Piece {
  public:
    explicit Piece(int bars, int numerator = 4, int denominator = 4)
        : bar_ticks_(kQuarter * 4 * numerator / denominator) {
        song_.tempo.ticks_per_quarter = static_cast<int>(kQuarter);
        song_.time_signatures.push_back(
            {0, static_cast<std::uint8_t>(numerator), static_cast<std::uint8_t>(denominator)});
        for (int b = 0; b < bars; ++b) {
            Measure m;
            m.number = b + 1;
            m.start_tick = b * bar_ticks_;
            m.start_s = static_cast<double>(m.start_tick) / (2.0 * kQuarter); // 120 bpm
            m.numerator = static_cast<std::uint8_t>(numerator);
            m.denominator = static_cast<std::uint8_t>(denominator);
            song_.measures.push_back(m);
        }
        song_.end_tick = bars * bar_ticks_;
        song_.duration_s = static_cast<double>(song_.end_tick) / (2.0 * kQuarter);
    }

    Piece& note(int bar, double beat, int pitch, double length, Hand hand = Hand::Right) {
        Note n;
        n.start_tick = bar * bar_ticks_ + static_cast<std::int64_t>(beat * kQuarter);
        n.end_tick = n.start_tick + static_cast<std::int64_t>(length * kQuarter);
        n.start_s = static_cast<double>(n.start_tick) / (2.0 * kQuarter);
        n.duration_s = static_cast<double>(n.end_tick - n.start_tick) / (2.0 * kQuarter);
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.hand = hand;
        n.measure = bar + 1;
        song_.notes.push_back(n);
        return *this;
    }
    // Notes struck together.
    Piece& chord(int bar, double beat, std::initializer_list<int> pitches, double length,
                 Hand hand = Hand::Left) {
        for (const int p : pitches) {
            note(bar, beat, p, length, hand);
        }
        return *this;
    }
    // A chord held for the whole bar.
    Piece& whole(int bar, std::initializer_list<int> pitches, Hand hand = Hand::Left) {
        return chord(bar, 0.0, pitches, 3.9, hand);
    }
    Piece& key(int fifths, KeyMode mode = KeyMode::Major) {
        song_.keys.push_back(key_from_signature(0, fifths, mode));
        return *this;
    }
    Song& song() {
        std::stable_sort(song_.notes.begin(), song_.notes.end(), [](const Note& a, const Note& b) {
            return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
        });
        return song_;
    }

  private:
    Song song_;
    std::int64_t bar_ticks_;
};

inline std::string name_at(const Song& song, HarmonyScope scope, int bar, double beat = 0.0) {
    const std::vector<HarmonyStretch> stretches = analyse_harmony(song, scope);
    const std::int64_t tick = (bar - 1) * 4 * kQuarter + static_cast<std::int64_t>(beat * kQuarter);
    const HarmonyStretch* s = harmony_at_tick(stretches, tick);
    return s != nullptr && s->chord.found ? harmony_name(*s) : "-";
}

inline std::string all_at(Piece& p, int bar, double beat = 0.0) {
    return name_at(p.song(), HarmonyScope::All, bar, beat);
}

constexpr int C2 = 36, G2 = 43, C3 = 48, D3 = 50, E3 = 52, F3 = 53, G3 = 55, A3 = 57, B3 = 59,
              C4 = 60, D4 = 62, E4 = 64, F4 = 65, G4 = 67, A4 = 69, C5 = 72, D5 = 74, E5 = 76;

} // namespace harmony_test_support
