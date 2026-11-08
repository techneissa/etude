// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of harmony.cpp share: the weighing of a stretch's notes (harmony_stats.cpp), the
// fit of a chord to it (harmony_fit.cpp) and the stretches built from both (harmony.cpp). Not part
// of the library's interface.
#pragma once

#include "etude/harmony.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace etude {

// A stretch must sound a pitch class this many times, or hold it this many beats (the other
// bound is in harmony_stats.cpp), before the pitch class counts at all.
constexpr int kEstablishedOnsets = 2;
// The fewest pitch classes a stretch needs before it names a chord, and the fewest that sound
// together or come back (the rest may be a tune).
constexpr int kMinEstablished = 3;
constexpr int kMinStable = 2;

struct Frame {
    std::int64_t t0 = 0;
    std::int64_t t1 = 0;
    std::int64_t bar_t0 = 0;      // where beats are counted from
    std::int64_t group_ticks = 1; // one beat (a dotted beat in 6/8)
    int groups = 4;               // beats in the bar
};

struct PcStat {
    double weight = 0.0;
    double longest = 0.0; // beats of the longest note of the pitch class in the stretch
    int onsets = 0;
    bool established = false;
    bool together = false; // sounds with a note of another pitch class for a while
    bool stable = false;   // established, and either sounds together or comes back
};

struct Stats {
    std::array<PcStat, 12> pc{};
    double total = 0.0; // the weight of the established pitch classes
    int established = 0;
    int stable = 0;
    int bass_pc = -1;
    double sounding = 0.0; // the share of the stretch in which any note sounds, 0..1
};

// The stretch's notes weighed. Linear in the notes of the stretch (and twelve times the notes for
// the simultaneity sweep).
Stats stats_of(const Song& song, const std::vector<std::uint32_t>& notes, const Frame& bar,
               std::int64_t t0, std::int64_t t1);

// The notes of a stretch that are its tune, sorted by index: the highest note sounding when it
// starts, the only start of its pitch, with the next voice below it at least an octave down.
std::vector<std::uint32_t> tune_of(const Song& song, const std::vector<std::uint32_t>& notes,
                                   std::int64_t t0, std::int64_t t1);
std::vector<std::uint32_t> top_voice_of(const Song& song, const std::vector<std::uint32_t>& notes,
                                        std::int64_t t0, std::int64_t t1);
bool stepwise_line(const Song& song, const std::vector<std::uint32_t>& notes, const Frame& bar,
                   std::int64_t t0, std::int64_t t1);

struct Fit {
    bool found = false;
    int root = 0;
    ChordQuality quality = ChordQuality::None;
    unsigned tones = 0; // the chord's tones as a mask of pitch classes
    int bass_pc = 0;    // the chord's bass: the stretch's, when it is a tone of the chord
    double score = -1e9;
    double coverage = 0.0; // the share of the weight the chord's tones cover
    double margin = 0.0;   // the lead over the best different chord
};

unsigned rotate_mask(unsigned shape, int root);
bool plays_full_chord(const Stats& st, int root, ChordQuality quality);
bool same_chord(const Fit& a, const Fit& b);
Fit fit_chord(const Stats& st);
Fit fit_held(const Stats& st, int root, ChordQuality quality);
float confidence_of(const Fit& f);

} // namespace etude
