// SPDX-License-Identifier: GPL-3.0-or-later
#include "ink_measure.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace etude::app::ui {

namespace {

float difference(const PixelView& a, const PixelView& b, int x, int y) {
    const std::uint8_t* pa = a.data + static_cast<std::ptrdiff_t>(y) * a.stride + x * 4;
    const std::uint8_t* pb = b.data + static_cast<std::ptrdiff_t>(y) * b.stride + x * 4;
    int most = 0;
    for (int c = 0; c < 3; ++c) {
        most = std::max(most, std::abs(static_cast<int>(pa[c]) - static_cast<int>(pb[c])));
    }
    return static_cast<float>(most) / 255.0f;
}

bool usable(const PixelView& v) { return v.data != nullptr && v.width > 0 && v.height > 0; }

// The box of the coverage grid (0..1 per pixel, `aw` x `ah`, its top left at `ox`, `oy`): each edge
// at the first or last column or row that has any, less the share of it that is empty.
InkBox box_of_coverage(const std::vector<float>& cover, int aw, int ah, int ox, int oy,
                       float noise) {
    InkBox out;
    std::vector<float> col(static_cast<std::size_t>(aw), 0.0f);
    std::vector<float> row(static_cast<std::size_t>(ah), 0.0f);
    for (int y = 0; y < ah; ++y) {
        for (int x = 0; x < aw; ++x) {
            const float c = cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(aw) +
                                  static_cast<std::size_t>(x)];
            if (c <= noise) {
                continue;
            }
            col[static_cast<std::size_t>(x)] = std::max(col[static_cast<std::size_t>(x)], c);
            row[static_cast<std::size_t>(y)] = std::max(row[static_cast<std::size_t>(y)], c);
        }
    }
    const auto first = [](const std::vector<float>& v) {
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] > 0.0f) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const auto last = [](const std::vector<float>& v) {
        for (std::size_t i = v.size(); i > 0; --i) {
            if (v[i - 1] > 0.0f) {
                return static_cast<int>(i - 1);
            }
        }
        return -1;
    };
    const int x_first = first(col);
    const int x_last = last(col);
    const int y_first = first(row);
    const int y_last = last(row);
    if (x_first < 0 || y_first < 0) {
        return out;
    }
    out.found = true;
    out.left = static_cast<float>(ox + x_first) + (1.0f - col[static_cast<std::size_t>(x_first)]);
    out.right = static_cast<float>(ox + x_last) + col[static_cast<std::size_t>(x_last)];
    out.top = static_cast<float>(oy + y_first) + (1.0f - row[static_cast<std::size_t>(y_first)]);
    out.bottom = static_cast<float>(oy + y_last) + row[static_cast<std::size_t>(y_last)];
    return out;
}

bool clip_area(PixelRect& area, int w, int h) {
    area.x0 = std::max(area.x0, 0);
    area.y0 = std::max(area.y0, 0);
    area.x1 = std::min(area.x1, w);
    area.y1 = std::min(area.y1, h);
    return area.x1 > area.x0 && area.y1 > area.y0;
}

} // namespace

InkBox measure_ink(const PixelView& with, const PixelView& without, PixelRect area, float noise) {
    if (!usable(with) || !usable(without) ||
        !clip_area(area, std::min(with.width, without.width), std::min(with.height, without.height))) {
        return {};
    }
    const int aw = area.x1 - area.x0;
    const int ah = area.y1 - area.y0;
    std::vector<float> diff(static_cast<std::size_t>(aw) * static_cast<std::size_t>(ah));
    float peak = 0.0f;
    for (int y = 0; y < ah; ++y) {
        for (int x = 0; x < aw; ++x) {
            const float d = difference(with, without, area.x0 + x, area.y0 + y);
            diff[static_cast<std::size_t>(y) * static_cast<std::size_t>(aw) +
                 static_cast<std::size_t>(x)] = d;
            peak = std::max(peak, d);
        }
    }
    if (!(peak > noise)) {
        return {};
    }
    // The strongest difference counts as full coverage.
    for (float& d : diff) {
        d = d > noise ? std::min(1.0f, d / peak) : 0.0f;
    }
    return box_of_coverage(diff, aw, ah, area.x0, area.y0, 0.0f);
}

InkBox measure_alpha(const std::uint8_t* alpha, int stride, int bytes_per_pixel, int width,
                     int height, PixelRect area, float noise) {
    if (alpha == nullptr || bytes_per_pixel < 1 ||
        !clip_area(area, width, height)) {
        return {};
    }
    const int aw = area.x1 - area.x0;
    const int ah = area.y1 - area.y0;
    std::vector<float> cover(static_cast<std::size_t>(aw) * static_cast<std::size_t>(ah));
    for (int y = 0; y < ah; ++y) {
        const std::uint8_t* row = alpha + static_cast<std::ptrdiff_t>(area.y0 + y) * stride +
                                  static_cast<std::ptrdiff_t>(area.x0) * bytes_per_pixel;
        for (int x = 0; x < aw; ++x) {
            cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(aw) +
                  static_cast<std::size_t>(x)] = static_cast<float>(row[x * bytes_per_pixel]) / 255.0f;
        }
    }
    InkBox box = box_of_coverage(cover, aw, ah, area.x0, area.y0, noise);
    return box;
}

