// SPDX-License-Identifier: GPL-3.0-or-later
// screen_layout: the first topics (split from screen_layout_test.cpp).
#include "etude/screen_layout.hpp"
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "sample_looks.hpp"

#include "screen_layout_test_support.hpp"

using namespace etude;
using namespace etude::test;
using namespace screen_layout_test_support;

namespace {

std::vector<LookSettings> sweep_looks() {
    std::vector<LookSettings> out;
    for (float kb = 0.10f; kb <= 0.3001f; kb += 0.02f) {
        LookSettings s;
        s.keyboard_height = kb;
        out.push_back(s);
    }
    // Out of range and nonsense heights are clamped, not trusted.
    for (float kb : {-1.0f, 0.0f, 0.5f, 1.0f, 99.0f, std::numeric_limits<float>::quiet_NaN()}) {
        LookSettings s;
        s.keyboard_height = kb;
        out.push_back(s);
    }
    for (float size : {kMinChordLabelSize, 1.0f, kMaxChordLabelSize}) {
        for (bool chord : {false, true}) {
            for (NameMode mode : {NameMode::Off, NameMode::Letters, NameMode::Solfege}) {
                LookSettings s;
                s.chord_label_size = size;
                s.chord_label = chord;
                s.name_mode = mode;
                s.names_on_keys = true;
                out.push_back(s);
            }
        }
    }
    for (Sample id : kAllSamples) {
        out.push_back(sample_look(id));
    }
    return out;
}

} // namespace

TEST_CASE("a 1080p window is stacked top row, chord label, notes, keyboard") {
    const ScreenLayout l = compute_layout(1920, 1080, kPpmm96, LookSettings{});
    check_invariants(l, 1920, 1080, "1080p");
    CHECK(l.top_row.y == 0.0f);
    CHECK(l.bar_under.empty()); // the bar is not pinned
    CHECK(l.chord_label.y == l.top_row.bottom());
    CHECK(l.note_area.y == l.chord_label.bottom());
    CHECK(l.keyboard.y == l.note_area.bottom());
    CHECK(l.keyboard.bottom() == 1080.0f);
    for (const RectF* r : rects(l)) {
        if (!r->empty()) {
            CHECK(r->x == 0.0f);
            CHECK(r->w == 1920.0f);
        }
    }
    CHECK(l.chord_label_visible);
    CHECK(l.status_visible);
    CHECK(l.block_labels_visible);
    CHECK_FALSE(l.key_labels_visible); // names_on_keys is off by default
}

TEST_CASE("the keyboard is the requested fraction of the window height") {
    LookSettings s;
    s.keyboard_height = 0.25f;
    const ScreenLayout l = compute_layout(1920, 1080, kPpmm96, s);
    CHECK(l.keyboard.h == 270.0f);
    s.keyboard_height = 0.12f;
    CHECK(compute_layout(1920, 1080, kPpmm96, s).keyboard.h == 129.0f); // floor(129.6)
}

TEST_CASE("a taller keyboard setting leaves less for the notes") {
    float last_keyboard = 0.0f;
    float last_notes = 1e9f;
    for (float kb : {0.10f, 0.14f, 0.18f, 0.22f, 0.26f, 0.30f}) {
        LookSettings s;
        s.keyboard_height = kb;
        const ScreenLayout l = compute_layout(1280, 720, kPpmm96, s);
        CHECK(l.keyboard.h > last_keyboard);
        CHECK(l.note_area.h < last_notes);
        last_keyboard = l.keyboard.h;
        last_notes = l.note_area.h;
    }
}

TEST_CASE("no overlap, nothing outside the window, notes keep 40 percent: every size and setting") {
    const std::vector<LookSettings> looks = sweep_looks();
    for (const Case& c : sweep_cases()) {
        for (const LookSettings& look : looks) {
            check_invariants(compute_layout(c.w, c.h, c.ppmm, look), c.w, c.h, "sweep");
        }
    }
}

TEST_CASE("from 800x480 up the chord label, labels and status line all fit at the defaults") {
    for (const Case& c : sweep_cases()) {
        if (c.w < kMinWindowWidthPx || c.h < kMinWindowHeightPx || c.ppmm > 6.5f) {
            continue;
        }
        const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        CHECK(l.chord_label_visible);
        CHECK(l.status_visible);
        CHECK(l.block_labels_visible);
        CHECK(l.top_row.h >= kMinTouchTargetMm * c.ppmm);
    }
}

