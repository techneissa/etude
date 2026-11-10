// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/roll_view.hpp"

#include "etude/playback.hpp"
#include "etude/transport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace etude {

namespace {

constexpr double kEps = 1e-9;

double song_length(const Song& song) {
    return std::isfinite(song.duration_s) && song.duration_s > 0.0 ? song.duration_s : 0.0;
}

// Index of the bar holding `seconds` (the last bar starting at or before it).
std::size_t bar_index_at(const Song& song, double seconds) {
    const auto& bars = song.measures;
    auto it = std::upper_bound(bars.begin(), bars.end(), seconds + kEps,
                               [](double v, const Measure& m) { return v < m.start_s; });
    return it == bars.begin() ? 0 : static_cast<std::size_t>(it - bars.begin()) - 1;
}

double bar_end_s(const Song& song, std::size_t i) {
    return i + 1 < song.measures.size() ? song.measures[i + 1].start_s : song_length(song);
}

std::int64_t bar_end_tick(const Song& song, std::size_t i) {
    return i + 1 < song.measures.size() ? song.measures[i + 1].start_tick
                                        : std::max(song.end_tick, song.measures[i].start_tick + 1);
}

// Ticks in one beat of the bar: a beat is one note of the denominator.
std::int64_t beat_ticks(const Song& song, const Measure& m) {
    const int den = std::max<int>(1, m.denominator);
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(song.tempo.ticks_per_quarter) * 4 /
                                         den);
}

RollView clamped_view(const Song& song, double start_s, double span_s) {
    const double length = song_length(song);
    RollView v;
    if (!(length > 0.0)) {
        v.start_s = 0.0;
        v.end_s = 1.0;
        return v;
    }
    span_s = std::clamp(span_s, std::min(kEps * 10.0, length), length);
    v.start_s = std::clamp(start_s, 0.0, length - span_s);
    v.end_s = v.start_s + span_s;
    return v;
}

} // namespace

// ---- Window

RollView whole_song_view(const Song& song) {
    RollView v;
    v.start_s = 0.0;
    v.end_s = song_length(song) > 0.0 ? song_length(song) : 1.0;
    return v;
}

double min_view_span(const Song& song, double around_s) {
    const double length = song_length(song);
    if (song.measures.empty()) {
        return std::min(kMinZoomSeconds, std::max(length, 1.0));
    }
    const std::size_t i = bar_index_at(song, std::clamp(around_s, 0.0, length));
    const std::size_t j = std::min(i + kMinZoomBars, song.measures.size());
    const double span = (j < song.measures.size() ? song.measures[j].start_s : length) -
                        song.measures[i].start_s;
    // Near the end there may be fewer than two bars left: use the bars before it.
    if (span < bar_end_s(song, i) - song.measures[i].start_s + kEps && i > 0) {
        const std::size_t h = i - 1;
        return std::min(length, bar_end_s(song, i) - song.measures[h].start_s);
    }
    return std::min(length, span);
}

RollView zoom_view(const Song& song, const RollView& view, double factor, double focus_s) {
    const double length = song_length(song);
    if (!(length > 0.0) || !(factor > 0.0) || std::isnan(factor)) {
        return view;
    }
    const double span = view.span_s();
    const double frac = span > 0.0 ? std::clamp((focus_s - view.start_s) / span, 0.0, 1.0) : 0.5;
    const double focus = view.start_s + frac * span;
    const double lo = min_view_span(song, focus);
    const double new_span = std::clamp(span / factor, std::min(lo, length), length);
    return clamped_view(song, focus - frac * new_span, new_span);
}

RollView pan_view(const Song& song, const RollView& view, double delta_s) {
    if (std::isnan(delta_s)) {
        return view;
    }
    return clamped_view(song, view.start_s + delta_s, view.span_s());
}

RollView view_around(const Song& song, double start_s, double end_s, double margin) {
    const double length = song_length(song);
    if (!(length > 0.0) || std::isnan(start_s) || std::isnan(end_s)) {
        return whole_song_view(song);
    }
    if (end_s < start_s) {
        std::swap(start_s, end_s);
    }
    const double pad = (end_s - start_s) * std::max(0.0, margin);
    const double mid = (start_s + end_s) * 0.5;
    double span = (end_s - start_s) + 2.0 * pad;
    span = std::max(span, std::min(min_view_span(song, mid), length));
    return clamped_view(song, mid - span * 0.5, span);
}

int bars_in_view(const Song& song, const RollView& view) {
    if (song.measures.empty()) {
        return 0;
    }
    const std::size_t a = bar_index_at(song, view.start_s);
    const std::size_t b = bar_index_at(song, std::max(view.start_s, view.end_s - 1e-6));
    return static_cast<int>(b - a + 1);
}

