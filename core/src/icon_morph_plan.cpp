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

struct Cloud {
    const MorphPoint* a;
    const MorphPoint* b;
    int n;
};
Procrustes fit(const Cloud* clouds, int count) {
    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    int total = 0;
    for (int k = 0; k < count; ++k) {
        for (int i = 0; i < clouds[k].n; ++i) {
            ax += clouds[k].a[i].x;
            ay += clouds[k].a[i].y;
            bx += clouds[k].b[i].x;
            by += clouds[k].b[i].y;
        }
        total += clouds[k].n;
    }
    Procrustes p;
    if (total == 0) {
        return p;
    }
    ax /= total;
    ay /= total;
    bx /= total;
    by /= total;
    p.ca = {static_cast<float>(ax), static_cast<float>(ay)};
    p.cb = {static_cast<float>(bx), static_cast<float>(by)};
    double sxx = 0.0, sxy = 0.0, syx = 0.0, syy = 0.0, saa = 0.0, sbb = 0.0;
    for (int k = 0; k < count; ++k) {
        for (int i = 0; i < clouds[k].n; ++i) {
            const double x0 = clouds[k].a[i].x - ax;
            const double y0 = clouds[k].a[i].y - ay;
            const double x1 = clouds[k].b[i].x - bx;
            const double y1 = clouds[k].b[i].y - by;
            sxx += x0 * x1;
            sxy += x0 * y1;
            syx += y0 * x1;
            syy += y0 * y1;
            saa += x0 * x0 + y0 * y0;
            sbb += x1 * x1 + y1 * y1;
        }
    }
    constexpr double kTiny = 1e-12;
    if (saa < kTiny || sbb < kTiny) {
        // A point on one side: no rotation or scale to find, only translation.
        p.residual = saa < kTiny && sbb < kTiny ? 0.0f : 1.0f;
        return p;
    }
    const double angle = std::atan2(sxy - syx, sxx + syy);
    const double dot = std::cos(angle) * (sxx + syy) + std::sin(angle) * (sxy - syx);
    p.angle = static_cast<float>(angle);
    p.scale = static_cast<float>(std::clamp(dot / saa, 1e-4, 1e4));
    p.residual = static_cast<float>(std::sqrt(std::max(0.0, sbb - dot * dot / saa) / sbb));
    return p;
}
// The points of a loop (kMorphSamples - 1 distinct, the last repeating the first) from `start`,
// forward or backward, closed again at the end.
void loop_order(const MorphPoint* in, int start, bool backward, MorphPoint* out) {
    constexpr int n = kMorphSamples - 1;
    for (int i = 0; i < n; ++i) {
        const int j = backward ? ((start - i) % n + n) % n : (start + i) % n;
        out[i] = in[j];
    }
    out[n] = out[0];
}
void reversed(const MorphPoint* in, MorphPoint* out) {
    for (int i = 0; i < kMorphSamples; ++i) {
        out[i] = in[kMorphSamples - 1 - i];
    }
}
// The ways to line up a pair's samples: both directions of b and, when either is a loop, every
// start point of the loop.
constexpr int kMaxOrders = (kMorphSamples - 1) * 2;
int order_count(const MorphSubpath& a, const MorphSubpath& b) {
    return a.closed || b.closed ? kMaxOrders : 2;
}
// Writes order `c` of a's and b's samples.
void order(const MorphSubpath& a, const MorphSubpath& b, int c, MorphPoint* a_out,
           MorphPoint* b_out) {
    const bool back = c % 2 == 1;
    if (b.closed) {
        std::copy(a.pts.begin(), a.pts.end(), a_out);
        loop_order(b.pts.data(), c / 2, back, b_out);
    } else if (a.closed) {
        // An open target cuts the loop: every start point of the loop is a place to cut.
        loop_order(a.pts.data(), c / 2, false, a_out);
        back ? reversed(b.pts.data(), b_out)
             : static_cast<void>(std::copy(b.pts.begin(), b.pts.end(), b_out));
    } else {
        std::copy(a.pts.begin(), a.pts.end(), a_out);
        back ? reversed(b.pts.data(), b_out)
             : static_cast<void>(std::copy(b.pts.begin(), b.pts.end(), b_out));
    }
}
int fit_count(const MorphSubpath& a, const MorphSubpath& b) {
    return a.closed && b.closed ? kMorphSamples - 1 : kMorphSamples;
}
struct OrderFit {
    float residual = 0.0f;
    float angle = 0.0f;
};
// Every order of a pair, fitted.
struct PairOrders {
    std::array<OrderFit, kMaxOrders> fits{};
    int count = 0;
};
void fit_orders(const MorphSubpath& a, const MorphSubpath& b, PairOrders& out) {
    std::array<MorphPoint, kMorphSamples> ta{};
    std::array<MorphPoint, kMorphSamples> tb{};
    out.count = order_count(a, b);
    for (int c = 0; c < out.count; ++c) {
        order(a, b, c, ta.data(), tb.data());
        const Cloud cloud{ta.data(), tb.data(), fit_count(a, b)};
        const Procrustes f = fit(&cloud, 1);
        out.fits[static_cast<std::size_t>(c)] = {f.residual, f.angle};
    }
}
float wrapped(float angle) {
    const auto pi = static_cast<float>(kPi);
    while (angle > pi) {
        angle -= 2.0f * pi;
    }
    while (angle <= -pi) {
        angle += 2.0f * pi;
    }
    return angle;
}
// Deformation first; of orders that fit about as well, the one that turns least away from
// `reference` (0: the shortest turn). A line fits both ways round, and only this picks one.
int best_order(const PairOrders& orders, float reference) {
    int best = 0;
    float best_score = std::numeric_limits<float>::max();
    for (int c = 0; c < orders.count; ++c) {
        const OrderFit& f = orders.fits[static_cast<std::size_t>(c)];
        const float s =
            f.residual + 0.05f * std::fabs(wrapped(f.angle - reference)) / static_cast<float>(kPi);
        if (s < best_score) {
            best_score = s;
            best = c;
        }
    }
    return best;
}
float pair_cost(const MorphSubpath& a, const MorphSubpath& b) {
    const float dx = a.centroid.x - b.centroid.x;
    const float dy = a.centroid.y - b.centroid.y;
    return std::sqrt(dx * dx + dy * dy) + 0.35f * std::fabs(a.length - b.length);
}
// The cheapest injection of the `small` side into the `large` side: pick[i] is the large index
// for small index i. Exhaustive with pruning (at most kMaxSubpaths on each side).
struct Matcher {
    float cost[kMaxSubpaths][kMaxSubpaths]; // [small][large]
    int small = 0;
    int large = 0;
    int pick[kMaxSubpaths]{};
    int best_pick[kMaxSubpaths]{};
    float best = std::numeric_limits<float>::max();
    bool used[kMaxSubpaths]{};

