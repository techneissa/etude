// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/beat_lines.hpp"

#include "etude/meter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace etude {
namespace {

// The bar's end: the next bar's start, or a full bar of its meter for the last one.
std::int64_t bar_end(const Song& song, std::size_t i) {
    if (i + 1 < song.measures.size()) {
        return song.measures[i + 1].start_tick;
    }
    const Measure& m = song.measures[i];
    const double quarters = 4.0 * m.numerator / std::max<int>(1, m.denominator);
    return m.start_tick +
           static_cast<std::int64_t>(std::llround(quarters * song.tempo.ticks_per_quarter));
}

// The lines of one level in the window. False when they would crowd closer than the minimum gap
// or overflow `out`.
bool lines_at(const Song& song, const BeatLineWindow& w, BeatLines level, std::span<float> out,
              std::size_t* count) {
    *count = 0;
    const double end_s = w.now_s + w.ahead_s;
    const double min_gap_s = static_cast<double>(w.min_gap_px) / w.area_px * w.ahead_s;
    // The first bar that may hold a line in the window: the last one starting at or before now.
    const auto& bars = song.measures;
    auto it = std::upper_bound(bars.begin(), bars.end(), w.now_s,
                               [](double t, const Measure& m) { return t < m.start_s; });
    std::size_t i = it == bars.begin() ? 0 : static_cast<std::size_t>(it - bars.begin()) - 1;
    double last_s = -1e300;
    for (; i < bars.size() && bars[i].start_s <= end_s; ++i) {
        const Measure& m = bars[i];
        const std::int64_t start = m.start_tick;
        const std::int64_t end = bar_end(song, i);
        const double beat =
            ticks_per_beat(song.tempo.ticks_per_quarter, m.numerator, m.denominator);
        const double step = level == BeatLines::Bars        ? static_cast<double>(end - start)
                            : level == BeatLines::HalfBeats ? 0.5 * beat
                                                            : beat;
        if (!(step > 0.0)) {
            continue;
        }
        // Counted from the bar's start; a pickup bar counts back from its end, so its beats fall
        // where the full bar's would.
        const double first =
            m.number == 0 && level != BeatLines::Bars
                ? static_cast<double>(end) -
                      std::floor(static_cast<double>(end - start) / step + 1e-9) * step
                : static_cast<double>(start);
        for (double tick = first; tick < static_cast<double>(end) - 0.5; tick += step) {
            const double t = song.tempo.seconds_at(static_cast<std::int64_t>(std::llround(tick)));
            if (t <= w.now_s) {
                last_s = t;
                continue;
            }
            if (t > end_s) {
                return true;
            }
            if (t - last_s < min_gap_s) {
                return false;
            }
            last_s = t;
            if (*count == out.size()) {
                return false;
            }
            out[(*count)++] = static_cast<float>((t - w.now_s) / w.ahead_s);
        }
    }
    return true;
}

BeatLines coarser(BeatLines level) {
    switch (level) {
    case BeatLines::HalfBeats:
        return BeatLines::Beats;
    case BeatLines::Beats:
        return BeatLines::Bars;
    default:
        return BeatLines::Off;
    }
}

} // namespace

BeatLineResult beat_line_heights(const Song& song, const BeatLineWindow& window,
                                 std::span<float> out) {
    BeatLineResult r;
    if (window.mode == BeatLines::Off || song.measures.empty() || !(window.area_px > 0.0f) ||
        !(window.ahead_s > 0.0) || !std::isfinite(window.now_s)) {
        return r;
    }
    for (BeatLines level = window.mode; level != BeatLines::Off; level = coarser(level)) {
        std::size_t count = 0;
        if (lines_at(song, window, level, out, &count)) {
            r.count = count;
            r.level = level;
            return r;
        }
    }
    return r;
}

} // namespace etude
