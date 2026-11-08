// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/effects_governor.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

EffectsBudget effects_budget(EffectsLevel level) {
    const int n = static_cast<int>(level);
    EffectsBudget b;
    b.shaders = n < static_cast<int>(EffectsLevel::NoShaders);
    b.particles = n < static_cast<int>(EffectsLevel::NoParticles);
    b.motion_background = n < static_cast<int>(EffectsLevel::NoMotion);
    b.glow_width = n < static_cast<int>(EffectsLevel::NarrowGlow) ? 1.0f : 0.5f;
    b.style_extras = n < static_cast<int>(EffectsLevel::NoStyleExtras);
    return b;
}

std::string_view effects_level_name(EffectsLevel level) {
    switch (level) {
    case EffectsLevel::Full:
        return "full";
    case EffectsLevel::NoShaders:
        return "no shaders";
    case EffectsLevel::NoParticles:
        return "no particles";
    case EffectsLevel::NoMotion:
        return "no motion";
    case EffectsLevel::NarrowGlow:
        return "narrow glow";
    case EffectsLevel::NoStyleExtras:
        return "no style extras";
    }
    return "full";
}

std::int64_t frame_cost_ns(std::int64_t work_ns, std::int64_t interval_ns, std::int64_t period_ns) {
    if (period_ns > 0 && interval_ns > period_ns + period_ns / 2) {
        return std::max(interval_ns, work_ns);
    }
    return work_ns;
}

EffectsGovernor::EffectsGovernor(GovernorConfig config) : config_(config) {
    config_.budget_ns = std::max<std::int64_t>(1, config_.budget_ns);
    config_.down_ns = std::max<std::int64_t>(1, config_.down_ns);
    config_.up_ns = std::max<std::int64_t>(1, config_.up_ns);
    config_.smoothing_period_ns = std::max<std::int64_t>(1, config_.smoothing_period_ns);
    config_.max_up_backoff = std::max(1, config_.max_up_backoff);
    config_.smoothing = std::clamp(config_.smoothing, 0.001, 1.0);
}

EffectsLevel EffectsGovernor::stepped(EffectsLevel level, int dir) const {
    int n = static_cast<int>(level) + dir;
    if (!config_.shader_level && n == static_cast<int>(EffectsLevel::NoShaders)) {
        n += dir;
    }
    return static_cast<EffectsLevel>(n);
}

void EffectsGovernor::set_shader_level(bool on) {
    config_.shader_level = on;
    if (on) {
        return;
    }
    if (level_ == EffectsLevel::NoShaders) {
        level_ = EffectsLevel::Full;
        over_ns_ = 0;
        headroom_ns_ = 0;
    }
    if (lowest_ == EffectsLevel::NoShaders) {
        lowest_ = EffectsLevel::Full;
    }
}

void EffectsGovernor::reset() {
    const GovernorConfig config = config_;
    *this = EffectsGovernor(config);
}

EffectsLevel EffectsGovernor::add_frame_after_gap(std::int64_t frame_ns) {
    over_ns_ = 0;
    headroom_ns_ = 0;
    return add_frame(frame_ns);
}

EffectsLevel EffectsGovernor::add_frame(std::int64_t frame_ns, std::int64_t interval_ns) {
    if (frame_ns <= 0) {
        return level_;
    }
    const std::int64_t dt = interval_ns > 0 ? interval_ns : config_.budget_ns;
    const auto t = static_cast<double>(frame_ns);
    const double weight =
        1.0 - std::pow(1.0 - config_.smoothing, static_cast<double>(dt) /
                                                    static_cast<double>(config_.smoothing_period_ns));
    average_ns_ = has_average_ ? average_ns_ + weight * (t - average_ns_) : t;
    has_average_ = true;
    since_up_ns_ = std::min<std::int64_t>(since_up_ns_ + dt, INT64_MAX / 2);

    const auto budget = static_cast<double>(config_.budget_ns);
    if (average_ns_ > budget * config_.over_ratio) {
        over_ns_ += dt;
        headroom_ns_ = 0;
    } else if (average_ns_ < budget * config_.headroom_ratio) {
        headroom_ns_ += dt;
        over_ns_ = 0;
    } else {
        over_ns_ = 0;
        headroom_ns_ = 0;
    }

    const std::int64_t up_needed = config_.up_ns * backoff_;
    if (over_ns_ >= config_.down_ns && level_ != kLowestEffectsLevel) {
        level_ = stepped(level_, 1);
        lowest_ = std::max(lowest_, level_);
        if (since_up_ns_ < config_.up_ns) {
            backoff_ = std::min(backoff_ * 2, config_.max_up_backoff);
        }
        over_ns_ = 0;
        headroom_ns_ = 0;
    } else if (headroom_ns_ >= up_needed && level_ != EffectsLevel::Full) {
        level_ = stepped(level_, -1);
        since_up_ns_ = 0;
        over_ns_ = 0;
        headroom_ns_ = 0;
    }
    // Long runs at either end stop counting, so the counters never overflow.
    over_ns_ = std::min(over_ns_, config_.down_ns);
    headroom_ns_ = std::min(headroom_ns_, config_.up_ns * config_.max_up_backoff);
    return level_;
}

} // namespace etude
