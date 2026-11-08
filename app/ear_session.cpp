// SPDX-License-Identifier: GPL-3.0-or-later
#include "ear_session.hpp"

#include <algorithm>

namespace etude::app {

namespace {

// The semitones above a C of each white key of an octave.
constexpr std::array<int, 7> kWhiteSteps{0, 2, 4, 5, 7, 9, 11};

bool ear_is_black_note(int note) {
    const int pc = ((note % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

} // namespace

int ear_white_note(int low_note, int index) {
    const int i = std::max(index, 0);
    return low_note + 12 * (i / 7) + kWhiteSteps[static_cast<std::size_t>(i % 7)];
}

int ear_white_index(int low_note, int note) {
    const int d = std::max(note - low_note, 0);
    int white = 0;
    for (std::size_t i = 0; i < kWhiteSteps.size(); ++i) {
        if (kWhiteSteps[i] <= d % 12) {
            white = static_cast<int>(i);
        }
    }
    return 7 * (d / 12) + white;
}

EarSettings ear_settings(const EarConfig& c) {
    EarSettings s;
    s.range = KeyRange{c.low_note, ear_top_note(c)};
    s.kind = c.mode == LearnEarMode::Note ? EarQuestionKind::Note : EarQuestionKind::Interval;
    s.min_interval = 1;
    s.max_interval = c.max_interval;
    // Down asks downward only; up and together ask upward, so the starting note is the lower one.
    s.allow_ascending = c.mode != LearnEarMode::IntervalDown;
    s.allow_descending = c.mode == LearnEarMode::IntervalDown;
    s.harmonic = c.mode == LearnEarMode::Together;
    return s;
}

EarSession::EarSession(std::uint64_t seed) : seed_(seed), trainer_(seed, ear_settings(EarConfig{})) {}

bool EarSession::configure(const EarConfig& config) {
    const bool same_question = config == config_;
    const bool resized = config.whites != config_.whites;
    if (same_question && !resized) {
        return false;
    }
    config_ = config;
    ++epoch_;
    trainer_ = EarTrainer(seed_ + static_cast<std::uint64_t>(epoch_), ear_settings(config_));
    if (same_question && (phase_ == EarPhase::Idle || fit_view())) {
        return false; // only the window changed: the question in hand stays
    }
    phase_ = EarPhase::Idle;
    answered_ = -1;
    hinted_ = false;
    view_first_ = 0;
    return true;
}

// Keeps the notes of the question in hand on the keys on view after a resize: the view moves by white
// keys, the question does not. False when they span more white keys than are on view.
bool EarSession::fit_view() {
    int lo = ear_white_index(config_.low_note, question_.first);
    int hi = lo + (ear_is_black_note(question_.first) ? 1 : 0);
    for (const int note : {expected(), answered_}) {
        if (note < 0) {
            continue;
        }
        const int at = ear_white_index(config_.low_note, note);
        lo = std::min(lo, at);
        hi = std::max(hi, at + (ear_is_black_note(note) ? 1 : 0));
    }
    if (hi - lo + 1 > config_.whites) {
        return false;
    }
    if (lo < view_first_) {
        view_first_ = lo;
    }
    if (hi > view_first_ + config_.whites - 1) {
        view_first_ = hi - config_.whites + 1;
    }
    view_first_ = std::max(view_first_, 0);
    return true;
}

bool EarSession::next() {
    if (phase_ == EarPhase::Asked) {
        return false;
    }
    const EarQuestionResult r = trainer_.next();
    if (!r.ok) {
        message_ = r.message;
        phase_ = EarPhase::Idle;
        return false;
    }
    message_.clear();
    question_ = r.question;
    phase_ = EarPhase::Asked;
    answered_ = -1;
    hinted_ = false;
    view_first_ = 0;
    return true;
}

const EarQuestion* EarSession::replay() {
    if (phase_ == EarPhase::Idle) {
        return nullptr;
    }
    ++replays_;
    return &question_;
}

bool EarSession::is_expected(int note) const {
    return check_pitch_answer(question_, note, config_.any_octave);
}

EarPhase EarSession::press(int note) {
    if (phase_ != EarPhase::Asked || note < view_low() || note > view_top() || note == start_note()) {
        return phase_;
    }
    answered_ = note;
    const bool right = is_expected(note);
    phase_ = right ? EarPhase::Right : EarPhase::Wrong;
    ++answers_;
    if (right) {
        ++rights_;
    }
    if (!right) {
        streak_ = 0;
        mark(EarMark::Wrong);
    } else if (hinted_) {
        mark(EarMark::Hinted); // a hinted answer neither adds to the streak nor breaks it
    } else {
        ++streak_;
        best_ = std::max(best_, streak_);
        mark(EarMark::Right);
    }
    return phase_;
}

EarKeyRole EarSession::key_role(int note) const {
    const bool done = phase_ == EarPhase::Right || phase_ == EarPhase::Wrong;
    if (done && note == answered_) {
        return EarKeyRole::Tapped;
    }
    if (done && phase_ == EarPhase::Wrong && note == expected()) {
        return EarKeyRole::Missed;
    }
    if (done && note == start_note()) {
        return EarKeyRole::Start;
    }
    if (phase_ == EarPhase::Asked && hinted_ && note == expected()) {
        return EarKeyRole::Hint;
    }
    return EarKeyRole::Plain;
}

bool EarSession::hint() {
    if (phase_ != EarPhase::Asked || hinted_) {
        return false;
    }
    hinted_ = true;
    return true;
}

void EarSession::mark(EarMark m) {
    // The dots fill from the left; when all ten are used the oldest goes.
    std::size_t at = 0;
    while (at < dots_.size() && dots_[at] != EarMark::None) {
        ++at;
    }
    if (at == dots_.size()) {
        std::rotate(dots_.begin(), dots_.begin() + 1, dots_.end());
        at = dots_.size() - 1;
    }
    dots_[at] = m;
}

} // namespace etude::app
