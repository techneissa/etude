// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_label_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude {
namespace {

// The shares of the root's size the geometry is cut from.
constexpr float kLineShare = 0.055f;
constexpr float kTickShare = 0.08f;
constexpr float kNameAirShare = 0.2f; // between the baseline and the ticks
constexpr float kMarginPt = 14.0f;
constexpr float kPadPt = 8.0f;

// A small fixed list of runs: the frame path allocates nothing.
template <std::size_t N> struct Runs {
    std::array<ChordSpan, N> items{};
    std::size_t count = 0;
    void add(ChordSpan s) {
        if (count < N) {
            items[count++] = s;
        }
    }
    std::span<const ChordSpan> view() const { return {items.data(), count}; }
};

bool free_of(float left, float width, std::span<const ChordSpan> a, std::span<const ChordSpan> b) {
    for (const std::span<const ChordSpan> list : {a, b}) {
        for (const ChordSpan& s : list) {
            if (left < s.x1 && left + width > s.x0) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

ChordLabelGeometry chord_label_geometry(const RectF& strip, float font_px, float ui_unit,
                                        float cap_em, float cap_top_em) {
    ChordLabelGeometry g;
    g.line_thickness = std::max(1.0f, std::round(kLineShare * font_px));
    g.tick_height = std::max(2.0f, std::round(kTickShare * font_px));
    g.line_y = std::round(strip.bottom() - g.line_thickness - 1.0f);
    g.baseline = std::round(g.line_y - g.tick_height - std::round(kNameAirShare * font_px));
    g.name_top = g.baseline - (cap_top_em + cap_em) * font_px;
    g.margin = std::round(kMarginPt * ui_unit);
    g.pad = std::round(kPadPt * ui_unit);
    return g;
}

std::size_t chord_label_blockers(const RectF& strip, std::span<const RectF> rects, float pad,
                                 std::span<ChordSpan> out) {
    std::size_t n = 0;
    for (const RectF& r : rects) {
        if (r.empty() || !(r.y < strip.bottom() && r.bottom() > strip.y) || n >= out.size()) {
            continue;
        }
        out[n++] = {r.x - pad, r.right() + pad};
    }
    return n;
}

void place_chord_names(std::span<const ChordNameWant> wants, float x_min, float x_max, float pad,
                       std::span<const ChordSpan> blocked, std::span<ChordNamePlace> out) {
    if (blocked.size() > kChordMaxBlockers) {
        blocked = blocked.first(kChordMaxBlockers);
    }
    Runs<kChordMaxNames> placed; // the groups done: later groups step aside from them
    Runs<kChordMaxNames> this_group;
    int group = wants.empty() ? 0 : wants.front().group;
    for (std::size_t i = 0; i < wants.size() && i < out.size() && i < kChordMaxNames; ++i) {
        const ChordNameWant& w = wants[i];
        if (w.group != group) {
            for (std::size_t k = 0; k < this_group.count; ++k) {
                placed.add(this_group.items[k]);
            }
            this_group.count = 0;
            group = w.group;
        }
        ChordNamePlace& p = out[i];
        p = {};
        if (!(w.width > 0.0f) || w.width > x_max - x_min) {
            continue;
        }
        const float preferred = std::clamp(w.centre - 0.5f * w.width, x_min, x_max - w.width);
        // The preferred place, then the places that just clear each obstacle on either side.
        std::array<float, 1 + 2 * (kChordMaxBlockers + kChordMaxNames)> candidates{};
        std::size_t n = 0;
        candidates[n++] = preferred;
        for (const std::span<const ChordSpan> list : {blocked, placed.view()}) {
            for (const ChordSpan& b : list) {
                candidates[n++] = b.x0 - w.width;
                candidates[n++] = b.x1;
            }
        }
        float best = preferred;
        float best_cost = 1e30f;
        for (std::size_t c = 0; c < n; ++c) {
            const float left = candidates[c];
            if (left < x_min || left + w.width > x_max ||
                !free_of(left, w.width, blocked, placed.view())) {
                continue;
            }
            const float cost = std::abs(left - preferred);
            if (cost < best_cost) {
                best = left;
                best_cost = cost;
                p.placed = true;
            }
        }
        p.left = best;
        if (p.placed) {
            this_group.add({best - pad, best + w.width + pad});
        }
    }
}

std::size_t chord_line_runs(float x0, float x1, std::span<const ChordSpan> blocked,
                            std::span<ChordSpan> out) {
    if (blocked.size() > kChordMaxBlockers) {
        blocked = blocked.first(kChordMaxBlockers);
    }
    // Each obstacle cuts a run in two at most, so the runs never pass obstacles + 1.
    Runs<kChordMaxBlockers + 1> parts;
    parts.add({x0, x1});
    for (const ChordSpan& b : blocked) {
        Runs<kChordMaxBlockers + 1> next;
        for (std::size_t i = 0; i < parts.count; ++i) {
            const ChordSpan& q = parts.items[i];
            if (q.x1 <= b.x0 || q.x0 >= b.x1) {
                next.add(q);
            } else {
                if (q.x0 < b.x0) {
                    next.add({q.x0, b.x0});
                }
                if (q.x1 > b.x1) {
                    next.add({b.x1, q.x1});
                }
            }
        }
        parts = next;
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < parts.count; ++i) {
        if (parts.items[i].x1 - parts.items[i].x0 >= 1.0f && n < out.size()) {
            out[n++] = parts.items[i];
        }
    }
    return n;
}

} // namespace etude