TEST_CASE("the chord label shrinks with the window, then hides") {
    LookSettings s;
    float last = 1e9f;
    bool hidden = false;
    for (float h : {2160.0f, 1080.0f, 720.0f, 480.0f, 300.0f, 200.0f, 120.0f, 60.0f}) {
        const ScreenLayout l = compute_layout(1280, h, kPpmm96, s);
        check_invariants(l, 1280, h, "shrinking");
        if (!l.chord_label_visible) {
            hidden = true;
        } else {
            CHECK_FALSE(hidden); // once hidden, a smaller window never brings it back
            CHECK(l.chord_label_font_px <= last);
            last = l.chord_label_font_px;
        }
    }
    CHECK(hidden);
    CHECK(last >= kMinChordFontPx);
}

TEST_CASE("the chord label size setting scales the font until the strip is the limit") {
    LookSettings small;
    small.chord_label_size = kMinChordLabelSize;
    LookSettings big;
    big.chord_label_size = kMaxChordLabelSize;
    const ScreenLayout a = compute_layout(1920, 1080, kPpmm96, small);
    const ScreenLayout b = compute_layout(1920, 1080, kPpmm96, big);
    CHECK(b.chord_label_font_px > a.chord_label_font_px);
    CHECK(b.chord_label.h > a.chord_label.h);
    CHECK(b.note_area.h < a.note_area.h);
    // A short window caps the font at what the strip allows, whatever the setting.
    const ScreenLayout c = compute_layout(800, 480, kPpmm96, small);
    const ScreenLayout d = compute_layout(800, 480, kPpmm96, big);
    CHECK(d.chord_label_font_px <= 0.12f * 480.0f / kLineHeightEm);
    CHECK(d.chord_label_font_px >= c.chord_label_font_px);
}

TEST_CASE("chord label off gives no strip") {
    LookSettings s;
    s.chord_label = false;
    const ScreenLayout l = compute_layout(1280, 720, kPpmm96, s);
    CHECK_FALSE(l.chord_label_visible);
    CHECK(l.chord_label.h == 0.0f);
    CHECK(l.chord_label_font_px == 0.0f);
    check_invariants(l, 1280, 720, "chord off");
}

TEST_CASE("the status text hides in short windows before anything collides") {
    const ScreenLayout tall = compute_layout(1280, 720, kPpmm96, LookSettings{});
    CHECK(tall.status_visible);
    const ScreenLayout flat = compute_layout(1280, 40, kPpmm96, LookSettings{});
    CHECK_FALSE(flat.status_visible);
    check_invariants(flat, 1280, 40, "flat");
}

TEST_CASE("note labels follow the names setting and hide when too small to read") {
    LookSettings s;
    s.names_on_blocks = true;
    s.names_on_keys = true;
    const ScreenLayout on = compute_layout(1280, 720, kPpmm96, s);
    CHECK(on.block_labels_visible);
    CHECK(on.key_labels_visible);
    CHECK(on.note_label_font_px >= kMinLabelFontPx);

    s.name_mode = NameMode::Off;
    const ScreenLayout off = compute_layout(1280, 720, kPpmm96, s);
    CHECK_FALSE(off.block_labels_visible);
    CHECK_FALSE(off.key_labels_visible);

    s.name_mode = NameMode::Letters;
    s.names_on_blocks = false;
    const ScreenLayout keys_only = compute_layout(1280, 720, kPpmm96, s);
    CHECK_FALSE(keys_only.block_labels_visible);
    CHECK(keys_only.key_labels_visible);

    s.names_on_blocks = true;
    const ScreenLayout tiny = compute_layout(1280, 60, kPpmm96, s);
    CHECK_FALSE(tiny.block_labels_visible);
    CHECK_FALSE(tiny.key_labels_visible);
}

