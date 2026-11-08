// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the screen_layout_*test.cpp files (split from screen_layout_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/screen_layout.hpp"
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "sample_looks.hpp"

namespace screen_layout_test_support {

using namespace etude;
using namespace etude::test;

constexpr float kPpmm96 = 96.0f / 25.4f;

struct Case {
    float w;
    float h;
    float ppmm;
};

inline std::vector<const RectF*> rects(const ScreenLayout& l) {
    return {&l.top_row, &l.bar_under, &l.lane, &l.chord_label, &l.note_area, &l.keyboard};
}

// Positive-area intersection, written out here on purpose and not shared with the code under test.
inline bool overlap(const RectF& a, const RectF& b) {
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) {
        return false;
    }
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Everything every layout must satisfy, at any size.
inline void check_invariants(const ScreenLayout& l, float width, float height, const char* what) {
    INFO(what);
    const float W = std::floor(std::isfinite(width) && width > 0 ? width : 0.0f);
    const float H = std::floor(std::isfinite(height) && height > 0 ? height : 0.0f);
    for (const RectF* r : {&l.key_bed, &l.key_area}) {
        CHECK(std::isfinite(r->y));
        CHECK(r->x >= 0.0f);
        CHECK(r->w >= 0.0f);
        CHECK(r->h >= 0.0f);
        if (l.keyboard.h > 0.0f) {
            CHECK(r->y >= l.keyboard.y);
            CHECK(r->bottom() <= l.keyboard.bottom());
        }
    }
    for (const RectF* r : rects(l)) {
        CHECK(std::isfinite(r->x));
        CHECK(std::isfinite(r->y));
        CHECK(std::isfinite(r->w));
        CHECK(std::isfinite(r->h));
        CHECK(r->x >= 0.0f);
        CHECK(r->y >= 0.0f);
        CHECK(r->w >= 0.0f);
        CHECK(r->h >= 0.0f);
        CHECK(r->x + r->w <= W);
        CHECK(r->y + r->h <= H);
    }
    if (W < 1.0f || H < 1.0f) {
        // No room at all: everything is empty and hidden.
        for (const RectF* r : rects(l)) {
            CHECK(r->empty());
        }
        CHECK_FALSE(l.chord_label_visible);
        CHECK_FALSE(l.status_visible);
        CHECK(l.gear.empty());
        CHECK(l.status_text.empty());
        CHECK_FALSE(l.block_labels_visible);
        CHECK_FALSE(l.key_labels_visible);
        CHECK_FALSE(l.c_markers_visible);
        return;
    }
    const auto all = rects(l);
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            CHECK_FALSE(overlap(*all[i], *all[j]));
        }
    }
    // The key bed and the key area split the keyboard exactly: bed on top, keys start where it
    // ends.
    if (l.keyboard.h > 0.0f) {
        CHECK(l.key_bed.y == l.keyboard.y);
        CHECK(l.key_bed.bottom() == l.key_area.y);
        CHECK(l.key_area.bottom() == l.keyboard.bottom());
        // At least the minimum, unless the keyboard is so small that 20 percent of it is less.
        CHECK(l.key_bed.h >= std::min(kMinKeyBedPx, std::floor(l.keyboard.h * 0.2f)));
        CHECK(l.key_bed.h <= 0.2f * l.keyboard.h);
        CHECK(l.key_area.h > 0.0f);
        CHECK(l.key_bed.w == (l.key_bed.h > 0.0f ? l.keyboard.w : 0.0f));
        CHECK(l.key_area.w == l.keyboard.w);
        CHECK(l.felt_line_h >= (l.key_bed.h > 0.0f ? 1.0f : 0.0f));
        CHECK(l.felt_line_h <= l.key_bed.h);
        CHECK(l.key_shadow_h >= std::min(2.0f, l.key_area.h));
        CHECK(l.key_shadow_h <= l.key_area.h);
    } else {
        CHECK(l.key_bed.empty());
        CHECK(l.key_area.empty());
        CHECK(l.felt_line_h == 0.0f);
        CHECK(l.key_shadow_h == 0.0f);
    }
    // The keyboard sits flush with the bottom edge and nothing is drawn below it. The top row is
    // at the top, above the chord label.
    CHECK(l.keyboard.bottom() == H);
    for (const RectF* r : {&l.top_row, &l.bar_under, &l.lane, &l.chord_label, &l.note_area}) {
        CHECK(r->bottom() <= l.keyboard.y);
    }
    CHECK(l.top_row.y == 0.0f);
    CHECK(l.bar_under.y == l.top_row.bottom());
    CHECK(l.lane.y == l.bar_under.bottom());
    CHECK(l.chord_label.y == l.lane.bottom());
    CHECK(l.note_area.y == l.chord_label.bottom());
    // Together they fill the window height exactly.
    CHECK(l.top_row.h + l.bar_under.h + l.lane.h + l.chord_label.h + l.note_area.h + l.keyboard.h ==
          H);
    // The gear and the status text sit inside the top row, side by side, never touching.
    if (!l.gear.empty()) {
        CHECK(l.gear.y == l.top_row.y);
        CHECK(l.gear.h == l.top_row.h);
        CHECK(l.gear.right() == l.top_row.right());
        CHECK(l.gear.w <= l.top_row.h);
    }
    if (!l.status_text.empty()) {
        CHECK(l.status_text.x > 0.0f);
        CHECK(l.status_text.y == l.top_row.y);
        CHECK(l.status_text.bottom() == l.top_row.bottom());
        CHECK(l.status_text.right() < l.gear.x);
    }
    CHECK(l.note_area.h >= kMinNoteAreaShare * H);
    // Text lives inside its rect.
    if (l.chord_label_visible) {
        CHECK(l.chord_label_font_px >= kMinChordFontPx);
        CHECK(l.chord_label_font_px * kLineHeightEm <= l.chord_label.h);
        CHECK(l.chord_label_font_px * kLabelCharWidthEm * static_cast<float>(kChordLabelMaxChars) <=
              l.chord_label.w);
    } else {
        CHECK(l.chord_label_font_px == 0.0f);
        CHECK(l.chord_label.empty());
    }
    if (l.status_visible) {
        CHECK(l.status_font_px >= kMinLabelFontPx);
        CHECK(l.status_font_px * kTopRowMinHeightEm <= l.top_row.h);
        CHECK_FALSE(l.status_text.empty());
    } else {
        CHECK(l.status_font_px == 0.0f);
    }
    if (l.block_labels_visible) {
        CHECK(l.note_label_font_px >= kMinLabelFontPx);
        CHECK(l.note_label_font_px <= l.keyboard.h);
    } else {
        CHECK(l.note_label_font_px == 0.0f);
    }
    if (l.c_markers_visible) {
        CHECK(l.c_marker_font_px >= kMinLabelFontPx);
        CHECK(l.c_marker_font_px <= l.keyboard.h);
    } else {
        CHECK(l.c_marker_font_px == 0.0f);
    }
    if (l.key_labels_visible) {
        CHECK(l.key_label_font_px >= kMinLabelFontPx);
        CHECK(l.key_label_font_px <= l.keyboard.h);
    } else {
        CHECK(l.key_label_font_px == 0.0f);
    }
}

