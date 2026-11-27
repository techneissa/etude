// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scene_types.hpp"

#include "etude/keyboard.hpp"
#include "etude/midi_event.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etude::app {

constexpr std::int64_t kSecond = 1'000'000'000;
constexpr double kWarmupSeconds = 0.5; // after the screen has filled (the visible seconds)
constexpr double kFirstBeat = 0.2;     // seconds

// A fixed, made-up piece: chords on every beat, a melody note between them, a held bass note every
// fourth beat and a long held chord every third. Sized so about `notes` blocks are on screen.
struct Schedule {
    std::vector<MidiEvent> events; // sorted by time, relative to the start
    double beat = 0.5;
};

Schedule make_schedule(int notes, double visible_seconds, double total_seconds, KeyRange range);

std::int64_t clock_origin(bool shot);

// The screenshot patterns: the last note-on lands on this 60 Hz frame time.
constexpr double kPatternEndSeconds = 2.0;

Schedule make_pattern(const std::string& pattern);

std::vector<SongBar> song_pattern(const std::string& pattern, float visible_seconds);

inline double ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }

struct Stats {
    std::size_t frames = 0;
    double min = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double max = 0.0;
};

Stats summarise(std::vector<std::int64_t>& samples);

} // namespace etude::app
