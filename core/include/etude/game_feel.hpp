// SPDX-License-Identifier: GPL-3.0-or-later
// The numbers behind the latency hint and the combo display (#88), with no drawing. All plain
// values and fixed-size storage: nothing here allocates per press.
//
// Word note: a "combo" is notes hit in a row. progress.hpp's "streak" means practice days, so the
// note run is never called a streak here.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace etude {

// The calibration suggestion needs this many graded presses in the run, so a few early presses
// of a hard passage never trigger it.
inline constexpr int kCalibrationMinPresses = 30;
// ...and a mean offset beyond this (strictly: exactly 25 ms is not beyond), in one direction.
inline constexpr std::int64_t kCalibrationThresholdNs = 25'000'000;

// Combo pulses: these, then every kComboMilestoneEvery after the last of them.
inline constexpr std::array<int, 4> kComboMilestones = {10, 25, 50, 100};
inline constexpr int kComboMilestoneEvery = 100; // 200, 300, ...
// A combo this long dropping to 0 is a "break"; a shorter one just ends.
inline constexpr int kComboBreakMin = 10;

enum class TimingDirection { Early, Late };

enum class ComboEventKind { None, Milestone, Break };

struct ComboEvent {
    ComboEventKind kind = ComboEventKind::None;
    // Milestone: the milestone reached (the highest one if a frame crossed several). Break: the
    // combo that was lost.
    int value = 0;
};

// A frame can hold both: a Break (a combo lost) and a Milestone (a combo reached).
struct ComboEvents {
    ComboEvent milestone;
    ComboEvent loss; // kind Break or None
};

// What the combo did going from `previous` to `current`. A rise past a milestone pulses; a combo
// of kComboBreakMin or more falling to 0 breaks. Anything else is None.
ComboEvent combo_event(int previous, int current);

// Everything one run keeps, fed by the grades as they arrive.
class GameFeel {
  public:
    // `combo` is the combo already in force (a seek that keeps the run's combo), so the next
    // update does not take it for a rise.
    void reset(int combo = 0);
    // A seek: only takes `combo` as the one in force, so no false pulse follows. The run's press
    // count, mean and the once-per-run suggestion stay (the suggestion fires at most once per
    // run, however much is skipped).
    void rebase_combo(int combo) { combo_ = combo; }

    // One graded press (Perfect, Good, Early or Late) with its signed error. Returns the
    // direction the first time this run's suggestion fires, else nullopt.
    std::optional<TimingDirection> add_press(std::int64_t error_ns);

    // The evaluator's combo after this frame's grades, and the combos it lost since the last call
    // (EvaluationSummary::combo_losses), oldest first. A loss of kComboBreakMin or more is a Break
    // (the largest one) even when a later hit in the same frame raised the combo again; a rise
    // past a milestone is a Milestone.
    ComboEvents update_combo(int combo, std::span<const int> losses = {});

    // The combo as of the last update_combo: the number beside the fire icon.
    int combo() const { return combo_; }
    // Graded presses this run.
    int presses() const { return presses_; }
    // Mean of every graded press this run; nullopt when none.
    std::optional<std::int64_t> run_mean_ns() const;
    // Set once the suggestion has fired this run.
    std::optional<TimingDirection> calibration_suggestion() const { return suggestion_; }

  private:
    std::int64_t sum_ns_ = 0;
    int presses_ = 0;
    std::optional<TimingDirection> suggestion_;
    int combo_ = 0;
};

} // namespace etude