inline std::vector<Case> sweep_cases() {
    std::vector<Case> out;
    const float densities[] = {2.0f, kPpmm96, 5.0f, 6.4f, 10.0f};
    // Named sizes: 4:3, 16:10, 16:9, 21:9, 32:9, portrait, the 800x480 floor, tiny, 4K, 8K.
    const float sizes[][2] = {
        {800, 480},   {1024, 600},  {1024, 768},  {1280, 720},  {1280, 800},  {1600, 1200},
        {1920, 1080}, {2560, 1080}, {3440, 1440}, {3840, 2160}, {5120, 1440}, {7680, 4320},
        {480, 800},   {720, 1280},  {1080, 1920}, {100, 50},    {320, 240},   {64, 64},
        {10, 10},     {1, 1},       {0, 0},       {800, 0},     {0, 480},     {799.5f, 479.5f}};
    for (const auto& s : sizes) {
        for (float d : densities) {
            out.push_back({s[0], s[1], d});
        }
    }
    // Every aspect ratio from 4:3 to 21:9 at several heights, including odd pixel counts.
    for (float h : {480.0f, 481.0f, 600.0f, 721.0f, 1080.0f, 1440.0f, 2160.0f}) {
        for (int step = 0; step <= 16; ++step) {
            const float aspect =
                4.0f / 3.0f + (21.0f / 9.0f - 4.0f / 3.0f) * static_cast<float>(step) / 16.0f;
            for (float d : {kPpmm96, 5.0f}) {
                out.push_back({std::round(h * aspect), h, d});
            }
        }
    }
    return out;
}

constexpr BarStyle kBarStyles[] = {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock};

} // namespace screen_layout_test_support
