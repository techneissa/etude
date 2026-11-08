// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_controller.hpp"

#include "hand_split_job.hpp"
#include "lent_song.hpp"
#include "practice_tables_job.hpp"

#include "etude/look_ahead.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace etude::app {

void SongController::start_countdown(std::int64_t now_ns) {
    const int n = std::clamp(options_.resume_countdown_s, kMinResumeCountdownS, kMaxResumeCountdownS);
    countdown_end_ns_ = now_ns + static_cast<std::int64_t>(n) * 1'000'000'000;
    countdown_ticks_left_ = n;
}

bool SongController::count_in_wanted() const {
    if (count_in_bars_ <= 0 || !loaded_) {
        return false;
    }
    if (mode_ != PracticeMode::Wait) {
        return true;
    }
    const auto& bars = song_.song.measures;
    const double first = bars.empty() ? 0.0 : bars.front().start_s;
    return transport_.position_s() <= first + 1e-6;
}

void SongController::set_count_in_bars(int bars) {
    count_in_bars_ = std::clamp(bars, 0, kMaxCountInBars);
}

void SongController::restore_song_settings(const SessionState& state, std::int64_t now_ns) {
    if (!loaded_ || state.song_hash == 0 || state.song_hash != song_.hash) {
        return;
    }
    SongPractice p = practice();
    p.speed_percent = state.speed_percent;
    p.count_in_bars = state.count_in_bars;
    p.hands = state.practice_hands;
    p.transpose_semitones = state.transpose_semitones;
    apply_practice(p, now_ns);
}

double SongController::bar_start_at(double song_s) const {
    return snap_to_measure(song_.song, song_s, SnapMode::Floor);
}

void SongController::play(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    if (menu_open_) {
        resume_after_menu_ = true; // plays through the countdown when the menu closes
        return;
    }
    cancel_countdown();
    stop_audition(now_ns);
    pump(now_ns);
    if (transport_.ended()) {
        replay(now_ns);
        return;
    }
    start_transport(now_ns, count_in_wanted());
    pump(now_ns);
    follow_wait(now_ns);
}

void SongController::pause(std::int64_t now_ns) {
    cancel_countdown();
    resume_after_menu_ = false;
    if (!loaded_) {
        return;
    }
    pump(now_ns);
    transport_.pause(now_ns);
    pump(now_ns);
}

void SongController::toggle_play(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    if (counting_down()) {
        pause(now_ns);
    } else if (menu_open_) {
        resume_after_menu_ = !resume_after_menu_;
    } else if (transport_.playing()) {
        pause(now_ns);
    } else {
        play(now_ns);
    }
}

void SongController::replay(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    wrongs_.clear(); // a jump leaves no stale red flash behind
    reset_feel(false);
    pump(now_ns);
    finish_run(now_ns);
    // A restart while playing stops the clock first, so the start counts in like any other.
    transport_.pause(now_ns);
    const double start = transport_.loop_enabled() ? transport_.loop_start_s() : 0.0;
    transport_.seek(now_ns, start);
    pump(now_ns);
    if (menu_open_) {
        resume_after_menu_ = true;
        return;
    }
    cancel_countdown();
    start_transport(now_ns, count_in_wanted());
    pump(now_ns);
    follow_wait(now_ns);
}

void SongController::seek(double song_s, std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    wrongs_.clear(); // a jump leaves no stale red flash behind
    reset_feel(false);
    pump(now_ns);
    if (transport_.scrub(now_ns, song_s)) {
        loop_a_.reset(); // the loop is over: a half-set A or B goes with it
        loop_b_.reset();
    }
    pump(now_ns);
    follow_wait(now_ns);
}

void SongController::skip_back(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    wrongs_.clear(); // a jump leaves no stale red flash behind
    reset_feel(false);
    pump(now_ns);
    if (transport_.skip(now_ns,
                        -static_cast<double>(std::clamp(options_.skip_back_seconds, 1, 30)))) {
        loop_a_.reset();
        loop_b_.reset();
    }
    pump(now_ns);
    follow_wait(now_ns);
}

void SongController::skip_forward(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    wrongs_.clear(); // a jump leaves no stale red flash behind
    reset_feel(false);
    pump(now_ns);
    if (transport_.skip(now_ns,
                        static_cast<double>(std::clamp(options_.skip_forward_seconds, 1, 30)))) {
        loop_a_.reset();
        loop_b_.reset();
    }
    pump(now_ns);
    follow_wait(now_ns);
}

