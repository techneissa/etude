// SPDX-License-Identifier: GPL-3.0-or-later
// How hard a song is to play, bar by bar and as a whole, on a scale of 1 to 5 stars in halves.
// Pure values: a loader thread rates a song once when it opens (or when the library scans it) and
// everything after reads the numbers.
//
// A bar's rating is made of eight things, each a share 0..1 of what would make that part of the
// bar hard, added by weight (`kDifficultyWeight`):
//   speed         distinct onsets a second (both hands together)
//   chords        notes a hand plays at once, the mean and the largest
//   span          the widest chord a hand has to reach
//   reach         how far one hand's next five onsets lie apart (a position it cannot hold)
//   leaps         jumps of a hand between one onset and the next (carried over the bar line)
//   independence  the sparser hand's onsets that the other hand does not play with
//   colour        black keys, and notes outside the key
//   tempo         the quarter-note rate, counted only as far as the beat is divided in onsets
// The sum 0..1 is bent into 1..5 stars (`bar_stars`): a harder bar never rates lower.
//
// A song's rating is made of its hardest stretches, not its average: stretches are two bars, and
// the song takes the mean of its hardest share (`kHardestShare`, at least one stretch). A piece
// that is easy but for a few difficult bars rates by those bars.
#pragma once

#include "etude/song.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace etude {

inline constexpr float kMinStars = 1.0f;
inline constexpr float kMaxStars = 5.0f;
// The song's rating in half stars: 2 (one star) to 10 (five).
inline constexpr int kMinHalfStars = 2;
inline constexpr int kMaxHalfStars = 10;

// A stretch is this many bars; a song takes the mean of the hardest `kHardestShare` of its
// stretches.
inline constexpr int kStretchBars = 2;
inline constexpr double kHardestShare = 0.15;

// Notes starting within this of each other are one onset of a hand (a chord).
inline constexpr double kOnsetWindowS = 0.040;
// A bar is never counted as shorter than this when its speed is worked out: a sliver of a bar
// (the last, cut short) must not look fast.
inline constexpr double kMinBarSeconds = 0.4;

// What a bar is worth to each part, summing to 1.
namespace difficulty_tok {
inline constexpr double kSpeed = 0.36;
inline constexpr double kChords = 0.10;
inline constexpr double kSpan = 0.10;
inline constexpr double kReach = 0.06;
inline constexpr double kLeaps = 0.12;
inline constexpr double kIndependence = 0.12;
inline constexpr double kColour = 0.08;
inline constexpr double kTempo = 0.06;
// Where each part is full (1): 12 onsets a second; 5 notes in a hand; a 16-semitone chord; five
// onsets 21 semitones apart; a 24-semitone leap; 140 quarter notes a minute.
inline constexpr double kFullOnsetsPerSecond = 12.0;
inline constexpr double kFullChordNotes = 5.0;
inline constexpr double kFreeSpan = 7.0; // a fifth is a hand's rest position
inline constexpr double kFullSpan = 16.0;
inline constexpr double kFreeReach = 9.0;
inline constexpr double kFullReach = 21.0;
inline constexpr double kFreeLeap = 7.0;
inline constexpr double kFullLeap = 24.0;
inline constexpr double kFreeBpm = 60.0;
inline constexpr double kFullBpm = 140.0;
// The sum of the parts that makes a bar five stars; a real piece never fills every part.
inline constexpr double kFullSum = 0.80;
// The bend of the curve from the sum to the stars (1: straight).
inline constexpr double kCurve = 0.8;
} // namespace difficulty_tok

// What was measured in one bar. Every field is 0 for a bar with no notes.
struct BarFeatures {
    double onsets_per_second = 0.0; // distinct onsets, both hands, over the bar's length
    double mean_chord_notes = 0.0;  // notes per onset of a hand; 1 for single notes
    int max_chord_notes = 0;
    double max_span = 0.0;         // semitones, the widest chord of one hand
    double max_reach = 0.0;        // semitones, the widest run of five onsets of one hand
    double max_leap = 0.0;         // semitones between two onsets of one hand
    int big_leaps = 0;             // leaps of a seventh or more
    double independence = 0.0;     // 0..1, see above
    double black_share = 0.0;      // 0..1 of the bar's notes
    double accidental_share = 0.0; // 0..1 of the bar's notes outside the key
    double bpm = 0.0;              // quarter notes a minute at the bar's start
    int notes = 0;
};

// The parts of a bar, each 0..1.
struct BarParts {
    double speed = 0.0;
    double chords = 0.0;
    double span = 0.0;
    double reach = 0.0;
    double leaps = 0.0;
    double independence = 0.0;
    double colour = 0.0;
    double tempo = 0.0;
};
BarParts bar_parts(const BarFeatures& f);
// The weighted sum of the parts, 0..1.
double bar_sum(const BarParts& parts);
// 1 for an empty bar; else 1..5 stars from the sum. Never falls as the sum rises.
float bar_stars(const BarFeatures& f);
float stars_from_sum(double sum);

// A song's rating from its bars' stars: the mean of the hardest stretches. Empty: 1 star.
float song_stars(std::span<const float> bars);
// Half stars, 2..10, nearest the stars.
int half_stars_of(float stars);
// The whole-star level 1..5 the library sorts and filters by: the half stars rounded half up.
int level_of_half_stars(int half_stars);

struct SongDifficulty {
    // One rating per bar of the song, in the order of `Song::measures`, 1..5.
    std::vector<float> bars;
    std::vector<int> numbers;          // Measure::number of each bar, ascending
    std::vector<BarFeatures> features; // the same bars, for tests and the report
    float stars = 1.0f;                // song_stars(bars)
    int half_stars = kMinHalfStars;    // half_stars_of(stars)

    // The rating of the bar with this Measure::number; 0 when the song has no such bar.
    float bar_at(int measure_number) const;
    bool empty() const { return bars.empty(); }
};

// Rates `song`. Hands should be split already: a note with no hand is read as the right hand's,
// so a song that was never split is rated as one hand playing everything. Playable notes only:
// drums and accompaniment parts are left out. Allocates; never called per frame.
SongDifficulty rate_song(const Song& song);

// The bars to suggest practising: the hardest of `difficulty`, hardest first, a tie to the earlier
// bar, at most `max`, each at least `min_stars` and within `kSuggestBelowPeak` of the song's
// hardest bar. Returns the Measure::number of each.
inline constexpr float kSuggestBelowPeak = 1.0f;
inline constexpr float kSuggestMinStars = 2.0f;
std::vector<int> hardest_bars(const SongDifficulty& difficulty, int max,
                              float min_stars = kSuggestMinStars);

} // namespace etude
