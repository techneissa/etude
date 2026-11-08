// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/tempo_control.hpp"

#include "etude/meter.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

double bpm_of(const TempoChange& c) { return 60'000'000.0 / std::max<std::uint32_t>(1, c.us_per_quarter); }

} // namespace

double song_bpm_at(const Song& song, double song_s) {
    const std::vector<TempoChange>& changes = song.tempo.changes;
    if (changes.empty()) {
        return 120.0;
    }
    auto after = std::upper_bound(changes.begin(), changes.end(), song_s,
                                  [](double s, const TempoChange& c) { return s < c.seconds; });
    return bpm_of(after == changes.begin() ? changes.front() : *(after - 1));
}

double main_bpm(const Song& song) {
    const std::vector<TempoChange>& changes = song.tempo.changes;
    if (changes.empty()) {
        return 120.0;
    }
    double best = bpm_of(changes.front());
    double best_span = -1.0;
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const double end = i + 1 < changes.size() ? changes[i + 1].seconds : song.duration_s;
        const double span = std::max(0.0, std::min(end, song.duration_s) - changes[i].seconds);
        if (span > best_span + 1e-9) {
            best_span = span;
            best = bpm_of(changes[i]);
        }
    }
    return best;
}

BeatUnit beat_unit_at(const Song& song, double song_s) {
    int numerator = 4;
    int denominator = 4;
    const std::vector<Measure>& bars = song.measures;
    if (!bars.empty()) {
        auto after = std::upper_bound(bars.begin(), bars.end(), song_s,
                                      [](double s, const Measure& m) { return s < m.start_s; });
        const Measure& m = after == bars.begin() ? bars.front() : *(after - 1);
        numerator = m.numerator;
        denominator = m.denominator;
    } else if (!song.time_signatures.empty()) {
        numerator = song.time_signatures.front().numerator;
        denominator = song.time_signatures.front().denominator;
    }
    BeatUnit unit;
    unit.quarters = chip_beat_quarters(numerator, denominator);
    unit.dotted = is_compound_meter(numerator, denominator);
    return unit;
}

double effective_bpm(const Song& song, double song_s, int speed_percent) {
    return song_bpm_at(song, song_s) / beat_unit_at(song, song_s).quarters *
           std::max(1, speed_percent) / 100.0;
}

double main_beat_bpm(const Song& song, double song_s) {
    return main_bpm(song) / beat_unit_at(song, song_s).quarters;
}

int speed_for_bpm(double target_bpm, double song_bpm) {
    if (!(target_bpm > 0.0) || !(song_bpm > 0.0) || !std::isfinite(target_bpm) ||
        !std::isfinite(song_bpm)) {
        return 100;
    }
    const double percent = target_bpm / song_bpm * 100.0;
    return clamp_speed_percent(static_cast<int>(std::lround(std::min(percent, 10'000.0))));
}

int speed_for_target(const Song& song, double target_bpm, double song_s) {
    return speed_for_bpm(target_bpm, main_beat_bpm(song, song_s));
}

BpmRange bpm_range(const Song& song, double song_s) {
    const double main = main_beat_bpm(song, song_s);
    return {main * kMinSpeedPercent / 100.0, main * kMaxSpeedPercent / 100.0};
}

int display_bpm(double bpm) {
    if (!(bpm > 0.0) || !std::isfinite(bpm)) {
        return 1;
    }
    return std::max(1, static_cast<int>(std::lround(std::min(bpm, 99'999.0))));
}

int step_speed(int percent, int delta_steps) {
    const int p = clamp_speed_percent(percent);
    const int step = kSpeedStepPercent;
    int target = p;
    if (delta_steps > 0) {
        target = (p / step + delta_steps) * step; // the next multiple above, then further steps
    } else if (delta_steps < 0) {
        const int up = (p + step - 1) / step * step; // p rounded up to a multiple
        target = up + delta_steps * step;
    }
    return clamp_speed_percent(target);
}

} // namespace etude
