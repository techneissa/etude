// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/trouble_run.hpp"

#include <algorithm>

namespace etude {

TroubleBars trouble_bars(const ResultsCard& card, const BarPractice& practice) {
    return trouble_bars(
        std::span<const CardBar>(card.bars.data(), static_cast<std::size_t>(card.bar_count)),
        practice);
}

TroubleBars trouble_bars(std::span<const CardBar> bars, const BarPractice& practice) {
    TroubleBars out;
    const int n = std::min(practice.count, kMaxRunBars);
    for (int i = 0; i < n; ++i) {
        const BarLoop& loop = practice.loops[static_cast<std::size_t>(i)];
        TroubleBar bar;
        bar.measure = loop.first_measure;
        bar.written = loop.first_measure;
        for (const CardBar& named : bars) {
            if (named.measure == loop.first_measure) {
                bar.written = named.written;
                bar.pass = named.pass;
                bar.before_pct = bar_accuracy_pct(named.graded, named.missed, named.wrong);
                break;
            }
        }
        out.bar[static_cast<std::size_t>(out.count++)] = bar;
    }
    return out;
}

int TroubleRun::cleared() const {
    return static_cast<int>(
        std::count_if(progress_.begin(), progress_.begin() + count_,
                      [](const BarProgress& p) { return p.end == BarEnd::Cleared; }));
}

int TroubleRun::skipped() const {
    return static_cast<int>(
        std::count_if(progress_.begin(), progress_.begin() + count_,
                      [](const BarProgress& p) { return p.end == BarEnd::Skipped; }));
}

RunStep TroubleRun::start(std::span<const TroubleBar> bars) {
    reset();
    count_ = static_cast<int>(std::min<std::size_t>(bars.size(), kMaxRunBars));
    if (count_ == 0) {
        return {};
    }
    std::copy_n(bars.begin(), count_, bars_.begin());
    return go_to(0);
}

void TroubleRun::reset() {
    bars_ = {};
    progress_ = {};
    count_ = 0;
    current_ = 0;
    phase_ = RunPhase::Idle;
    ended_ = false;
}

RunStep TroubleRun::go_to(int index) {
    if (index >= count_) {
        return end(false);
    }
    current_ = index;
    progress_[static_cast<std::size_t>(index)].end = BarEnd::Playing;
    phase_ = RunPhase::Playing;
    return {RunStep::Kind::Play, index};
}

RunStep TroubleRun::end(bool early) {
    phase_ = RunPhase::Done;
    ended_ = early;
    return {RunStep::Kind::End, -1};
}

RunStep TroubleRun::on_pass(const RunPass& pass) {
    if (phase_ != RunPhase::Playing) {
        return {};
    }
    BarProgress& p = progress_[static_cast<std::size_t>(current_)];
    ++p.passes;
    p.graded += std::max(0, pass.graded);
    p.missed += std::max(0, pass.missed);
    p.wrong += std::max(0, pass.wrong);
    p.perfect += std::max(0, pass.perfect);
    p.good += std::max(0, pass.good);
    p.early += std::max(0, pass.early);
    p.late += std::max(0, pass.late);
    p.played += std::max(0, pass.played);
    p.combo = std::max(p.combo, pass.combo);
    if (!pass.clean) {
        return {};
    }
    ++p.clean_passes;
    p.end = BarEnd::Cleared;
    phase_ = RunPhase::CheckIn;
    return {RunStep::Kind::Pause, current_};
}

RunStep TroubleRun::command(RunCommand command) {
    if (!active()) {
        return {};
    }
    BarProgress& here = progress_[static_cast<std::size_t>(current_)];
    if (command == RunCommand::End) {
        if (phase_ == RunPhase::Playing) {
            here.end = BarEnd::Ended;
        }
        return end(true);
    }
    if (phase_ == RunPhase::Playing) {
        switch (command) {
        case RunCommand::Again:
            return {RunStep::Kind::Play, current_};
        case RunCommand::Next:
        case RunCommand::Skip:
            here.end = BarEnd::Skipped;
            return go_to(current_ + 1);
        case RunCommand::End:
            break;
        }
        return {};
    }
    // The check-in: the bar just played was cleared.
    switch (command) {
    case RunCommand::Next:
        return go_to(current_ + 1);
    case RunCommand::Again:
        return go_to(current_);
    case RunCommand::Skip: {
        // Pass over the bar that would come next and play the one after it; after the last bar
        // there is nothing to pass over.
        const int passed = current_ + 1;
        if (passed >= count_) {
            return {};
        }
        progress_[static_cast<std::size_t>(passed)].end = BarEnd::Skipped;
        return go_to(current_ + 2);
    }
    case RunCommand::End:
        break;
    }
    return {};
}

RunStep TroubleRun::abandon() { return command(RunCommand::End); }

} // namespace etude
