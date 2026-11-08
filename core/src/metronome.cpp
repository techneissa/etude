// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/metronome.hpp"

#include "etude/meter.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

std::uint8_t scaled(int base, int volume_percent) {
    if (volume_percent <= 0) {
        return 0;
    }
    const int v = static_cast<int>(std::lround(base * std::min(volume_percent, kMaxClickVolume) /
                                               static_cast<double>(kDefaultClickVolume)));
    return static_cast<std::uint8_t>(std::clamp(v, 1, 127));
}

// Clicks per beat: Divide splits it in 2 (3 eighths in a compound meter), Triplets in 3. A compound
// beat is already in threes, so Triplets there is Divide.
int subdivision_parts(ClickSubdivision s, bool compound) {
    switch (s) {
    case ClickSubdivision::Divide:
        return compound ? 3 : 2;
    case ClickSubdivision::Triplets:
        return 3;
    case ClickSubdivision::Off:
        break;
    }
    return 1;
}

} // namespace

ClickSound click_sound_for(ClickVoice voice, int volume_percent) {
    ClickSound s;
    s.enabled = volume_percent > 0;
    s.channel = 9;
    switch (voice) {
    case ClickVoice::WoodBlock:
        s.accent_note = 76;
        s.note = 77;
        break;
    case ClickVoice::Click:
        s.accent_note = 34;
        s.note = 33;
        break;
    case ClickVoice::Beep:
        s.accent_note = 81;
        s.note = 80;
        break;
    }
    // Full volume (the default, 70) is the sound's usual loudness; 100 is louder, never above 127.
    s.accent_velocity = scaled(112, volume_percent);
    s.velocity = scaled(80, volume_percent);
    s.sub_velocity = scaled(44, volume_percent);
    return s;
}

void BeatGrid::set_song(const Song& song) {
    tempo_ = song.tempo;
    measures_ = song.measures;
}

// A pickup bar (number 0) is the end of a bar, so its beats are numbered back from the next bar's
// last beat.
void BeatGrid::clicks(const TransportOp& move, const MetronomeSettings& settings,
                      std::vector<TransportOp>& out) const {
    const double from_s = move.from_s;
    const double to_s = move.to_s;
    if (measures_.empty() || !(to_s > from_s)) {
        return;
    }
    const auto ns_at = [&](double t) {
        return move.at_ns + std::llround((t - from_s) / (to_s - from_s) *
                                         static_cast<double>(move.end_ns - move.at_ns));
    };
    auto first = std::upper_bound(measures_.begin(), measures_.end(), from_s,
                                  [](double s, const Measure& m) { return s < m.start_s; });
    std::size_t i =
        first == measures_.begin() ? 0 : static_cast<std::size_t>(first - measures_.begin()) - 1;
    for (; i < measures_.size(); ++i) {
        const Measure& m = measures_[i];
        if (m.start_s >= to_s) {
            return;
        }
        const bool has_next = i + 1 < measures_.size();
        const std::int64_t next_tick = has_next ? measures_[i + 1].start_tick : INT64_MAX;
        const double spacing = ticks_per_beat(tempo_.ticks_per_quarter, m.numerator, m.denominator);
        const int parts = subdivision_parts(settings.subdivision,
                                            is_compound_meter(m.numerator, m.denominator));
        int beats = beats_per_bar(m.numerator, m.denominator);
        int label = 1;
        if (m.number == 0 && has_next) {
            const Measure& bar = measures_[i + 1];
            const int bar_beats = beats_per_bar(bar.numerator, bar.denominator);
            beats = std::max(1, static_cast<int>(std::llround(
                                    static_cast<double>(next_tick - m.start_tick) / spacing)));
            label = std::max(1, bar_beats - beats + 1);
        }
        for (int k = 0; k < beats; ++k) {
            for (int j = 0; j < parts; ++j) {
                const double tick = static_cast<double>(m.start_tick) + (k + j / double(parts)) * spacing;
                if (tick >= static_cast<double>(next_tick)) {
                    break;
                }
                const double t = tempo_.seconds_at(std::llround(tick));
                if (t < from_s) {
                    continue;
                }
                if (t >= to_s) {
                    return;
                }
                TransportOp op;
                op.kind = TransportOp::Kind::Click;
                op.at_ns = ns_at(t);
                op.to_s = t;
                op.beat = label + k;
                op.sub = j;
                op.accent = settings.accent && j == 0 && op.beat == 1;
                out.push_back(op);
            }
        }
    }
}