TEST_CASE("a label font shrinks to the cell, then hides") {
    // 14 px font, 3 characters: needs 3 * 0.62 * 14 + 4 = 30 px.
    CHECK(fit_label_font_px(14.0f, 100.0f, 3) == 14.0f);
    CHECK(fit_label_font_px(14.0f, 30.1f, 3) == 14.0f);
    CHECK(fit_label_font_px(14.0f, 30.0f, 3) == 13.0f); // floor of 13.98
    CHECK(fit_label_font_px(14.0f, 20.0f, 3) == 0.0f);  // would be 8.6 px: unreadable
    CHECK(fit_label_font_px(14.0f, 0.0f, 3) == 0.0f);
    CHECK(fit_label_font_px(14.0f, -5.0f, 3) == 0.0f);
    CHECK(fit_label_font_px(14.0f, 100.0f, 0) == 0.0f);
    CHECK(fit_label_font_px(0.0f, 100.0f, 3) == 0.0f);
    CHECK(fit_label_font_px(std::numeric_limits<float>::quiet_NaN(), 100.0f, 3) == 0.0f);
    CHECK(fit_label_font_px(14.0f, std::numeric_limits<float>::quiet_NaN(), 3) == 0.0f);
    // More characters need more room.
    CHECK(fit_label_font_px(14.0f, 40.0f, 1) == 14.0f);
    CHECK(fit_label_font_px(14.0f, 40.0f, 5) < 14.0f);
}

TEST_CASE("whatever fit_label_font_px returns, the text fits the cell") {
    for (float cell = 0.0f; cell < 80.0f; cell += 0.5f) {
        for (int chars = 1; chars <= 6; ++chars) {
            const float f = fit_label_font_px(16.0f, cell, chars);
            if (f > 0.0f) {
                CHECK(f >= kMinLabelFontPx);
                CHECK(f <= 16.0f);
                CHECK(static_cast<float>(chars) * kLabelCharWidthEm * f + 2.0f * kLabelPaddingPx <=
                      cell);
            }
        }
    }
}

TEST_CASE("bad inputs give empty layouts or the default density, never garbage") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float bad : {-5.0f, 0.0f, nan, -inf}) {
        const ScreenLayout l = compute_layout(bad, 480, kPpmm96, LookSettings{});
        check_invariants(l, bad, 480, "bad width");
        const ScreenLayout m = compute_layout(800, bad, kPpmm96, LookSettings{});
        check_invariants(m, 800, bad, "bad height");
    }
    const ScreenLayout reference = compute_layout(1280, 720, kDefaultPxPerMm, LookSettings{});
    for (float bad : {0.0f, -3.0f, nan, inf}) {
        const ScreenLayout l = compute_layout(1280, 720, bad, LookSettings{});
        check_invariants(l, 1280, 720, "bad density");
        CHECK(l.top_row.h == reference.top_row.h);
    }
    LookSettings wild;
    wild.keyboard_height = nan;
    wild.chord_label_size = inf;
    check_invariants(compute_layout(1280, 720, kPpmm96, wild), 1280, 720, "wild look");
}

// ---- Behaviour the first renderer's layout had and the renderer still relies on ----

TEST_CASE("at 1080p the chord label is calm and the notes get most of the screen") {
    const ScreenLayout l = compute_layout(1920, 1080, kPpmm96, LookSettings{});
    check_invariants(l, 1920, 1080, "1080p");
    // Sized from the window height, not 8 mm, and no banner: a share of what the first renderer
    // drew (kChordLabelShare), so the strip leaves the notes about 70% of the screen.
    CHECK(l.chord_label_font_px >= 0.04f * 1080.0f);
    CHECK(l.chord_label_font_px <= 0.06f * 1080.0f);
    CHECK(l.chord_label.h <= 0.06f * 1080.0f);
    CHECK(l.note_area.h >= 0.65f * 1080.0f);
    CHECK(l.note_area.h >= 0.5f * 1080.0f);
    CHECK(l.status_font_px >= 0.02f * 1080.0f);
}