bool beat_snap_zoom(const Song& song, const RollView& view) {
    const int bars = bars_in_view(song, view);
    return bars > 0 && bars <= kBeatSnapBars;
}

// ---- Grid

void grid_lines(const Song& song, double start_s, double end_s, bool beats,
                std::vector<GridLine>& out) {
    out.clear();
    if (song.measures.empty() || !(end_s > start_s)) {
        return;
    }
    for (std::size_t i = bar_index_at(song, start_s); i < song.measures.size(); ++i) {
        const Measure& m = song.measures[i];
        if (m.start_s > end_s + kEps) {
            break;
        }
        if (m.start_s >= start_s - kEps) {
            out.push_back({m.start_s, true, m.number, 1, m.written_number()});
        }
        if (!beats) {
            continue;
        }
        const std::int64_t step = beat_ticks(song, m);
        const std::int64_t stop = bar_end_tick(song, i);
        int beat = 2;
        for (std::int64_t tick = m.start_tick + step; tick < stop; tick += step, ++beat) {
            const double t = song.tempo.seconds_at(tick);
            if (t > end_s + kEps) {
                break;
            }
            if (t >= start_s - kEps) {
                out.push_back({t, false, m.number, beat, m.written_number()});
            }
        }
    }
}

double snap_to_grid(const Song& song, double seconds, bool beats) {
    const double length = song_length(song);
    const double t = std::clamp(std::isnan(seconds) ? 0.0 : seconds, 0.0, length);
    if (song.measures.empty()) {
        return t;
    }
    const double bar = snap_to_measure(song, t, SnapMode::Nearest);
    if (!beats) {
        return bar;
    }
    const std::size_t i = bar_index_at(song, t);
    const Measure& m = song.measures[i];
    const std::int64_t step = beat_ticks(song, m);
    const std::int64_t stop = bar_end_tick(song, i);
    double best = bar;
    double best_d = std::fabs(bar - t);
    for (std::int64_t tick = m.start_tick; tick <= stop; tick += step) {
        const double b = std::min(song.tempo.seconds_at(tick), length);
        const double d = std::fabs(b - t);
        if (d < best_d) {
            best_d = d;
            best = b;
        }
    }
    return best;
}

std::string position_label(const Song& song, double seconds) {
    const double length = song_length(song);
    const double t = std::clamp(std::isnan(seconds) ? 0.0 : seconds, 0.0, length);
    if (song.measures.empty()) {
        const int total = static_cast<int>(t);
        char buf[24];
        std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
        return buf;
    }
    const std::size_t i = bar_index_at(song, t);
    const Measure& m = song.measures[i];
    const std::int64_t step = beat_ticks(song, m);
    const double tick = song.tempo.tick_at(t);
    // A time a hair before a beat line counts as on it (snapped times carry rounding).
    const auto beat = static_cast<int>(std::floor((tick - static_cast<double>(m.start_tick)) /
                                                      static_cast<double>(step) +
                                                  1e-6)) +
                      1;
    const int numerator = std::max<int>(1, m.numerator);
    std::string label = "Bar " + std::to_string(m.number);
    if (m.written_number() != m.number) {
        label += " \xC2\xB7 sheet bar " + std::to_string(m.written_number());
    }
    return label + " \xC2\xB7 beat " + std::to_string(std::clamp(beat, 1, numerator));
}

// ---- Thumbnail

PitchRange song_pitch_range(const Song& song) {
    int lo = 127;
    int hi = 0;
    for (const Note& n : song.notes) {
        if (!n.drum) {
            lo = std::min<int>(lo, n.pitch);
            hi = std::max<int>(hi, n.pitch);
        }
    }
    if (hi < lo) {
        return PitchRange{36, 96};
    }
    constexpr int kMinRows = 25;
    while (hi - lo + 1 < kMinRows) {
        if (lo > 0) {
            --lo;
        }
        if (hi - lo + 1 < kMinRows && hi < 127) {
            ++hi;
        }
        if (lo == 0 && hi == 127) {
            break;
        }
    }
    return PitchRange{lo, hi};
}

PitchRange window_pitch_range(const Song& song, double t0_s, double t1_s, int min_rows) {
    int lo = 127;
    int hi = 0;
    for (const Note& n : song.notes) {
        if (n.start_s >= t1_s) {
            break;
        }
        if (n.drum || n.start_s + std::max(n.duration_s, kMinNoteSeconds) <= t0_s) {
            continue;
        }
        lo = std::min<int>(lo, n.pitch);
        hi = std::max<int>(hi, n.pitch);
    }
    if (hi < lo) {
        return song_pitch_range(song);
    }
    min_rows = std::clamp(min_rows, 2, 128);
    while (hi - lo + 1 < min_rows) {
        if (lo > 0) {
            --lo;
        }
        if (hi - lo + 1 < min_rows && hi < 127) {
            ++hi;
        }
        if (lo == 0 && hi == 127) {
            break;
        }
    }
    return PitchRange{lo, hi};
}