Offset judge_ink(const InkBox& ink, float button_cx, float button_cy, InkKind kind, bool check_y) {
    Offset o;
    if (!ink.found) {
        return o;
    }
    const float want_dx = kind == InkKind::PlayTriangle ? kPlayOpticalShare * ink.width() : 0.0f;
    o.dx = ink.cx() - button_cx;
    o.dy = ink.cy() - button_cy;
    // Judged against the wanted offset; reported as it is, so a table shows the real position.
    constexpr float kLimit = kCentreTolerancePx + kMeasureSlackPx;
    o.ok = std::fabs(o.dx - want_dx) <= kLimit && (!check_y || std::fabs(o.dy) <= kLimit);
    return o;
}

std::uint64_t label_metrics_key(const void* font, float size, std::string_view shown) {
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a
    const auto mix = [&h](const void* data, std::size_t n) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ bytes[i]) * 1099511628211ULL;
        }
    };
    mix(&font, sizeof font);
    mix(&size, sizeof size);
    mix(shown.data(), shown.size());
    return h == 0 ? 1 : h;
}

LabelPen place_label(const LabelMetrics& m, float x0, float x1, float cy, LabelAlign align) {
    LabelPen p;
    p.y = cy - (m.cap_top + m.baseline) * 0.5f;
    switch (align) {
    case LabelAlign::Left:
        p.x = x0;
        break;
    case LabelAlign::Centre:
        p.x = (x0 + x1) * 0.5f - (m.ink_left + m.ink_right) * 0.5f;
        break;
    case LabelAlign::Right:
        p.x = x1 - m.ink_right;
        break;
    }
    return p;
}

LabelInk measure_label(const PixelView& with, const PixelView& without, PixelRect area,
                       bool descenders, float noise) {
    LabelInk out;
    if (!usable(with) || !usable(without) ||
        !clip_area(area, std::min(with.width, without.width),
                   std::min(with.height, without.height))) {
        return out;
    }
    const int aw = area.x1 - area.x0;
    const int ah = area.y1 - area.y0;
    std::vector<float> cover(static_cast<std::size_t>(aw) * static_cast<std::size_t>(ah));
    float peak = 0.0f;
    for (int y = 0; y < ah; ++y) {
        for (int x = 0; x < aw; ++x) {
            const float d = difference(with, without, area.x0 + x, area.y0 + y);
            cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(aw) +
                  static_cast<std::size_t>(x)] = d;
            peak = std::max(peak, d);
        }
    }
    if (!(peak > noise)) {
        return out;
    }
    for (float& c : cover) {
        c = c > noise ? std::min(1.0f, c / peak) : 0.0f;
    }
    out.box = box_of_coverage(cover, aw, ah, area.x0, area.y0, 0.0f);
    if (!out.box.found) {
        return out;
    }
    if (!descenders) {
        out.baseline = out.box.bottom;
        out.found = true;
        return out;
    }
    // The ink in each row; the baseline is the foot of the lowest row with a real share of it.
    std::vector<float> rows(static_cast<std::size_t>(ah), 0.0f);
    float strongest = 0.0f;
    for (int y = 0; y < ah; ++y) {
        float sum = 0.0f;
        for (int x = 0; x < aw; ++x) {
            sum += cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(aw) +
                         static_cast<std::size_t>(x)];
        }
        rows[static_cast<std::size_t>(y)] = sum;
        strongest = std::max(strongest, sum);
    }
    for (int y = ah - 1; y >= 0; --y) {
        if (rows[static_cast<std::size_t>(y)] >= kBaselineShare * strongest) {
            out.baseline = static_cast<float>(area.y0 + y + 1);
            out.found = true;
            break;
        }
    }
    return out;
}

LabelOffset judge_label(const LabelInk& ink, float cap_h, float slot_cx, float slot_cy,
                        bool check_x, bool check_y) {
    LabelOffset o;
    if (!ink.found) {
        return o;
    }
    o.dx = ink.box.cx() - slot_cx;
    o.dy = (ink.baseline - cap_h * 0.5f) - slot_cy;
    constexpr float kLimit = kLabelTolerancePx + kMeasureSlackPx;
    o.ok = (!check_y || std::fabs(o.dy) <= kLimit) && (!check_x || std::fabs(o.dx) <= kLimit);
    return o;
}

bool label_inside(float ink_left, float ink_right, float face_left, float face_right) {
    constexpr float kLimit = kLabelTolerancePx + kMeasureSlackPx;
    return ink_left >= face_left - kLimit && ink_right <= face_right + kLimit;
}

int label_face(std::span<const FaceBox> faces, const void* list, float ink_left, float ink_right,
               float ink_cy) {
    // An end counts only when it lies in from the face's edge by the tolerance: a label that stops
    // where the next control's face starts (a switch beside it) is not on that face.
    constexpr float kIn = kLabelTolerancePx + kMeasureSlackPx;
    int best = -1;
    float best_area = 0.0f;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const FaceBox& f = faces[i];
        bool holds = f.list == list && f.top <= ink_cy && ink_cy <= f.bottom;
        const float mid = (ink_left + ink_right) * 0.5f;
        const bool across = (f.left <= mid && mid <= f.right) ||
                            (f.left + kIn <= ink_left && ink_left <= f.right - kIn) ||
                            (f.left + kIn <= ink_right && ink_right <= f.right - kIn);
        holds = holds && across;
        const float area = (f.right - f.left) * (f.bottom - f.top);
        if (holds && (best < 0 || area < best_area)) {
            best = static_cast<int>(i);
            best_area = area;
        }
    }
    return best;
}

} // namespace etude::app::ui