TEST_CASE("the chord label gives way before the note area does, then hides") {
    LookSettings s;
    s.chord_label_size = kMaxChordLabelSize;
    for (float h : {480.0f, 600.0f, 720.0f, 1080.0f}) {
        const ScreenLayout l = compute_layout(1920, h, kPpmm96, s);
        check_invariants(l, 1920, h, "max chord size");
        CHECK(l.note_area.h >= kMinNoteAreaShare * h);
    }
    // No room left once the keyboard and top row are taken: the label hides instead of shrinking.
    s.keyboard_height = kMaxKeyboardHeight;
    const ScreenLayout tight = compute_layout(1280, 150, 5.0f, s);
    check_invariants(tight, 1280, 150, "tight");
    CHECK_FALSE(tight.chord_label_visible);
    CHECK(tight.chord_label_font_px == 0.0f);
    CHECK(tight.chord_label.h == 0.0f);
}

TEST_CASE("the keyboard height is the setting, whatever the key width") {
    // The first renderer sized the keyboard from the white-key width, clamped to 10..20 percent.
    // Now the look's keyboard_height decides, so layouts do not depend on the key count.
    LookSettings s;
    s.keyboard_height = 0.2f;
    const ScreenLayout a = compute_layout(1920, 1080, kPpmm96, s);
    const ScreenLayout b = compute_layout(800, 2000, kPpmm96, s);
    CHECK(a.keyboard.h == std::floor(1080.0f * 0.2f));
    CHECK(b.keyboard.h == std::floor(2000.0f * 0.2f));
}

TEST_CASE("the key bed sits on top of the keyboard and the keys start where it ends") {
    const ScreenLayout l = compute_layout(1920, 1080, kPpmm96, LookSettings{});
    CHECK(l.key_bed.y == l.keyboard.y);
    CHECK(l.key_bed.y == l.note_area.bottom()); // the hit line is the top of the bed
    CHECK(l.key_bed.bottom() == l.key_area.y);
    CHECK(l.key_area.bottom() == l.keyboard.bottom());
    CHECK(l.key_bed.h >= kMinKeyBedPx);
    CHECK(l.felt_line_h >= 1.0f);
    CHECK(l.felt_line_h < l.key_bed.h);
    // The bed scales with the keyboard, and stays usable at the 800x480 floor.
    const ScreenLayout small = compute_layout(800, 480, 5.0f, LookSettings{});
    const ScreenLayout big = compute_layout(3840, 2160, kPpmm96, LookSettings{});
    CHECK(small.key_bed.h >= kMinKeyBedPx);
    CHECK(big.key_bed.h > small.key_bed.h);
    CHECK(big.key_shadow_h > small.key_shadow_h);
}

TEST_CASE("C markers have their own switch and size, the same as the key labels") {
    LookSettings s;
    s.name_mode = NameMode::Off;
    s.names_on_keys = false;
    const ScreenLayout on = compute_layout(1280, 720, kPpmm96, s);
    CHECK(on.c_markers_visible); // on by default, with every name switch off
    CHECK_FALSE(on.key_labels_visible);
    CHECK_FALSE(on.block_labels_visible);
    CHECK(on.c_marker_font_px >= kMinLabelFontPx);
    s.names_on_keys = true;
    s.name_mode = NameMode::Letters;
    const ScreenLayout both = compute_layout(1280, 720, kPpmm96, s);
    CHECK(both.c_marker_font_px == both.key_label_font_px);
    s.c_markers = false;
    const ScreenLayout off = compute_layout(1280, 720, kPpmm96, s);
    CHECK_FALSE(off.c_markers_visible);
    CHECK(off.c_marker_font_px == 0.0f);
    // A keyboard too short for readable text hides them, like every other label.
    CHECK_FALSE(compute_layout(1280, 60, kPpmm96, LookSettings{}).c_markers_visible);
}

TEST_CASE("a C marker shrinks with the key width and hides before it collides") {
    // "C4" on a white key: 14 px font needs 2 * 0.62 * 14 + 4 = 21.4 px.
    CHECK(fit_label_font_px(14.0f, 24.0f, 2) == 14.0f);
    CHECK(fit_label_font_px(14.0f, 18.0f, 2) < 14.0f);
    CHECK(fit_label_font_px(14.0f, 18.0f, 2) >= kMinLabelFontPx);
    // 88 keys across 800 px: 15 px keys, "C-1" (3 characters) is unreadable there.
    CHECK(fit_label_font_px(14.0f, 800.0f / 52.0f - 1.6f, 3) == 0.0f);
}