void SongController::change_speed(int delta_percent, std::int64_t now_ns) {
    // A step of the buttons goes to the next multiple of 5 (83% up is 85%).
    const int current = transport_.speed_percent();
    set_speed(delta_percent % kSpeedStepPercent == 0
                  ? step_speed(current, delta_percent / kSpeedStepPercent)
                  : current + delta_percent,
              now_ns);
}

void SongController::set_speed(int percent, std::int64_t now_ns) {
    pump(now_ns);
    transport_.set_speed_percent(now_ns, percent);
    pump(now_ns);
    watch_run_setup();
}

void SongController::set_target_bpm(double bpm, std::int64_t now_ns) {
    if (loaded_) {
        set_speed(speed_for_target(song_.song, bpm, transport_.position_s()), now_ns);
    }
}

double SongController::tempo_bpm() const {
    return loaded_ ? effective_bpm(song_.song, transport_.position_s(), transport_.speed_percent())
                   : 0.0;
}

double SongController::main_tempo_bpm() const {
    return loaded_ ? main_beat_bpm(song_.song, transport_.position_s()) : 0.0;
}

bool SongController::tempo_beat_dotted() const {
    return loaded_ && beat_unit_at(song_.song, transport_.position_s()).dotted;
}

void SongController::set_loop_start_here(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    pump(now_ns);
    loop_a_ = bar_start_at(transport_.position_s());
    if (loop_b_ && std::abs(*loop_b_ - *loop_a_) >= kMinLoopSeconds) {
        set_loop(*loop_a_, *loop_b_, now_ns);
    }
}

void SongController::set_loop_end_here(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    pump(now_ns);
    const double pos = transport_.position_s();
    loop_b_ = snap_to_measure(song_.song, pos + 1e-6, SnapMode::Ceil);
    if (!loop_a_) {
        loop_a_ = bar_start_at(pos); // B alone loops the current bar
    }
    if (std::abs(*loop_b_ - *loop_a_) >= kMinLoopSeconds) {
        set_loop(*loop_a_, *loop_b_, now_ns);
    }
}

bool SongController::set_loop(double start_s, double end_s, std::int64_t now_ns) {
    if (!loaded_) {
        return false;
    }
    pump(now_ns);
    if (!transport_.set_loop(now_ns, start_s, end_s)) {
        return false;
    }
    transport_.set_loop_enabled(now_ns, true);
    loop_a_ = transport_.loop_start_s();
    loop_b_ = transport_.loop_end_s();
    pump(now_ns);
    refresh_hands_scope(now_ns);
    return true;
}

bool SongController::loop_bar(int measure, std::int64_t now_ns) {
    if (!loaded_) {
        return false;
    }
    const std::vector<Measure>& bars = song_.song.measures;
    const auto it = std::find_if(bars.begin(), bars.end(),
                                 [measure](const Measure& m) { return m.number == measure; });
    if (it == bars.end()) {
        return false;
    }
    const double start = it->start_s;
    const double end =
        std::next(it) != bars.end() ? std::next(it)->start_s : transport_.duration_s();
    if (!set_loop(start, end, now_ns)) {
        return false;
    }
    replay(now_ns);
    return true;
}

void SongController::clear_loop(std::int64_t now_ns) {
    loop_a_.reset();
    loop_b_.reset();
    if (!loaded_) {
        return;
    }
    pump(now_ns);
    transport_.clear_loop(now_ns);
    pump(now_ns);
    refresh_hands_scope(now_ns);
}

bool SongController::apply_plan_step(const PlanStep& step, std::int64_t now_ns) {
    if (!loaded_ || step.kind == PlanStep::Kind::None) {
        return true;
    }
    pump(now_ns);
    const bool ok = etude::apply_plan_step(transport_, now_ns, step);
    if (step.kind == PlanStep::Kind::SetLoop && ok) {
        loop_a_ = transport_.loop_start_s();
        loop_b_ = transport_.loop_end_s();
    } else if (step.kind != PlanStep::Kind::SetLoop) {
        loop_a_.reset();
        loop_b_.reset();
    }
    pump(now_ns);
    follow_wait(now_ns);
    return ok;
}

std::vector<LoopPass> SongController::take_loop_passes() {
    std::vector<LoopPass> out;
    out.swap(loop_passes_);
    return out;
}

} // namespace etude::app