    void search(int i, float so_far) {
        if (so_far >= best) {
            return;
        }
        if (i >= small || i >= kMaxSubpaths) {
            best = so_far;
            for (int k = 0; k < small && k < kMaxSubpaths; ++k) {
                best_pick[k] = pick[k];
            }
            return;
        }
        for (int j = 0; j < large && j < kMaxSubpaths; ++j) {
            if (!used[j]) {
                used[j] = true;
                pick[i] = j;
                search(i + 1, so_far + cost[i][j]);
                used[j] = false;
            }
        }
    }
};

} // namespace

void plan_morph(const MorphShape& a, const MorphShape& b, MorphPlan& out) {
    out.count = 0;
    out.rigid = false;
    if (a.count == 0 || b.count == 0) {
        return;
    }
    // Pairing: the smaller side is matched one to one into the larger, then every leftover of the
    // larger side joins its nearest partner on the smaller side, which is shared (duplicated).
    const bool a_small = a.count <= b.count;
    const MorphShape& small = a_small ? a : b;
    const MorphShape& large = a_small ? b : a;
    Matcher m;
    m.small = small.count;
    m.large = large.count;
    for (int i = 0; i < m.small; ++i) {
        for (int j = 0; j < m.large; ++j) {
            m.cost[i][j] = pair_cost(small.subpaths[static_cast<std::size_t>(i)],
                                     large.subpaths[static_cast<std::size_t>(j)]);
        }
    }
    m.search(0, 0.0f);
    int partner[kMaxSubpaths]; // for each large index, its small index
    std::fill(partner, partner + kMaxSubpaths, -1);
    for (int i = 0; i < m.small; ++i) {
        partner[m.best_pick[i]] = i;
    }
    for (int j = 0; j < m.large; ++j) {
        if (partner[j] < 0) {
            int best = 0;
            for (int i = 1; i < m.small; ++i) {
                if (m.cost[i][j] < m.cost[best][j]) {
                    best = i;
                }
            }
            partner[j] = best;
        }
    }
    out.count = m.large;
    std::array<PairOrders, kMaxSubpaths> orders;
    std::array<int, kMaxSubpaths> chosen{};
    std::array<Cloud, kMaxSubpaths> clouds{};
    for (int j = 0; j < out.count; ++j) {
        MorphPair& p = out.pairs[static_cast<std::size_t>(j)];
        p.from = a_small ? partner[j] : j;
        p.to = a_small ? j : partner[j];
        const MorphSubpath& sa = a.subpaths[static_cast<std::size_t>(p.from)];
        const MorphSubpath& sb = b.subpaths[static_cast<std::size_t>(p.to)];
        p.closed = sa.closed && sb.closed;
        fit_orders(sa, sb, orders[static_cast<std::size_t>(j)]);
        chosen[static_cast<std::size_t>(j)] = best_order(orders[static_cast<std::size_t>(j)], 0.0f);
        clouds[static_cast<std::size_t>(j)] = {p.a.data(), p.b.data(), fit_count(sa, sb)};
    }
    // Ordered samples go to the pair's buffers; they are centred and aligned below.
    const auto apply = [&](const std::array<int, kMaxSubpaths>& pick) {
        for (int j = 0; j < out.count; ++j) {
            MorphPair& p = out.pairs[static_cast<std::size_t>(j)];
            order(a.subpaths[static_cast<std::size_t>(p.from)],
                  b.subpaths[static_cast<std::size_t>(p.to)], pick[static_cast<std::size_t>(j)],
                  p.a.data(), p.b.data());
        }
        return fit(clouds.data(), out.count);
    };
    // The global pass: one similarity for all the pairs. A congruent icon moves as one piece.
    Procrustes global = apply(chosen);
    out.rigid = global.residual < kRigidResidual;
    if (!out.rigid && out.count > 1) {
        // A pair that fits both ways round (a line) may have taken the other turn from the rest.
        // Try each pair's turn for all of them; the icon is congruent if one makes it so.
        for (int h = 0; h < out.count && !out.rigid; ++h) {
            const float turn =
                orders[static_cast<std::size_t>(h)]
                    .fits[static_cast<std::size_t>(chosen[static_cast<std::size_t>(h)])]
                    .angle;
            std::array<int, kMaxSubpaths> pick{};
            for (int j = 0; j < out.count; ++j) {
                pick[static_cast<std::size_t>(j)] =
                    best_order(orders[static_cast<std::size_t>(j)], turn);
            }
            const Procrustes g = apply(pick);
            if (g.residual < kRigidResidual) {
                global = g;
                out.rigid = true;
            }
        }
        if (!out.rigid) {
            global = apply(chosen);
        }
    }
    out.angle = global.angle;
    out.log_scale = std::log(global.scale);
    out.ga = global.ca;
    const float gc = std::cos(global.angle);
    const float gs = std::sin(global.angle);
    for (int j = 0; j < out.count; ++j) {
        MorphPair& p = out.pairs[static_cast<std::size_t>(j)];
        const Procrustes own = fit(&clouds[static_cast<std::size_t>(j)], 1);
        const Procrustes& use = out.rigid ? global : own;
        p.angle = use.angle;
        p.log_scale = std::log(use.scale);
        p.ca = own.ca;
        p.cb = own.cb;
        const float c = std::cos(-use.angle);
        const float s = std::sin(-use.angle);
        const float inv = 1.0f / use.scale;
        for (int i = 0; i < kMorphSamples; ++i) {
            MorphPoint& pa = p.a[static_cast<std::size_t>(i)];
            MorphPoint& pb = p.b[static_cast<std::size_t>(i)];
            pa = {pa.x - p.ca.x, pa.y - p.ca.y};
            const MorphPoint r = rotate({pb.x - p.cb.x, pb.y - p.cb.y}, c, s);
            pb = {r.x * inv, r.y * inv};
        }
        // Block transport: the centroid rides the shared similarity around the icon's centroid;
        // the drift absorbs what is left so it lands exactly on cb.
        p.drift = {0.0f, 0.0f};
        if (out.rigid) {
            const MorphPoint off{p.ca.x - out.ga.x, p.ca.y - out.ga.y};
            const MorphPoint turned = rotate(off, gc, gs);
            const float sc = global.scale;
            p.drift = {p.cb.x - p.ca.x - (sc * turned.x - off.x),
                       p.cb.y - p.ca.y - (sc * turned.y - off.y)};
        }
    }
}

} // namespace etude