bool BeatGrid::last_beat(double song_s, Beat* out) const {
    if (measures_.empty() || !(song_s >= 0.0)) {
        return false;
    }
    auto after = std::upper_bound(measures_.begin(), measures_.end(), song_s,
                                  [](double s, const Measure& m) { return s < m.start_s; });
    if (after == measures_.begin()) {
        return false;
    }
    const std::size_t i = static_cast<std::size_t>(after - measures_.begin()) - 1;
    const Measure& m = measures_[i];
    const bool has_next = i + 1 < measures_.size();
    const double spacing = ticks_per_beat(tempo_.ticks_per_quarter, m.numerator, m.denominator);
    int beats = beats_per_bar(m.numerator, m.denominator);
    int label = 1;
    if (m.number == 0 && has_next) {
        const int bar_beats = beats_per_bar(measures_[i + 1].numerator, measures_[i + 1].denominator);
        beats = std::max(1, static_cast<int>(std::llround(
                                static_cast<double>(measures_[i + 1].start_tick - m.start_tick) /
                                spacing)));
        label = std::max(1, bar_beats - beats + 1);
    }
    // The beat is the last whose time is at or before song_s: bisect on the tick the time maps to.
    const double tick = tempo_.tick_at(song_s);
    int k = static_cast<int>(std::floor((tick - static_cast<double>(m.start_tick)) / spacing));
    k = std::clamp(k, 0, beats - 1);
    // Guard the rounding of tick_at against the beat's own time.
    while (k > 0 &&
           tempo_.seconds_at(std::llround(static_cast<double>(m.start_tick) + k * spacing)) > song_s) {
        --k;
    }
    out->time_s = tempo_.seconds_at(std::llround(static_cast<double>(m.start_tick) + k * spacing));
    out->number = label + k;
    return out->time_s <= song_s;
}

namespace {

double elapsed_beats(const CountInInput& in) { return in.total_beats - in.beats_left; }

double beats_per_second(const CountInInput& in) {
    return std::max(1, in.speed_percent) / 100.0 / std::max(1e-6, in.beat_s);
}

} // namespace

void plan_count_in(const CountInInput& in, std::int64_t now_ns, std::int64_t window_ns,
                   const MetronomeSettings& settings, int& next_beat,
                   std::vector<TransportOp>& out) {
    if (in.total_beats <= 0) {
        return;
    }
    const double elapsed = elapsed_beats(in);
    const double bps = beats_per_second(in);
    const int numerator = std::max(1, in.numerator);
    for (; next_beat < in.total_beats; ++next_beat) {
        const double wait_s = (next_beat - elapsed) / bps; // negative: already due
        const auto wait_ns = static_cast<std::int64_t>(std::llround(wait_s * 1e9));
        if (wait_ns >= window_ns) {
            break;
        }
        TransportOp op;
        op.kind = TransportOp::Kind::Click;
        op.at_ns = now_ns + wait_ns;
        op.to_s = in.start_s - (in.total_beats - next_beat) * in.beat_s;
        op.beat = next_beat % numerator + 1;
        op.accent = settings.accent && op.beat == 1;
        op.count_in = true;
        out.push_back(op);
    }
}

int first_unsounded_count_in_beat(const CountInInput& in) {
    if (in.total_beats <= 0) {
        return 0;
    }
    return std::clamp(static_cast<int>(std::ceil(elapsed_beats(in) - 1e-9)), 0, in.total_beats);
}

int count_in_label(const CountInInput& in) {
    if (in.total_beats <= 0 || in.beats_left <= 0.0) {
        return 0;
    }
    const int reached = std::clamp(static_cast<int>(std::floor(elapsed_beats(in) + 1e-9)), 0,
                                   in.total_beats - 1);
    return reached % std::max(1, in.numerator) + 1;
}

} // namespace etude
