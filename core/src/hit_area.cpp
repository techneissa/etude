// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hit_area.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

RectF grown(const RectF& face, float least) {
    const float w = std::max(face.w, least);
    const float h = std::max(face.h, least);
    return {face.x + face.w * 0.5f - w * 0.5f, face.y + face.h * 0.5f - h * 0.5f, w, h};
}

// The signed gap between two spans (negative: how far they overlap).
float span_gap(float a0, float a1, float b0, float b1) { return std::max(b0 - a1, a0 - b1); }

float x_gap(const RectF& a, const RectF& b) { return span_gap(a.x, a.right(), b.x, b.right()); }
float y_gap(const RectF& a, const RectF& b) { return span_gap(a.y, a.bottom(), b.y, b.bottom()); }

void cut_x(RectF& left, RectF& right, float mid, float gap) {
    const float edge = mid - gap * 0.5f;
    const float start = mid + gap * 0.5f;
    left.w = std::max(0.0f, std::min(left.right(), edge) - left.x);
    const float end = right.right();
    right.x = std::max(right.x, start);
    right.w = std::max(0.0f, end - right.x);
}

void cut_y(RectF& top, RectF& bottom, float mid, float gap) {
    const float edge = mid - gap * 0.5f;
    const float start = mid + gap * 0.5f;
    top.h = std::max(0.0f, std::min(top.bottom(), edge) - top.y);
    const float end = bottom.bottom();
    bottom.y = std::max(bottom.y, start);
    bottom.h = std::max(0.0f, end - bottom.y);
}

} // namespace

float hit_area_gap(const RectF& a, const RectF& b) {
    const float gx = x_gap(a, b);
    const float gy = y_gap(a, b);
    if (gx > 0.0f && gy > 0.0f) {
        return std::hypot(gx, gy); // apart on both axes: corner to corner
    }
    // Apart on one axis: that axis's gap. Overlapping on both: the smaller overlap, negative.
    return std::max(gx, gy);
}

std::vector<HitArea> clip_hit_areas(std::span<const RectF> faces, float least, float gap) {
    std::vector<HitArea> out(faces.size());
    for (std::size_t i = 0; i < faces.size(); ++i) {
        out[i].rect = grown(faces[i], least);
    }
    for (std::size_t i = 0; i < faces.size(); ++i) {
        for (std::size_t j = i + 1; j < faces.size(); ++j) {
            if (hit_area_gap(out[i].rect, out[j].rect) >= gap) {
                continue;
            }
            const RectF& a = faces[i];
            const RectF& b = faces[j];
            const float fx = x_gap(a, b);
            const float fy = y_gap(a, b);
            if (fx >= fy) {
                const bool a_left = a.x + a.w * 0.5f <= b.x + b.w * 0.5f;
                const float mid = a_left ? (a.right() + b.x) * 0.5f : (b.right() + a.x) * 0.5f;
                a_left ? cut_x(out[i].rect, out[j].rect, mid, gap)
                       : cut_x(out[j].rect, out[i].rect, mid, gap);
            } else {
                const bool a_top = a.y + a.h * 0.5f <= b.y + b.h * 0.5f;
                const float mid = a_top ? (a.bottom() + b.y) * 0.5f : (b.bottom() + a.y) * 0.5f;
                a_top ? cut_y(out[i].rect, out[j].rect, mid, gap)
                      : cut_y(out[j].rect, out[i].rect, mid, gap);
            }
        }
    }
    constexpr float kSlack = 1e-3f;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const RectF& r = out[i].rect;
        const RectF& f = faces[i];
        out[i].full = r.w >= least - kSlack && r.h >= least - kSlack && r.x <= f.x + kSlack &&
                      r.y <= f.y + kSlack && r.right() >= f.right() - kSlack &&
                      r.bottom() >= f.bottom() - kSlack;
    }
    return out;
}

} // namespace etude
