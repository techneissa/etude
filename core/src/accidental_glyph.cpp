// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/accidental_glyph.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

GlyphQuad rect_quad(float x, float y, float w, float h) {
    return {{x, x + w, x + w, x}, {y, y, y + h, y + h}};
}

void add(AccidentalShape& s, const GlyphQuad& q) {
    if (s.count < s.quads.size()) {
        s.quads[s.count++] = q;
    }
}

float whole(float v) { return std::round(v); }

float clamp_height(float height_px) {
    return std::isfinite(height_px) ? std::max(whole(height_px), kMinAccidentalGlyphPx)
                                    : kMinAccidentalGlyphPx;
}

void build_sharp(AccidentalShape& s, float h) {
    const float w = s.width;
    const float t = std::max(1.0f, whole(h / 9.0f));                        // stroke thickness
    const float tb = std::max(1.0f, whole(h / 6.0f));                       // bar thickness
    const float slant = h >= 8.0f ? std::max(1.0f, whole(h / 9.0f)) : 0.0f; // left end lower
    // Bars' top edge on the right; they stay inside the glyph and apart.
    float y2 = std::min(whole(0.70f * h - 0.5f * tb), h - tb - slant);
    float y1 = std::max(0.0f, std::min(whole(0.36f * h - 0.5f * tb), y2 - tb - 1.0f));
    y2 = std::max(y2, y1 + tb + 1.0f);
    const float margin = std::max(1.0f, whole(w * 0.17f));
    const float xs[2] = {margin, w - margin - t};

    const auto bar_top = [&](float y, float x) { return y + slant * (1.0f - x / w); };
    for (float y : {y1, y2}) {
        add(s, {{0.0f, w, w, 0.0f}, {y + slant, y, y + tb, y + tb + slant}});
    }
    // The strokes, cut along the bars' edges where they cross: above the first bar, between the
    // two, below the second. The cuts follow the bars' slant exactly, so the pieces meet the bars
    // with no gap and no overlap (a corner on a junction may sit between pixels; the outline of
    // the glyph is on whole pixels).
    for (float x : xs) {
        const float x2 = x + t;
        const float top1[2] = {bar_top(y1, x), bar_top(y1, x2)};
        const float top2[2] = {bar_top(y2, x), bar_top(y2, x2)};
        const auto piece = [&](float ya_l, float ya_r, float yb_l, float yb_r) {
            if ((yb_l - ya_l) + (yb_r - ya_r) >= 2.0f * 0.5f) { // at least half a pixel tall
                add(s, {{x, x2, x2, x}, {ya_l, ya_r, yb_r, yb_l}});
            }
        };
        piece(0.0f, 0.0f, top1[0], top1[1]);
        piece(top1[0] + tb, top1[1] + tb, top2[0], top2[1]);
        piece(top2[0] + tb, top2[1] + tb, h, h);
    }
}

void build_flat(AccidentalShape& s, float h) {
    const float w = s.width;
    const float t = std::max(1.0f, whole(h / 9.0f));
    const float yt = whole(0.40f * h);   // top of the bowl
    const float yr = whole(0.72f * h);   // where its right side turns back to the stem
    add(s, rect_quad(0.0f, 0.0f, t, h)); // the stem
    add(s, rect_quad(t, yt, std::max(1.0f, w - 2.0f * t), t));
    add(s, rect_quad(w - t, yt, t, std::max(1.0f, yr - yt)));
    // The bowl's lower side runs from the right side back to the foot of the stem.
    add(s, {{w - t, w, 2.0f * t, t}, {yr, yr, h, h}});
}

} // namespace

AccidentalKind accidental_kind(std::string_view accidental) {
    if (accidental == "#") {
        return AccidentalKind::Sharp;
    }
    if (accidental == "b") {
        return AccidentalKind::Flat;
    }
    return AccidentalKind::None;
}

float accidental_glyph_height_px(float cap_px) {
    if (!std::isfinite(cap_px) || cap_px <= 0.0f) {
        return kMinAccidentalGlyphPx;
    }
    return std::max(whole(kAccidentalCapShare * cap_px), kMinAccidentalGlyphPx);
}

float accidental_glyph_width_px(AccidentalKind kind, float height_px) {
    const float h = clamp_height(height_px);
    switch (kind) {
    case AccidentalKind::Sharp:
        return std::max(whole(kSharpWidthShare * h), kMinSharpWidthPx);
    case AccidentalKind::Flat:
        return std::max(whole(kFlatWidthShare * h), kMinFlatWidthPx);
    case AccidentalKind::None:
        break;
    }
    return 0.0f;
}

AccidentalShape accidental_shape(AccidentalKind kind, float height_px) {
    AccidentalShape s;
    if (kind == AccidentalKind::None) {
        return s;
    }
    const float h = clamp_height(height_px);
    s.height = h;
    s.width = accidental_glyph_width_px(kind, h);
    if (kind == AccidentalKind::Sharp) {
        build_sharp(s, h);
    } else {
        build_flat(s, h);
    }
    return s;
}

} // namespace etude
