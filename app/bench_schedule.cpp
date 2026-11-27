// SPDX-License-Identifier: GPL-3.0-or-later
#include "bench_schedule.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace etude::app {

namespace {

constexpr double kAvgNoteSeconds = 0.55;
// A chord of 3 or 4, a melody note, a bass note every 4th beat: about 5 notes a beat, measured
// down to 4.1 so the count on screen lands near the asked-for number.
constexpr double kNotesPerBeat = 4.1;

class Rng {
  public:
    double next() {
        state_ = state_ * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state_ >> 11) / 9007199254740992.0;
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() * (hi - lo + 1)); }

  private:
    std::uint64_t state_ = 0x243f6a8885a308d3ull;
};
// A shot depends on its arguments only: its clock starts at a fixed time (the UI's animations read
// the absolute time) and the UI reads no wall clock (`ui::exact_shot`, set in `run_bench_mode`).
constexpr std::int64_t kShotEpochNs = 10'000'000'000;

} // namespace

Schedule make_schedule(int notes, double visible_seconds, double total_seconds, KeyRange range) {
    Schedule s;
    const double rate = std::max(1.0, notes / (visible_seconds + kAvgNoteSeconds));
    s.beat = kNotesPerBeat / rate;
    Rng rng;
    const auto add = [&](int note, double start, double length, int velocity) {
        note = std::clamp(note, range.lowest, range.highest);
        const auto on = static_cast<std::int64_t>(start * 1e9);
        const auto off = static_cast<std::int64_t>((start + std::max(0.06, length)) * 1e9);
        s.events.push_back({on, MidiEventType::NoteOn, 0, static_cast<std::uint8_t>(note),
                            static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)), false});
        s.events.push_back(
            {off, MidiEventType::NoteOff, 0, static_cast<std::uint8_t>(note), 0, false});
    };
    const int mid = (range.lowest + range.highest) / 2;
    int beat_index = 0;
    for (double t = kFirstBeat; t < total_seconds; t += s.beat, ++beat_index) {
        // Chord: a triad with the octave on top, major or minor, sometimes held long.
        const int root = std::clamp(mid - 12 + rng.range(-5, 7), range.lowest, range.highest - 12);
        const int third = rng.next() < 0.5 ? 4 : 3;
        const bool long_hold = beat_index % 3 == 0;
        const double length = long_hold ? s.beat * (2.5 + rng.next()) : s.beat * (0.5 + rng.next());
        const int velocity = rng.range(60, 120);
        add(root, t, length, velocity);
        add(root + third, t, length, velocity);
        add(root + 7, t, length, velocity);
        if (rng.next() < 0.75) {
            add(root + 12, t, length, velocity);
        }
        // Melody between the beats, above the chord.
        add(mid + rng.range(4, 26), t + 0.5 * s.beat, s.beat * (0.3 + 0.5 * rng.next()),
            rng.range(50, 127));
        if (beat_index % 4 == 0) {
            add(range.lowest + rng.range(4, 20), t, s.beat * 3.5, rng.range(70, 110));
        }
    }
    std::stable_sort(s.events.begin(), s.events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.time_ns != b.time_ns) {
            return a.time_ns < b.time_ns;
        }
        return a.type == MidiEventType::NoteOff && b.type != MidiEventType::NoteOff;
    });
    return s;
}

std::int64_t clock_origin(bool shot) {
    return shot ? kShotEpochNs : static_cast<std::int64_t>(SDL_GetTicksNS());
}


// A fast run up the C major scale from middle C (ten notes, 70 ms apart, short), or a C major
// chord with the octave on top, held; the last note-on at kPatternEndSeconds.
Schedule make_pattern(const std::string& pattern) {
    Schedule s;
    const auto add = [&](int note, double start, double length, int velocity) {
        const auto on = static_cast<std::int64_t>(start * 1e9);
        const auto off = static_cast<std::int64_t>((start + length) * 1e9);
        s.events.push_back({on, MidiEventType::NoteOn, 0, static_cast<std::uint8_t>(note),
                            static_cast<std::uint8_t>(velocity), false});
        s.events.push_back(
            {off, MidiEventType::NoteOff, 0, static_cast<std::uint8_t>(note), 0, false});
    };
    if (pattern == "run") {
        static constexpr int kScale[] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76};
        const double step = 0.07;
        double t = kPatternEndSeconds - step * static_cast<double>(std::size(kScale) - 1);
        for (const int note : kScale) {
            add(note, t, 0.12, 96);
            t += step;
        }
    } else if (pattern.rfind("song-", 0) == 0) {
        // Falling song notes only (song_pattern): nothing is pressed.
    } else {
        for (const int note : {60, 64, 67, 72}) {
            add(note, kPatternEndSeconds, 0.5, 100);
        }
    }
    std::stable_sort(s.events.begin(), s.events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.time_ns != b.time_ns) {
            return a.time_ns < b.time_ns;
        }
        return a.type == MidiEventType::NoteOff && b.type != MidiEventType::NoteOff;
    });
    return s;
}

// The song patterns: notes falling toward the keys, none pressed, each hand in its own colour (left
// below middle C). song-chromatic: every key from G3 to C5, one after another; song-chord: a chord
// in each hand on black and white keys, then another; song-run: a fast run in both hands. The
// note's start and length are in seconds before it reaches the hit line.
std::vector<SongBar> song_pattern(const std::string& pattern, float visible_seconds) {
    std::vector<SongBar> bars;
    const auto add = [&](int key, float start, float length) {
        SongBar b;
        b.key = static_cast<std::uint8_t>(key);
        b.velocity = 96;
        b.hand = key < 60 ? Hand::Left : Hand::Right;
        b.bottom = start / visible_seconds;
        b.top = (start + length) / visible_seconds;
        bars.push_back(b);
    };
    if (pattern == "song-chromatic") {
        for (int key = 55; key <= 72; ++key) {
            add(key, 0.12f + 0.13f * static_cast<float>(key - 55), 0.12f);
        }
    } else if (pattern == "song-chord") {
        for (const int key : {37, 44, 61, 65, 68, 73}) {
            add(key, 0.2f, 1.0f);
        }
        for (const int key : {41, 48, 60, 65, 69, 72}) {
            add(key, 1.45f, 0.8f);
        }
    } else if (pattern == "song-run") {
        static constexpr int kRight[] = {66, 68, 70, 71, 73, 75, 76, 78, 80, 82, 83, 85};
        static constexpr int kLeft[] = {42, 44, 46, 47, 49, 51, 52, 54, 56, 58, 59, 61};
        for (std::size_t i = 0; i < std::size(kRight); ++i) {
            const float t = 0.25f + 0.1f * static_cast<float>(i);
            add(kRight[i], t, 0.09f);
            add(kLeft[i] - 12, t, 0.09f);
        }
    }
    return bars;
}


Stats summarise(std::vector<std::int64_t>& samples) {
    Stats st;
    if (samples.empty()) {
        return st;
    }
    std::sort(samples.begin(), samples.end());
    st.frames = samples.size();
    st.min = ms(samples.front());
    st.max = ms(samples.back());
    st.median = ms(samples[samples.size() / 2]);
    st.p95 = ms(samples[std::min(samples.size() - 1, samples.size() * 95 / 100)]);
    return st;
}

} // namespace etude::app
