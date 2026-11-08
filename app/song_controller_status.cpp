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

int SongController::grade_of(std::uint32_t note_index) const {
    return note_index < grade_by_note_.size() ? grade_by_note_[note_index] : -1;
}

int SongController::combo() const {
    const EvaluationSummary* s = summary();
    return s != nullptr ? s->combo : 0;
}

int SongController::longest_combo() const {
    const EvaluationSummary* s = summary();
    return s != nullptr ? s->longest_combo : 0;
}

int SongController::countdown() const {
    if (!countdown_end_ns_) {
        return 0;
    }
    const std::int64_t left = *countdown_end_ns_ - last_now_ns_;
    return std::clamp(static_cast<int>((left + 999'999'999) / 1'000'000'000),
                      kMinResumeCountdownS, kMaxResumeCountdownS);
}

int SongController::count_in_label() const {
    return loaded_ ? etude::count_in_label(count_in_input()) : 0;
}

double SongController::count_in_progress() const {
    const int total = transport_.count_in_total();
    if (total <= 0) {
        return 0.0;
    }
    return std::clamp(1.0 - transport_.count_in_beats_left() / total, 0.0, 1.0);
}

double SongController::display_position_s() const {
    // The clock holds its position while it counts in, and so do the notes: nothing falls until
    // the count ends, so a moving note never reads as "it has started".
    return transport_.position_s();
}

SongController::BeatPulse SongController::beat_pulse() const {
    BeatPulse pulse;
    if (!loaded_) {
        return pulse;
    }
    const double rate = transport_.speed_percent() / 100.0;
    if (transport_.counting_in()) {
        const CountInInput in = count_in_input();
        const double beats = in.total_beats - in.beats_left;
        pulse.active = true;
        pulse.age_s = (beats - std::floor(beats + 1e-9)) * in.beat_s / rate;
        pulse.accent = etude::count_in_label(in) == 1;
        return pulse;
    }
    if (options_.metronome && transport_.state() == TransportState::Playing) {
        BeatGrid::Beat beat;
        if (grid_.last_beat(transport_.position_s(), &beat)) {
            pulse.active = true;
            pulse.age_s = (transport_.position_s() - beat.time_s) / rate;
            pulse.accent = beat.number == 1;
        }
    }
    return pulse;
}

const std::string& SongController::status_line() const {
    StatusKey key;
    key.loaded = loaded_;
    if (loaded_) {
        key.mode = mode_;
        key.hands = practice_hands(); // what applies, not what was asked: a loop can change it
        key.speed = transport_.speed_percent();
        key.bar = bar();
        key.bar_count = bar_count();
        key.written_bar = written_bar();
        key.loop_enabled = transport_.loop_enabled();
        key.loop_a_set = loop_a_.has_value();
        key.sound = song_sound();
        key.semitones = semitones_;
        key.combo = mode_ == PracticeMode::Flow ? combo() : 0;
        key.state = transport_.state();
        key.counting_down = counting_down();
    }
    if (status_valid_ && key == status_key_ && (!loaded_ || song_.title == status_title_)) {
        return status_text_;
    }
    status_key_ = key;
    status_title_ = song_.title;
    status_valid_ = true;
    status_text_.clear();
    status_span_count_ = 0;
    if (!loaded_) {
        return status_text_;
    }
    // Built in place: the text keeps its buffer from one rebuild to the next. Each part's bytes are
    // noted with its drop rank (status_parts.hpp).
    std::string& s = status_text_;
    const auto note = [&](std::size_t begin, StatusRank rank) {
        if (status_span_count_ < kMaxStatusSpans) {
            status_spans_[static_cast<std::size_t>(status_span_count_++)] = {
                static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(s.size()), rank};
        }
    };
    s += song_.title.empty() ? std::string_view("Untitled") : std::string_view(song_.title);
    note(0, StatusRank::Title);
    const auto add = [&](const std::string& part, StatusRank rank) {
        s += " \xC2\xB7 "; // a middle dot
        const std::size_t begin = s.size();
        s += part;
        note(begin, rank);
    };
    add(mode_name(mode_), StatusRank::Mode);
    add(hands_name(practice_hands()), StatusRank::Hands);
    add(format_unit_string(transport_.speed_percent(), Unit::Percent), StatusRank::Speed);
    add("Bar " + std::to_string(bar()) + "/" + std::to_string(bar_count()), StatusRank::Bar);
    if (written_bar() != bar()) {
        add("sheet bar " + std::to_string(written_bar()), StatusRank::SheetBar);
    }
    if (transport_.loop_enabled()) {
        add("Loop", StatusRank::Loop);
    } else if (loop_a_) {
        add("A set", StatusRank::Loop);
    }
    add(std::string("Sound: ") + song_sound_name(song_sound()), StatusRank::Sound);
    if (semitones_ != 0) {
        add((semitones_ > 0 ? "+" : "") + std::to_string(semitones_) + " st", StatusRank::Transpose);
    }
    if (mode_ == PracticeMode::Flow) {
        add("Combo " + std::to_string(combo()), StatusRank::Combo);
    }
    switch (transport_.state()) {
    case TransportState::Paused:
        add(counting_down() ? "Ready" : "Paused", StatusRank::State);
        break;
    case TransportState::Held:
        add("Waiting", StatusRank::State);
        break;
    case TransportState::Ended:
        add("End", StatusRank::State);
        break;
    case TransportState::CountIn:
    case TransportState::Playing:
        break;
    }
    return status_text_;
}

void SongController::add_status_parts(StatusParts& out, std::uint8_t group) const {
    const std::string_view text = status_line();
    for (int i = 0; i < status_span_count_; ++i) {
        const StatusSpan& span = status_spans_[static_cast<std::size_t>(i)];
        out.add(text.substr(span.begin, span.end - span.begin), span.rank, group);
    }
}

std::string results_line(const RunRecord& record, const AttemptChange& change, bool comparable) {
    // No accuracy (nothing graded) is a dash, never 100%.
    const auto pct = [](std::optional<double> a) {
        return a ? format_unit_string(std::round(*a * 100.0), Unit::Percent)
                 : std::string("\xE2\x80\x93");
    };
    std::string s = "Results: ";
    std::vector<std::string> parts;
    const bool right = record.hands != HandFilter::Left && record.right.graded > 0;
    const bool left = record.hands != HandFilter::Right && record.left.graded > 0;
    if (right) {
        parts.push_back("right " + pct(record.right.accuracy()));
    }
    if (left) {
        parts.push_back("left " + pct(record.left.accuracy()));
    }
    if (!right && !left) {
        parts.push_back(pct(record.accuracy(record.hands)));
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        s += (i > 0 ? ", " : "") + parts[i];
    }
    s += " \xC2\xB7 longest combo " + std::to_string(record.longest_combo);
    if (!comparable) {
        // The history is still loading, so "first try" or "same as last time" would be a guess.
    } else if (!change.has_previous) {
        s += " \xC2\xB7 first try like this";
    } else {
        const int delta = static_cast<int>(std::lround(change.accuracy_delta * 100.0));
        switch (change.trend) {
        case Trend::Better:
            s += " \xC2\xB7 up " + format_unit_string(std::max(delta, 1), Unit::Percent) +
                 " on last time";
            break;
        case Trend::Worse:
            s += " \xC2\xB7 down " + format_unit_string(std::max(-delta, 1), Unit::Percent) +
                 " on last time";
            break;
        case Trend::Same:
            s += " \xC2\xB7 same as last time";
            break;
        }
    }
    if (comparable && change.new_best) {
        s += " \xC2\xB7 new best";
    }
    return s;
}

} // namespace etude::app
