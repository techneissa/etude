// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_target.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace etude {
namespace {

int bar_count(const SheetMap& map) {
    return static_cast<int>(std::min(map.bar_start.size(), map.bar_written.size()));
}

std::int64_t bar_end(const SheetMap& map, const Song& song, int k) {
    if (k + 1 < bar_count(map)) {
        return map.bar_start[static_cast<std::size_t>(k) + 1];
    }
    return std::max(song.end_tick, map.bar_start[static_cast<std::size_t>(k)] + 1);
}

// A practised note starts within a tick of `tick` (a chord, not a rest). Song::notes is sorted by
// start tick.
bool note_starts_at(const Song& song, double tick) {
    const auto first = static_cast<std::int64_t>(std::floor(tick)) - 1;
    auto it = std::lower_bound(song.notes.begin(), song.notes.end(), first,
                               [](const Note& n, std::int64_t t) { return n.start_tick < t; });
    for (; it != song.notes.end() && static_cast<double>(it->start_tick) <= tick + 1.0; ++it) {
        if (!it->drum && !it->accompaniment) {
            return true;
        }
    }
    return false;
}

int played_bar_at(const SheetMap& map, double tick) {
    const auto it = std::upper_bound(map.bar_start.begin(), map.bar_start.end(), tick,
                                     [](double t, std::int64_t s) { return t < static_cast<double>(s); });
    return it == map.bar_start.begin() ? 0 : static_cast<int>(it - map.bar_start.begin()) - 1;
}

} // namespace

int sheet_written_bar_at(const SheetLayout& layout, int system, float x) {
    int best = -1;
    float best_d = std::numeric_limits<float>::max();
    for (const SheetBar& b : layout.bars) {
        if (b.system != system) {
            continue;
        }
        if (x >= b.x0 && x < b.x1) {
            return b.bar;
        }
        const float d = std::min(std::fabs(x - b.x0), std::fabs(x - b.x1));
        if (d < best_d) {
            best_d = d;
            best = b.bar;
        }
    }
    return best;
}

int sheet_played_bar_for(const SheetMap& map, int written, double tick) {
    const int n = bar_count(map);
    if (n == 0 || written < 0) {
        return -1;
    }
    const int now = played_bar_at(map, tick);
    const int run = map.bar_run[static_cast<std::size_t>(now)];
    int next = -1;
    int before = -1;
    for (int k = 0; k < n; ++k) {
        if (map.bar_written[static_cast<std::size_t>(k)] != written) {
            continue;
        }
        if (map.bar_run[static_cast<std::size_t>(k)] == run) {
            return k;
        }
        if (k > now && next < 0) {
            next = k;
        } else if (k <= now) {
            before = k;
        }
    }
    return next >= 0 ? next : before;
}

int sheet_played_bar_near(const SheetMap& map, int written, int played) {
    const int n = bar_count(map);
    if (n == 0 || written < 0 || played < 0 || played >= n) {
        return -1;
    }
    const int run = map.bar_run[static_cast<std::size_t>(played)];
    for (int k = 0; k < n; ++k) {
        if (map.bar_written[static_cast<std::size_t>(k)] == written &&
            map.bar_run[static_cast<std::size_t>(k)] == run) {
            return k;
        }
    }
    return -1;
}

SheetLoopEnds sheet_loop_ends(const SheetMap& map, int anchor, int written) {
    SheetLoopEnds ends;
    const int n = bar_count(map);
    if (anchor < 0 || anchor >= n || written < 0) {
        return ends;
    }
    const int same = sheet_played_bar_near(map, written, anchor);
    if (same >= 0) {
        ends.anchor = anchor;
        ends.last = same;
        return ends;
    }
    // Another pass holding both bars, the one nearest the anchor.
    const int anchor_written = map.bar_written[static_cast<std::size_t>(anchor)];
    for (int k = 0; k < n; ++k) {
        if (map.bar_written[static_cast<std::size_t>(k)] != written) {
            continue;
        }
        const int a = sheet_played_bar_near(map, anchor_written, k);
        if (a >= 0 && (ends.anchor < 0 || std::abs(a - anchor) < std::abs(ends.anchor - anchor))) {
            ends.anchor = a;
            ends.last = k;
        }
    }
    return ends;
}

SheetTarget sheet_target_at(const SheetLayout& layout, const SheetMap& map, const Song& song,
                            int system, float x, double tick) {
    SheetTarget t;
    const int k = sheet_played_bar_for(map, sheet_written_bar_at(layout, system, x), tick);
    if (k < 0 || static_cast<std::size_t>(k) >= song.measures.size()) {
        return t;
    }
    const std::int64_t start = map.bar_start[static_cast<std::size_t>(k)];
    const std::int64_t end = bar_end(map, song, k);
    t.played_bar = k;
    t.beat_tick = start;
    t.onset_tick = start;

    // The bar's path points on this system, in tick order.
    const auto first = std::lower_bound(map.path.begin(), map.path.end(), static_cast<double>(start),
                                        [](const SheetCursorPoint& p, double v) { return p.tick < v; });
    double at = static_cast<double>(start);
    const SheetCursorPoint* prev = nullptr;
    float best_d = std::numeric_limits<float>::max();
    bool placed = false;
    for (auto it = first; it != map.path.end() && it->tick <= static_cast<double>(end); ++it) {
        if (it->system != system) {
            continue;
        }
        if (it->tick < static_cast<double>(end) && note_starts_at(song, it->tick)) {
            const float d = std::fabs(it->x - x);
            if (d < best_d) {
                best_d = d;
                t.onset_tick = static_cast<std::int64_t>(std::llround(it->tick));
            }
        }
        if (!placed) {
            if (prev == nullptr && x <= it->x) {
                at = it->tick;
                placed = true;
            } else if (prev != nullptr && x <= it->x) {
                const float span = it->x - prev->x;
                const double f = span > 0.0f ? static_cast<double>((x - prev->x) / span) : 0.0;
                at = prev->tick + std::clamp(f, 0.0, 1.0) * (it->tick - prev->tick);
                placed = true;
            }
        }
        prev = &*it;
    }
    if (!placed && prev != nullptr) {
        at = prev->tick;
    }

    // The nearest beat of this bar.
    const Measure& m = song.measures[static_cast<std::size_t>(k)];
    const int den = m.denominator > 0 ? m.denominator : 4;
    const std::int64_t beat =
        std::max<std::int64_t>(1, static_cast<std::int64_t>(song.tempo.ticks_per_quarter) * 4 / den);
    const auto n = static_cast<std::int64_t>(std::llround((at - static_cast<double>(start)) /
                                                           static_cast<double>(beat)));
    const std::int64_t last = start + (end - 1 - start) / beat * beat;
    t.beat_tick = std::clamp(start + n * beat, start, last);
    return t;
}

SheetLoopRange sheet_loop_range(const SheetMap& map, const Song& song, int a, int b) {
    SheetLoopRange r;
    const int n = bar_count(map);
    if (a < 0 || b < 0 || a >= n || b >= n) {
        return r;
    }
    r.first_bar = std::min(a, b);
    r.last_bar = std::max(a, b);
    r.start_tick = map.bar_start[static_cast<std::size_t>(r.first_bar)];
    r.end_tick = bar_end(map, song, r.last_bar);
    return r;
}

} // namespace etude
