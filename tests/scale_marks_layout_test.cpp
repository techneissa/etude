// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene_scale_layout.hpp"

#include "etude/keyboard.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;

namespace {

struct Size {
    float w;
    float h;
};
constexpr Size kSizes[] = {{800.0f, 480.0f}, {1366.0f, 768.0f}, {1920.0f, 1080.0f}};
constexpr float kEps = 1e-3f;

// A 61-key keyboard (36 white keys) across the window, as the scene lays it out.
ScaleMarkKey key_at(Size s, bool black, bool root, float want_scale) {
    const ScreenLayout lay = compute_layout(s.w, s.h, kDefaultPxPerMm, LookSettings{});
    const float unit = s.h / 1080.0f;
    const float white = s.w / 36.0f;
    ScaleMarkKey k;
    k.black = black;
    k.width = black ? 0.62f * white : white;
    k.x = 10.0f * white + (black ? 0.7f * white : 0.0f);
    k.top = lay.key_area.y;
    k.bottom = lay.key_area.bottom() - 1.5f * unit;
    k.black_length = lay.key_area.h * kBlackKeyLengthShare;
    k.unit = unit;
    // The names a key may carry: the layout's key-label size (the scene fits it to the keys).
    k.label_px = std::max(kMinLabelFontPx, lay.key_label_font_px * (black ? 0.85f : 1.0f));
    k.root = root;
    k.root_px = std::min(0.62f * white, 26.0f * unit);
    k.finger_px = want_scale * std::max(std::min(0.62f * white, 24.0f * unit), k.label_px);
    return k;
}

MarkBox label_box(const ScaleMarkKey& k) {
    const float cx = k.x + 0.5f * k.width;
    const float bottom = k.black ? k.top + scale_marks_tok::kBlackLabelLine * k.black_length
                                 : k.bottom - scale_marks_tok::kFrontMargin * k.unit;
    return {cx - 0.5f * k.width, bottom - k.label_px, cx + 0.5f * k.width, bottom};
}

} // namespace

TEST_CASE("scale marks: a white key's number is in its open part, clear of its name and edges") {
    for (const Size s : kSizes) {
        for (const bool root : {false, true}) {
            CAPTURE(s.w);
            CAPTURE(root);
            const ScaleMarkKey k = key_at(s, false, root, 1.0f);
            const ScaleMarkBoxes m = scale_marks_layout(k);
            REQUIRE(m.finger_shown);
            CHECK(m.finger_px >= k.label_px - kEps); // at least the note-label size
            const float gap = scale_marks_tok::kGapUnits * k.unit;
            // Inside the key's rect, under the black keys' feet, a gap from every edge.
            CHECK(m.finger.x0 >= k.x + gap - kEps);
            CHECK(m.finger.x1 <= k.x + k.width - gap + kEps);
            CHECK(m.finger.y0 >= k.top + k.black_length + gap - kEps);
            CHECK(m.finger.y1 <= k.bottom - gap + kEps);
            // Clear of the key's names (the label line and the root's name).
            CHECK_FALSE(m.finger.overlaps(label_box(k)));
            if (root) {
                REQUIRE(m.name_shown);
                CHECK_FALSE(m.finger.overlaps(m.name));
            }
        }
    }
}

TEST_CASE("scale marks: a black key's number is above its root name with a gap") {
    for (const Size s : kSizes) {
        for (const bool root : {false, true}) {
            CAPTURE(s.w);
            CAPTURE(root);
            const ScaleMarkKey k = key_at(s, true, root, 1.0f);
            const ScaleMarkBoxes m = scale_marks_layout(k);
            if (!m.finger_shown) {
                continue; // dropped, never squeezed: checked below that white keys keep theirs
            }
            const float gap = scale_marks_tok::kGapUnits * k.unit;
            CHECK(m.finger.x0 >= k.x + gap - kEps);
            CHECK(m.finger.x1 <= k.x + k.width - gap + kEps);
            CHECK(m.finger.y0 >= k.top + gap - kEps);
            CHECK(m.finger.y1 <= k.top + k.black_length - gap + kEps);
            CHECK_FALSE(m.finger.overlaps(label_box(k)));
            if (root) {
                REQUIRE(m.name_shown);
                CHECK_FALSE(m.finger.overlaps(m.name));
                CHECK(m.name.y0 - m.finger.y1 >= gap - kEps);
            }
        }
    }
}

TEST_CASE("scale marks: no number where its open part cannot hold it") {
    ScaleMarkKey k = key_at(kSizes[0], false, false, 1.0f);
    k.bottom = k.top + k.black_length + 6.0f; // a foot of a few pixels
    CHECK_FALSE(scale_marks_layout(k).finger_shown);
    k = key_at(kSizes[1], false, false, 0.0f); // no finger asked for
    CHECK_FALSE(scale_marks_layout(k).finger_shown);
}

TEST_CASE("scale marks: a C root keeps its marker, and its number stays clear of it") {
    for (const Size s : kSizes) {
        CAPTURE(s.w);
        ScaleMarkKey k = key_at(s, false, true, 1.0f);
        k.has_marker = true; // middle C or any C with the C markers on
        const ScaleMarkBoxes m = scale_marks_layout(k);
        CHECK_FALSE(m.name_shown); // the marker is the key's name: the overlay adds no bare "C"
        REQUIRE(m.finger_shown);
        CHECK_FALSE(m.finger.overlaps(label_box(k)));
        // Without a marker (C markers off) the root is named by the overlay.
        k.has_marker = false;
        CHECK(scale_marks_layout(k).name_shown);
    }
}
