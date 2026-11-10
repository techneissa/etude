// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/results_card.hpp"
#include "etude/run_replay.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace etude {

// How a run went, worked out from what was played (the run's events) and the song's expected
// notes: the timing spread, the worst bars, the spots where a wrong note went in, and the two hands
// side by side. Built once, off the frame thread; the screen only reads it.
//
// A run whose played notes are gone (pruned, or an older run that never kept them) still has a
// summary, from its RunRecord: the counts, the worst bars and the hands, with no timing and no
// wrong-note spots (`detail` says which).

enum class SummaryDetail : std::uint8_t {
    Played,     // worked out from the played notes
    RecordOnly, // from the run's record alone: no timing, no wrong-note spots
    Nothing,    // the run graded nothing
};

// Which way a player's timing leans.
enum class Tendency : std::uint8_t { Steady, Early, Late };

// A median this far from zero, in milliseconds, is a lean and not noise: about half of Perfect's
// window at the usual 80 ms (28 ms, half of it 14), rounded up.
inline constexpr int kTendencyMs = 15;

// The timing of the presses that have one (Flow Mode; Wait Mode has no timing grade). A press is
// early when its error is below zero and late above it; one at exactly zero is neither.
struct TimingSpread {
    int samples = 0;
    int early = 0;
    int late = 0;
    int median_ms = 0; // of every error, signed; negative leans early
    // The middle half of the errors: Q3 - Q1, the hinges of the sorted errors (the median of each
    // half, the median itself left out when the count is odd). 0 for fewer than two samples.
    int spread_ms = 0;
    int median_early_ms = 0; // of the early presses only, negative; 0 when there are none
    int median_late_ms = 0;  // of the late presses only; 0 when there are none
    Tendency tendency = Tendency::Steady;

    bool valid() const { return samples > 0; }
};

// One hand's part of the run. A hand the run did not practise (a hand-filtered run) is not
// `practised` and has no figures: it is not "0%".
struct SummaryHand {
    bool practised = false;
    int graded = 0;
    int hit = 0;
    int missed = 0;
    int early = 0; // presses graded Early / Late
    int late = 0;
    int accuracy_pct = -1; // -1: nothing graded
    int median_ms = 0;     // of this hand's press errors (Flow); 0 when it has none
    int timing_samples = 0;
};

// A bar where wrong notes went in, with the first of them: the key pressed and the key of the
// note nearest in time (the one it was probably meant for), -1 when no note is near.
struct WrongSpot {
    int measure = 0;
    int written = 0;
    int count = 0;
    std::int32_t first_ms = 0;
    int key = 0;
    int expected_key = -1;
    friend bool operator==(const WrongSpot&, const WrongSpot&) = default;
};

// A wrong press and a note this close in time are one mistake: the note is what it was meant for.
inline constexpr std::int32_t kWrongSpotReachMs = 300;
inline constexpr int kMaxWrongSpots = 6;
// Two hands differ when their accuracy is this many points apart and each graded enough notes.
inline constexpr int kWeakerHandGapPct = 10;
inline constexpr int kWeakerHandMinNotes = 4;

struct RunSummary {
    SummaryDetail detail = SummaryDetail::Nothing;
    GhostSetup setup;

    int graded = 0;
    int hit = 0;
    int missed = 0;
    int wrong = 0;
    int accuracy_pct = -1;
    RunCounts counts; // Perfect, Good, Early, Late, Missed, Played

    // How much of the run the numbers cover.
    bool complete = false;
    bool looped = false;       // passes are summed
    bool stopped_early = false; // ended before it covered the notes it was to grade
    int coverage_pct = -1;     // graded notes of the expected ones; -1 when not stored
    int dropped_events = 0;    // events the recorder could not hold: the figures are of the rest

    TimingSpread timing;
    std::array<SummaryHand, 3> hands{}; // by Hand: Unknown, Left, Right
    Hand weaker_hand = Hand::Unknown;   // Unknown: neither, or not both practised

    // The trouble bars, worst first (the results card's ranking: misses, a wrong press worth half
    // of one, a tie to the earlier bar), at most kMaxCardBars. Each is what "practise these bars"
    // takes (`plan_summary_practice`).
    std::array<CardBar, kMaxCardBars> bars{};
    int bar_count = 0;
    std::vector<WrongSpot> wrong_spots; // the busiest bars first, at most kMaxWrongSpots
    // Every bar the run played, in song order, with what it graded, missed and got wrong: the run
    // bar by bar (the replay's track is coloured from it).
    std::vector<RunBar> played_bars;

    const SummaryHand& hand(Hand h) const { return hands[static_cast<std::size_t>(h)]; }
};

// The bars of `summary` ready to practise: what `SectionsController::practise_bars` takes. Every
// trouble bar by default, or the first `max_bars` of them; a bar too short to loop is dropped and
// counted. `duration_s` is the end of the song's last bar.
BarPractice plan_summary_practice(const RunSummary& summary, std::span<const Measure> measures,
                                  double duration_s, BarPracticeKind kind, int max_bars = kMaxCardBars);

// A run's replay and summary together: what the history thread hands the frame.
struct RunReplay {
    ReplayTimeline timeline;
    RunSummary summary;
};

RunReplay build_run_replay(const RunRecord& record, const RunEvents& events, const ReplaySong& song);

} // namespace etude