void roll_note_rects(const Song& song, double t0_s, double t1_s, int w, int h,
                     const PitchRange& pitches, double split_s, std::vector<RollRect>& out) {
    out.clear();
    if (w <= 0 || h <= 0 || !(t1_s > t0_s)) {
        return;
    }
    const double sx = static_cast<double>(w) / (t1_s - t0_s);
    const int rows = std::max(1, pitches.rows());
    const float row_h = static_cast<float>(h) / static_cast<float>(rows);
    for (const Note& n : song.notes) {
        if (n.start_s >= t1_s) {
            break; // sorted by start
        }
        const double end = n.start_s + std::max(n.duration_s, kMinNoteSeconds);
        if (n.drum || end <= t0_s) {
            continue;
        }
        // A pitch outside the rows moves by the fewest whole octaves that bring it in, like a note
        // outside the keyboard, and carries the fold marker. A range under an octave may have no
        // row for the pitch class; that note is left out.
        int pitch = n.pitch;
        int fold = 0;
        if (pitch < pitches.lowest) {
            fold = (pitches.lowest - pitch + 11) / 12;
        } else if (pitch > pitches.highest) {
            fold = -((pitch - pitches.highest + 11) / 12);
        }
        pitch += fold * 12;
        if (pitch < pitches.lowest || pitch > pitches.highest) {
            continue;
        }
        RollRect r;
        r.x0 = static_cast<float>(std::max(0.0, (n.start_s - t0_s) * sx));
        r.x1 = static_cast<float>(std::min(static_cast<double>(w), (end - t0_s) * sx));
        if (r.x1 - r.x0 < 1.0f) {
            r.x1 = std::min(static_cast<float>(w), r.x0 + 1.0f);
            r.x0 = r.x1 - 1.0f;
        }
        r.y0 = static_cast<float>(pitches.highest - pitch) * row_h;
        r.y1 = r.y0 + std::max(1.0f, row_h);
        r.hand = n.hand;
        r.side = end <= split_s ? -1 : (n.start_s >= split_s ? 1 : 0);
        r.fold = static_cast<std::int8_t>(std::clamp(fold, -11, 11));
        out.push_back(r);
    }
}

RollTile tile_for_view(const Song& song, const RollView& view, int view_w_px, int h_px) {
    RollTile t;
    const double length = song_length(song);
    if (!(length > 0.0) || view_w_px <= 0 || h_px <= 0 || !(view.span_s() > 0.0)) {
        return t;
    }
    const double span = view.span_s();
    const double density = static_cast<double>(view_w_px) / span;
    double t0 = std::max(0.0, view.start_s - span);
    double t1 = std::min(length, view.end_s + span);
    double w = (t1 - t0) * density;
    if (w > kMaxTileWidth) {
        // Keep the view in the middle of what fits.
        const double fit = kMaxTileWidth / density;
        const double mid = (view.start_s + view.end_s) * 0.5;
        t0 = std::clamp(mid - fit * 0.5, 0.0, std::max(0.0, length - fit));
        t1 = std::min(length, t0 + fit);
        w = (t1 - t0) * density;
    }
    t.t0_s = t0;
    t.t1_s = t1;
    t.w = std::max(1, static_cast<int>(std::lround(w)));
    t.h = h_px;
    return t;
}

bool tile_stale(const RollTile& tile, const RollView& view, int view_w_px) {
    if (!tile.valid() || view_w_px <= 0 || !(view.span_s() > 0.0)) {
        return true;
    }
    constexpr double kSlack = 1e-6;
    if (view.start_s < tile.t0_s - kSlack || view.end_s > tile.t1_s + kSlack) {
        return true;
    }
    const double need = static_cast<double>(view_w_px) / view.span_s();
    const double ratio = tile.px_per_s() / need;
    return ratio < 0.7 || ratio > 1.6;
}

void tile_uv(const RollTile& tile, const RollView& view, float& u0, float& u1) {
    const double span = tile.t1_s - tile.t0_s;
    if (!(span > 0.0)) {
        u0 = 0.0f;
        u1 = 1.0f;
        return;
    }
    u0 = static_cast<float>(std::clamp((view.start_s - tile.t0_s) / span, 0.0, 1.0));
    u1 = static_cast<float>(std::clamp((view.end_s - tile.t0_s) / span, 0.0, 1.0));
}

} // namespace etude
