// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/game_feel.hpp"

namespace etude {

namespace {

// The highest milestone in (previous, current], or 0.
int milestone_crossed(int previous, int current) {
    int best = 0;
    for (const int m : kComboMilestones) {
        if (m > previous && m <= current) {
            best = m;
        }
    }
    const int last = kComboMilestones.back();
    if (current > last) {
        const int top = current / kComboMilestoneEvery * kComboMilestoneEvery;
        const int from = previous > last ? previous : last;
        if (top > from) {
            best = top;
        }
    }
    return best;
}

} // namespace

ComboEvent combo_event(int previous, int current) {
    if (current == 0 && previous >= kComboBreakMin) {
        return {ComboEventKind::Break, previous};
    }
    if (current > previous) {
        const int m = milestone_crossed(previous, current);
        if (m > 0) {
            return {ComboEventKind::Milestone, m};
        }
    }
    return {};
}

void GameFeel::reset(int combo) {
    sum_ns_ = 0;
    presses_ = 0;
    suggestion_.reset();
    combo_ = combo;
}

std::optional<TimingDirection> GameFeel::add_press(std::int64_t error_ns) {
    sum_ns_ += error_ns;
    ++presses_;
    if (suggestion_ || presses_ < kCalibrationMinPresses) {
        return std::nullopt;
    }
    // Compare the sum with the threshold times the count: no rounding of the mean.
    const std::int64_t limit = kCalibrationThresholdNs * presses_;
    if (sum_ns_ > limit) {
        suggestion_ = TimingDirection::Late;
    } else if (sum_ns_ < -limit) {
        suggestion_ = TimingDirection::Early;
    }
    return suggestion_;
}

ComboEvents GameFeel::update_combo(int combo, std::span<const int> losses) {
    ComboEvents e;
    e.milestone = combo_event(combo_, combo);
    if (e.milestone.kind != ComboEventKind::Milestone) {
        e.milestone = {};
    }
    int largest = 0;
    for (const int lost : losses) {
        largest = lost > largest ? lost : largest;
    }
    if (largest >= kComboBreakMin) {
        e.loss = {ComboEventKind::Break, largest};
    }
    combo_ = combo;
    return e;
}

std::optional<std::int64_t> GameFeel::run_mean_ns() const {
    if (presses_ == 0) {
        return std::nullopt;
    }
    return sum_ns_ / presses_;
}

} // namespace etude
