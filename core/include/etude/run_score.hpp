// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/run_record.hpp"

namespace etude {

// The points of one run: a number that is fun to beat, fair across speeds and songs. Pure and
// deterministic; the history keeps it with the run (RunRecord::score), and a run from before scores
// is worked out from its record alone with the same function.
//
//   credit  = (1 x perfect + 0.8 x good + 0.6 x (early + late) + 0.85 x played - 0.25 x wrong)
//             / graded notes, kept to 0 .. 1 (Wait Mode grades no timing, so its notes are `played`
//             and the mode tops out under Flow Mode's)
//   flow    = longest combo / graded notes
//   balance = the weaker hand's accuracy when both hands were graded, else the run's accuracy
//   score   = 1000 x (0.70 credit + 0.20 flow + 0.10 balance)
//             x speed x difficulty x hands x coverage
//
//   speed       25 .. 100%: 0.25 + 0.75 x speed / 100 (50% is 0.625, 25% is 0.4375: slower earns
//               less and never nothing); above 100%: 1 + 0.2 x (speed - 100) / 100 (200% is 1.2)
//   difficulty  the song's rating in stars, 1 .. 5, 0.8 + 0.1 x (stars - 1) (0.8 .. 1.2); a song
//               with no rating counts as 1
//   hands       1 for both hands, 0.8 for one hand
//   coverage    0.2 + 0.8 x the share of the song the run covered: a loop of a few bars or a run
//               stopped half way earns a part of a whole pass, and a loop cannot be repeated into
//               more than a pass
//
// A run that graded no note has no score (0), not a perfect one. The most a run can earn is
// kMaxRunScore (1440): every note perfect, 200%, a five-star song, both hands, the whole song.

inline constexpr double kCreditPerfect = 1.0;
inline constexpr double kCreditGood = 0.8;
inline constexpr double kCreditTimingOff = 0.6; // early or late
inline constexpr double kCreditPlayed = 0.85;   // Wait Mode
inline constexpr double kWrongPressCost = 0.25; // of one note's credit
inline constexpr double kWeightCredit = 0.70;
inline constexpr double kWeightFlow = 0.20;
inline constexpr double kWeightBalance = 0.10;
inline constexpr double kOneHandFactor = 0.8;
inline constexpr double kCoverageFloor = 0.2;
inline constexpr int kScoreScale = 1000;

// What the app knows of the song when a run ends; 0 is not known.
struct ScoreContext {
    int song_notes = 0;           // the notes of the practised hands in the whole song
    int song_bars = 0;            // the bars of the whole song
    int difficulty_half_stars = 0; // rate_song's rating: 2 .. 10 half stars (1 to 5 stars)
};

// The factors of a score, for a test and for a screen that shows where the points came from.
struct ScoreParts {
    double credit = 0.0;
    double flow = 0.0;
    double balance = 0.0;
    double speed = 1.0;
    double difficulty = 1.0;
    double hands = 1.0;
    double coverage = 1.0; // the share of the song covered, 0 .. 1
    int score = 0;
};

double speed_factor(int speed_percent);
// 1 for 0 (not rated); else 0.8 + 0.1 x (stars - 1) with the stars kept to 1 .. 5.
double difficulty_factor(int difficulty_half_stars);

// The run's score and its parts. A context that knows the song gives the coverage; without one the
// record's own `covered_percent` does, and a record that has none counts as a whole pass.
ScoreParts score_parts(const RunRecord& run, const ScoreContext& context = {});

// The run with its `score` and `covered_percent` set from the context.
RunRecord scored(const RunRecord& run, const ScoreContext& context);

} // namespace etude
