// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/run_score.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

double credit_of(const RunRecord& run, double graded) {
    const RunCounts& c = run.counts;
    const double points = kCreditPerfect * c.perfect + kCreditGood * c.good +
                          kCreditTimingOff * (c.early + c.late) + kCreditPlayed * c.played -
                          kWrongPressCost * run.wrong;
    return clamp01(points / graded);
}

double balance_of(const RunRecord& run) {
    if (run.left.graded > 0 && run.right.graded > 0) {
        return std::min(*run.left.accuracy(), *run.right.accuracy());
    }
    return run.all.accuracy().value_or(0.0);
}

// The share of the song covered: the smaller of the notes graded (a loop's repeated passes
// cannot count past the song) and the bars touched, against what the context knows of the song.
double coverage_of(const RunRecord& run, const ScoreContext& context) {
    if (context.song_notes <= 0 && context.song_bars <= 0) {
        return run.covered_percent > 0 ? run.covered_percent / 100.0 : 1.0;
    }
    double share = 1.0;
    if (context.song_notes > 0) {
        share = std::min(share, static_cast<double>(run.all.graded) / context.song_notes);
    }
    if (context.song_bars > 0 && !run.measures.empty()) {
        share = std::min(share, static_cast<double>(run.measures.size()) / context.song_bars);
    }
    return clamp01(share);
}

} // namespace

double speed_factor(int speed_percent) {
    const int s = std::clamp(speed_percent, 25, 200);
    if (s <= 100) {
        return 0.25 + 0.75 * s / 100.0;
    }
    return 1.0 + 0.2 * (s - 100) / 100.0;
}

double difficulty_factor(int difficulty_half_stars) {
    if (difficulty_half_stars <= 0) {
        return 1.0;
    }
    const double stars = std::clamp(difficulty_half_stars / 2.0, 1.0, 5.0);
    return 0.8 + 0.1 * (stars - 1.0);
}

ScoreParts score_parts(const RunRecord& in, const ScoreContext& context) {
    const RunRecord run = sanitized(in);
    ScoreParts parts;
    parts.speed = speed_factor(run.speed_percent);
    parts.difficulty = difficulty_factor(context.difficulty_half_stars);
    parts.hands = run.hands == HandFilter::Both ? 1.0 : kOneHandFactor;
    if (run.all.graded <= 0) {
        parts.coverage = 0.0;
        return parts; // nothing graded: no points
    }
    const double graded = run.all.graded;
    parts.credit = credit_of(run, graded);
    parts.flow = clamp01(run.longest_combo / graded);
    parts.balance = clamp01(balance_of(run));
    parts.coverage = coverage_of(run, context);
    const double raw =
        kWeightCredit * parts.credit + kWeightFlow * parts.flow + kWeightBalance * parts.balance;
    const double points = kScoreScale * raw * parts.speed * parts.difficulty * parts.hands *
                          (kCoverageFloor + (1.0 - kCoverageFloor) * parts.coverage);
    // Rounded half up, with a hair of slack so 812.4999999 from the sums of 0.7 + 0.2 + 0.1 is 813.
    parts.score = std::clamp(static_cast<int>(std::floor(points + 0.5 + 1e-9)), 0, kMaxRunScore);
    return parts;
}

RunRecord scored(const RunRecord& in, const ScoreContext& context) {
    RunRecord run = sanitized(in);
    const ScoreParts parts = score_parts(run, context);
    run.score = parts.score;
    run.covered_percent =
        run.all.graded > 0 ? std::clamp(static_cast<int>(std::lround(parts.coverage * 100.0)), 1, 100)
                           : 0;
    return run;
}

} // namespace etude
