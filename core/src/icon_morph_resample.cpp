// SPDX-License-Identifier: GPL-3.0-or-later
// Morphing stroke icons, after the technique of morphicons (https://github.com/guillermolg00/

#include "icon_morph_internal.hpp"
// morphicons, MIT License, Copyright (c) 2026 Guillermo). A port of the technique, not the code.
#include "etude/icon_morph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace etude {

using namespace morph_detail;

namespace {

// 8-point Gauss-Legendre nodes and weights on [-1, 1].
constexpr double kGlX[8] = {-0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
                            -0.1834346424956498, 0.1834346424956498,  0.5255324099163290,
                            0.7966664774136267,  0.9602898564975363};
constexpr double kGlW[8] = {0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
                            0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
                            0.2223810344533745, 0.1012285362903763};
MorphPoint point_at(const Cubic& c, double t) {
    const double u = 1.0 - t;
    const double b0 = u * u * u;
    const double b1 = 3.0 * u * u * t;
    const double b2 = 3.0 * u * t * t;
    const double b3 = t * t * t;
    return {static_cast<float>(b0 * c.p0.x + b1 * c.c1.x + b2 * c.c2.x + b3 * c.p3.x),
            static_cast<float>(b0 * c.p0.y + b1 * c.c1.y + b2 * c.c2.y + b3 * c.p3.y)};
}
double speed_at(const Cubic& c, double t) {
    const double u = 1.0 - t;
    const double dx = 3.0 * (u * u * (c.c1.x - c.p0.x) + 2.0 * u * t * (c.c2.x - c.c1.x) +
                             t * t * (c.p3.x - c.c2.x));
    const double dy = 3.0 * (u * u * (c.c1.y - c.p0.y) + 2.0 * u * t * (c.c2.y - c.c1.y) +
                             t * t * (c.p3.y - c.c2.y));
    return std::sqrt(dx * dx + dy * dy);
}
// Arc length from 0 to `t`.
double length_to(const Cubic& c, double t) {
    double sum = 0.0;
    for (int i = 0; i < 8; ++i) {
        sum += kGlW[i] * speed_at(c, (kGlX[i] + 1.0) * 0.5 * t);
    }
    return sum * 0.5 * t;
}
// The parameter at arc length `s` along `c` (of length `total`).
double param_at(const Cubic& c, double s, double total) {
    if (s <= 0.0 || total <= 0.0) {
        return 0.0;
    }
    if (s >= total) {
        return 1.0;
    }
    double lo = 0.0;
    double hi = 1.0;
    double t = s / total;
    for (int i = 0; i < 12; ++i) {
        const double f = length_to(c, t) - s;
        if (std::fabs(f) < 1e-9) {
            break;
        }
        if (f > 0.0) {
            hi = t;
        } else {
            lo = t;
        }
        const double d = speed_at(c, t);
        double next = d > 1e-12 ? t - f / d : (lo + hi) * 0.5;
        if (next <= lo || next >= hi) {
            next = (lo + hi) * 0.5; // Newton left the bracket: bisect
        }
        t = next;
    }
    return t;
}
struct Dir {
    double x = 0.0;
    double y = 0.0;
};
Dir direction(MorphPoint from, MorphPoint to) {
    return {static_cast<double>(to.x) - from.x, static_cast<double>(to.y) - from.y};
}
bool zero(Dir d) { return d.x * d.x + d.y * d.y < 1e-18; }
Dir tangent_out(const Cubic& c) {
    Dir d = direction(c.c2, c.p3);
    if (zero(d)) {
        d = direction(c.c1, c.p3);
    }
    if (zero(d)) {
        d = direction(c.p0, c.p3);
    }
    return d;
}
Dir tangent_in(const Cubic& c) {
    Dir d = direction(c.p0, c.c1);
    if (zero(d)) {
        d = direction(c.p0, c.c2);
    }
    if (zero(d)) {
        d = direction(c.p0, c.p3);
    }
    return d;
}
bool is_corner(const Cubic& before, const Cubic& after) {
    const Dir a = tangent_out(before);
    const Dir b = tangent_in(after);
    if (zero(a) || zero(b)) {
        return false;
    }
    const double angle = std::atan2(std::fabs(a.x * b.y - a.y * b.x), a.x * b.x + a.y * b.y);
    return angle > kCornerRadians;
}
void resample_subpath(const CubicPath& path, const CubicSubpath& sp, MorphSubpath& out) {
    constexpr int kIntervals = kMorphSamples - 1;
    const Cubic* cubics = path.cubics.data() + sp.first;
    const int m = sp.count;
    out.closed = sp.closed;
    // Cumulative lengths: cum[j] is where cubic j starts.
    std::array<double, kMaxCubics + 1> cum{};
    std::array<double, kMaxCubics> len{};
    for (int j = 0; j < m; ++j) {
        len[static_cast<std::size_t>(j)] = length_to(cubics[j], 1.0);
        cum[static_cast<std::size_t>(j) + 1] =
            cum[static_cast<std::size_t>(j)] + len[static_cast<std::size_t>(j)];
    }
    const double total = cum[static_cast<std::size_t>(m)];
    if (m == 0 || total < 1e-9) {
        const MorphPoint p = m > 0 ? cubics[0].p0 : MorphPoint{};
        out.pts.fill(p);
        out.length = 0.0f;
        out.centroid = p;
        return;
    }
    // Anchors: arc positions sampled exactly, each with the joint's own point. A loop's positions
    // run past the end and wrap.
    std::array<double, kMaxCubics + 2> anchor{};
    std::array<MorphPoint, kMaxCubics + 2> anchor_pt{};
    int anchors = 0;
    const auto add = [&](double s, MorphPoint p) {
        anchor[static_cast<std::size_t>(anchors)] = s;
        anchor_pt[static_cast<std::size_t>(anchors)] = p;
        ++anchors;
    };
    if (!sp.closed) {
        add(0.0, cubics[0].p0);
        for (int j = 0; j + 1 < m; ++j) {
            if (is_corner(cubics[j], cubics[j + 1])) {
                add(cum[static_cast<std::size_t>(j) + 1], cubics[j].p3);
            }
        }
        add(total, cubics[m - 1].p3);
    } else {
        // In loop order: the joint where the loop closes sits at 0, the others follow.
        if (is_corner(cubics[m - 1], cubics[0])) {
            add(0.0, cubics[0].p0);
        }
        for (int j = 0; j + 1 < m; ++j) {
            if (is_corner(cubics[j], cubics[j + 1])) {
                add(cum[static_cast<std::size_t>(j) + 1], cubics[j].p3);
            }
        }
        if (anchors == 0) {
            add(0.0, cubics[0].p0); // a smooth loop: any start will do
        }
        add(anchor[0] + total, anchor_pt[0]);
    }
    int runs = anchors - 1;
    if (runs > kIntervals) {
        // More corners than samples: plain arc-length sampling from the first anchor.
        anchor[1] = anchor[0] + total;
        anchor_pt[1] = anchor_pt[0];
        anchors = 2;
        runs = 1;
    }
    // Intervals per run: one each, the rest shared by length with the largest remainders.
    std::array<int, kMaxCubics + 1> share{};
    std::array<double, kMaxCubics + 1> rest{};
    int given = 0;
    for (int r = 0; r < runs; ++r) {
        const double run_len =
            anchor[static_cast<std::size_t>(r) + 1] - anchor[static_cast<std::size_t>(r)];
        const double ideal = (kIntervals - runs) * run_len / total;
        share[static_cast<std::size_t>(r)] = 1 + static_cast<int>(std::floor(ideal));
        rest[static_cast<std::size_t>(r)] = ideal - std::floor(ideal);
        given += share[static_cast<std::size_t>(r)];
    }
    // Equal remainders: on an open path the earlier run; on a loop the run that starts higher on
    // the shape (then further left), never by where the path starts, so a symmetric loop samples
    // the same whatever its start.
    const auto first = [&](int r, int than) {
        const double d = rest[static_cast<std::size_t>(r)] - rest[static_cast<std::size_t>(than)];
        if (std::fabs(d) > 1e-9 || !sp.closed) {
            return d > 1e-9;
        }
        const MorphPoint p = anchor_pt[static_cast<std::size_t>(r)];
        const MorphPoint q = anchor_pt[static_cast<std::size_t>(than)];
        return std::fabs(p.y - q.y) > 1e-5f ? p.y < q.y : p.x < q.x;
    };
    while (given < kIntervals) {
        int best = 0;
        for (int r = 1; r < runs; ++r) {
            if (first(r, best)) {
                best = r;
            }
        }
        ++share[static_cast<std::size_t>(best)];
        rest[static_cast<std::size_t>(best)] = -1.0;
        ++given;
    }
    const auto sample = [&](double s) {
        if (s >= total) {
            s -= total;
        }
        int j = 0;
        while (j + 1 < m && cum[static_cast<std::size_t>(j) + 1] <= s) {
            ++j;
        }
        const Cubic& c = cubics[j];
        return point_at(
            c, param_at(c, s - cum[static_cast<std::size_t>(j)], len[static_cast<std::size_t>(j)]));
    };
    int k = 0;
    for (int r = 0; r < runs; ++r) {
        const double a = anchor[static_cast<std::size_t>(r)];
        const double b = anchor[static_cast<std::size_t>(r) + 1];
        const int n = share[static_cast<std::size_t>(r)];
        out.pts[static_cast<std::size_t>(k++)] = anchor_pt[static_cast<std::size_t>(r)];
        for (int i = 1; i < n; ++i) {
            out.pts[static_cast<std::size_t>(k++)] = sample(a + (b - a) * i / n);
        }
    }
    out.pts[static_cast<std::size_t>(k)] = anchor_pt[static_cast<std::size_t>(anchors - 1)];
    if (sp.closed) {
        out.pts.back() = out.pts.front();
    }
    refresh_metrics(out);
    out.length = static_cast<float>(total);
}

} // namespace

void refresh_metrics(MorphSubpath& s) {
    const int distinct = s.closed ? kMorphSamples - 1 : kMorphSamples;
    double sx = 0.0;
    double sy = 0.0;
    for (int i = 0; i < distinct; ++i) {
        sx += s.pts[static_cast<std::size_t>(i)].x;
        sy += s.pts[static_cast<std::size_t>(i)].y;
    }
    s.centroid = {static_cast<float>(sx / distinct), static_cast<float>(sy / distinct)};
    double len = 0.0;
    for (int i = 0; i + 1 < kMorphSamples; ++i) {
        const Dir d =
            direction(s.pts[static_cast<std::size_t>(i)], s.pts[static_cast<std::size_t>(i) + 1]);
        len += std::sqrt(d.x * d.x + d.y * d.y);
    }
    s.length = static_cast<float>(len);
}

void resample(const CubicPath& path, MorphShape& out) {
    out.count = 0;
    for (int i = 0; i < path.subpath_count; ++i) {
        resample_subpath(path, path.subpaths[static_cast<std::size_t>(i)],
                         out.subpaths[static_cast<std::size_t>(out.count++)]);
    }
}

} // namespace etude
