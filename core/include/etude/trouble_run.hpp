// SPDX-License-Identifier: GPL-3.0-or-later
// A trouble-bar run: the bars the player chose on the results card, practised one by one in the
// order they put them in. A bar is played, looped, until a pass is clean; then the run waits at a
// check-in (how it went, and Next bar, Again, Skip or End) before the next one. Skip and End also
// work while a bar plays. At the end, or on End, the summary lists every bar before and after.
//
// This is a pure state machine: it owns no clock, no song and no loop. The app feeds it each loop
// pass and each command, and does what the returned step says (play a bar, pause, end).
//
//   TroubleRun run;
//   RunStep step = run.start(bars);               // Play bar 0
//   step = run.on_pass({clean, graded, missed, wrong});   // Pause once a pass is clean
//   step = run.command(RunCommand::Next);         // Play the next bar, or End after the last
#pragma once

#include "etude/results_card.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace etude {

// The most bars a run holds: the card names at most this many.
inline constexpr int kMaxRunBars = kMaxCardBars;

// One bar of the run.
struct TroubleBar {
    int measure = 0;     // Measure::number
    int written = 0;     // the bar number the printed score shows
    int pass = 0;        // which time through a repeated written bar (CardBar::pass), 0 for once
    int before_pct = -1; // how it went in the run that named it, -1 when unknown
    friend bool operator==(const TroubleBar&, const TroubleBar&) = default;
};

// The bars of `practice` (the loops that were made, in the order they play) with the card's
// details, matched by `BarLoop::first_measure`. A loop with no such bar on the card gets its
// bar number and nothing else. At most `kMaxRunBars`.
struct TroubleBars {
    std::array<TroubleBar, kMaxRunBars> bar{};
    int count = 0;
};
TroubleBars trouble_bars(const ResultsCard& card, const BarPractice& practice);
// The same from any list of named bars (a run summary's).
TroubleBars trouble_bars(std::span<const CardBar> bars, const BarPractice& practice);

// Where a bar stands in the run.
enum class BarEnd : std::uint8_t {
    Waiting, // not reached yet (or the run ended before it)
    Playing, // being practised now
    Cleared, // a pass was clean
    Skipped, // passed over
    Ended,   // the run was ended while it played
};

struct BarProgress {
    int passes = 0;       // loop passes played
    int clean_passes = 0; // of them with no miss and no wrong press
    int graded = 0;       // notes graded over all the passes
    int missed = 0;
    int wrong = 0;
    // The graded notes by grade over all the passes (`played` in Wait Mode) and the longest run of
    // hits in any one pass.
    int perfect = 0;
    int good = 0;
    int early = 0;
    int late = 0;
    int played = 0;
    int combo = 0;
    BarEnd end = BarEnd::Waiting;
    // How it went over all its passes, -1 before any note was graded.
    int accuracy_pct() const { return bar_accuracy_pct(graded, missed, wrong); }
    friend bool operator==(const BarProgress&, const BarProgress&) = default;
};

enum class RunPhase : std::uint8_t {
    Idle,    // no run
    Playing, // a bar is looping
    CheckIn, // a bar was cleared: the song waits for Next bar, Again, Skip or End
    Done,    // finished or ended: the summary shows
};

// What the player asked for, from the check-in's buttons and the keys.
enum class RunCommand : std::uint8_t {
    Next,  // the following bar (while a bar plays: the same as Skip)
    Again, // this bar once more
    Skip,  // at the check-in: pass over the next bar and play the one after it (nothing after the
           // last bar); while a bar plays: give this one up and play the next
    End,   // end the run
};

// What a tap or a key on the check-in or the summary asks for: the four commands, or Done to
// dismiss the summary.
enum class RunCardAction : std::uint8_t { None, Next, Again, Skip, End, Done };

// The command a card action is, or none for None and Done.
constexpr bool run_card_command(RunCardAction action, RunCommand* out) {
    switch (action) {
    case RunCardAction::Next:
        *out = RunCommand::Next;
        return true;
    case RunCardAction::Again:
        *out = RunCommand::Again;
        return true;
    case RunCardAction::Skip:
        *out = RunCommand::Skip;
        return true;
    case RunCardAction::End:
        *out = RunCommand::End;
        return true;
    case RunCardAction::None:
    case RunCardAction::Done:
        break;
    }
    return false;
}

// What the app does after a pass or a command.
struct RunStep {
    enum class Kind : std::uint8_t {
        None,
        Play,  // loop bar `bar` from its start and play
        Pause, // pause the song and keep the loop: the check-in is up
        End,   // clear the loop and the speed-up and pause: the summary is up
    };
    Kind kind = Kind::None;
    int bar = -1; // Play
    friend bool operator==(const RunStep&, const RunStep&) = default;
};

// The numbers of one loop pass, from the evaluator.
struct RunPass {
    bool clean = false;
    int graded = 0;
    int missed = 0;
    int wrong = 0;
    int perfect = 0;
    int good = 0;
    int early = 0;
    int late = 0;
    int played = 0;
    int combo = 0;
};

class TroubleRun {
  public:
    // Begins a run over `bars` (at most `kMaxRunBars`, the rest are ignored) at the first one.
    // Play bar 0, or None, and no run, for no bars.
    RunStep start(std::span<const TroubleBar> bars);

    // One loop pass of the bar that is playing finished. A clean pass clears the bar and waits at
    // the check-in (Pause); any other pass only counts. None when no bar is playing.
    RunStep on_pass(const RunPass& pass);

    // A check-in button or a key, or the skip and end controls while a bar plays. None when no run
    // is under way (a finished run has nothing to ask for), and for Skip at the check-in of the
    // last bar (nothing is left to pass over).
    RunStep command(RunCommand command);

    // The run was ended from outside (the loop was cleared, another song opened): the same as End
    // while a bar plays or waits at the check-in. Returns End then, else None.
    RunStep abandon();

    // Forgets the run, the summary included.
    void reset();

    RunPhase phase() const { return phase_; }
    bool active() const { return phase_ == RunPhase::Playing || phase_ == RunPhase::CheckIn; }
    int count() const { return count_; }
    // The bar playing, or waiting at the check-in; the last one reached when done.
    int current() const { return current_; }
    // The bar Next would play, or -1 after the last.
    int next_bar() const { return current_ + 1 < count_ ? current_ + 1 : -1; }
    const TroubleBar& bar(int i) const { return bars_[static_cast<std::size_t>(i)]; }
    const BarProgress& progress(int i) const { return progress_[static_cast<std::size_t>(i)]; }
    // Done after the last bar (every bar reached), as opposed to ended early.
    bool finished() const { return phase_ == RunPhase::Done && !ended_; }
    bool ended() const { return phase_ == RunPhase::Done && ended_; }
    int cleared() const;
    int skipped() const;

  private:
    // Plays bar `index`, or ends the run after the last.
    RunStep go_to(int index);
    RunStep end(bool early);

    std::array<TroubleBar, kMaxRunBars> bars_{};
    std::array<BarProgress, kMaxRunBars> progress_{};
    int count_ = 0;
    int current_ = 0;
    RunPhase phase_ = RunPhase::Idle;
    bool ended_ = false;
};

} // namespace etude
