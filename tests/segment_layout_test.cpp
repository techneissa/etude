// SPDX-License-Identifier: GPL-3.0-or-later
// A segmented control never makes a cell under a hit area and the dead gap to the next (54 pt): it
// wraps into rows instead.
#include "ui/segment_layout.hpp"

#include <doctest/doctest.h>

#include <array>
#include <span>

using namespace etude::app::ui;

TEST_CASE("segment_rows: cells keep a hit and a gap, the row wraps when they cannot fit") {
    for (const float u : {1.0f, 1.25f, 1.5f, 1.406f}) {
        for (int n = 1; n <= 8; ++n) {
            for (float w = tok::kCell * u; w <= 700.0f * u; w += 3.7f) {
                const SegmentRows r = segment_rows(w, n, 20.0f * u, u);
                CAPTURE(u);
                CAPTURE(n);
                CAPTURE(w);
                CHECK(r.cell_w >= tok::kCell * u - 1e-3f);
                // The cell less the dead gap is a full hit area.
                CHECK(r.cell_w - tok::kHitGap * u >= tok::kHit * u - 1e-3f);
                CHECK(r.rows * r.per_row >= n);
                CHECK((r.rows - 1) * r.per_row < n); // no empty row
                CHECK(r.cell_w * static_cast<float>(r.per_row) == doctest::Approx(w));
            }
        }
    }
}

TEST_CASE("segment_rows: the Resume after row, 6 cells") {
    // Exactly six cells of a cell's width: one row.
    const float six = 6.0f * tok::kCell;
    const SegmentRows one = segment_rows(six, 6, 20.0f, 1.0f);
    CHECK(one.rows == 1);
    CHECK(one.cell_w == doctest::Approx(tok::kCell));
    // A point less: two rows of three, never 5 and 1.
    const SegmentRows two = segment_rows(six - 1.0f, 6, 20.0f, 1.0f);
    CHECK(two.rows == 2);
    CHECK(two.per_row == 3);
    CHECK(two.cell_w == doctest::Approx((six - 1.0f) / 3.0f));
    // 179 pt (where the cells were 29.8 pt wide): two rows of three, 59.7 pt each.
    const SegmentRows old = segment_rows(179.0f, 6, 20.0f, 1.0f);
    CHECK(old.rows == 2);
    CHECK(old.cell_w >= tok::kCell);
}

TEST_CASE("segment_rows: a cell wider than a hit for its label keeps that width") {
    const SegmentRows r = segment_rows(300.0f, 3, 120.0f, 1.0f);
    CHECK(r.rows == 2);
    CHECK(r.per_row == 2);
    CHECK(r.cell_w >= 120.0f);
    // Exactly enough for all three: one row.
    CHECK(segment_rows(360.0f, 3, 120.0f, 1.0f).rows == 1);
}

TEST_CASE("segment_cell_boxes: the pill is inset evenly and stays inside the hit area") {
    for (const float u : {1.0f, 1.25f, 1.5f, 1.406f}) {
        for (const float w : {tok::kCell * u, 104.0f * u, 132.0f * u, 358.0f * u}) {
            const float x0 = 17.0f * u;
            const SegmentCellBoxes b = segment_cell_boxes(x0, x0 + w, u);
            CAPTURE(u);
            CAPTURE(w);
            const float inset = segment_pill_inset(u);
            // The same distance in from the track's edge on all four sides.
            CHECK(b.pill_x0 - x0 == doctest::Approx(inset));
            CHECK(x0 + w - b.pill_x1 == doctest::Approx(inset));
            CHECK(b.pill_y0 == doctest::Approx(inset));
            CHECK(b.track_h - b.pill_y1 == doctest::Approx(inset));
            CHECK(b.track_h == doctest::Approx(tok::kFace * u));
            // The pill is within the hit area, which is the cell less half the dead gap each side.
            CHECK(b.hit_x0 - x0 == doctest::Approx(tok::kHitGap * u * 0.5f));
            CHECK(x0 + w - b.hit_x1 == doctest::Approx(tok::kHitGap * u * 0.5f));
            CHECK(b.pill_x0 >= b.hit_x0 - 1e-3f);
            CHECK(b.pill_x1 <= b.hit_x1 + 1e-3f);
            // A hit area is at least 12 mm for a cell a hit and a gap wide.
            CHECK(b.hit_x1 - b.hit_x0 >= tok::kHit * u - 1e-3f);
        }
    }
}

TEST_CASE("segment_cell_boxes: two neighbouring cells keep the dead gap and one pill never touches the next") {
    const float u = 1.0f;
    const SegmentCellBoxes a = segment_cell_boxes(0.0f, 104.0f, u);
    const SegmentCellBoxes b = segment_cell_boxes(104.0f, 208.0f, u);
    CHECK(b.hit_x0 - a.hit_x1 == doctest::Approx(tok::kHitGap * u));
    CHECK(b.pill_x0 - a.pill_x1 >= tok::kHitGap * u - 1e-3f);
}

namespace {

// Steps a slide with 60 Hz frames until it lands; returns the frames it took.
int run_slide(PillSlide& s, int cell, float x0, float x1) {
    int frames = 0;
    while (s.t < 1.0f || frames == 0) {
        pill_slide_step(s, cell, 0, x0, x1, 1.0f / 60.0f, false, false);
        ++frames;
        if (frames > 1000) {
            break;
        }
    }
    return frames;
}

} // namespace

TEST_CASE("pill_slide: the first draw puts the pill on its cell with no motion") {
    PillSlide s;
    pill_slide_step(s, 1, 0, 100.0f, 200.0f, 1.0f / 60.0f, true, false);
    float x0 = 0.0f;
    float x1 = 0.0f;
    pill_slide_at(s, &x0, &x1);
    CHECK(s.t == doctest::Approx(1.0f));
    CHECK(x0 == 100.0f);
    CHECK(x1 == 200.0f);
    // The same cell on the next frames stays put.
    pill_slide_step(s, 1, 0, 100.0f, 200.0f, 1.0f / 60.0f, false, false);
    pill_slide_at(s, &x0, &x1);
    CHECK(x0 == 100.0f);
    CHECK(x1 == 200.0f);
    // A control not drawn on the frame before (a page just opened) jumps to a changed choice.
    pill_slide_step(s, 2, 0, 300.0f, 400.0f, 1.0f / 60.0f, true, false);
    pill_slide_at(s, &x0, &x1);
    CHECK(x0 == 300.0f);
    CHECK(x1 == 400.0f);
}

TEST_CASE("pill_slide: eased position at the start, the middle and the end, landing exactly") {
    CHECK(pill_ease(0.0f) == 0.0f);
    CHECK(pill_ease(1.0f) == 1.0f);
    CHECK(pill_ease(0.5f) == doctest::Approx(0.875f)); // ease-out: past halfway at half the time
    CHECK(pill_ease(0.25f) > 0.25f);
    CHECK(pill_ease(-1.0f) == 0.0f);
    CHECK(pill_ease(2.0f) == 1.0f);
    PillSlide s;
    pill_slide_step(s, 0, 0, 0.0f, 100.0f, 0.016f, true, false);
    // A change of cell: the pill starts on the old cell (the first frame has moved only a little)
    // and ends on the new one to the exact edges.
    float x0 = 0.0f;
    float x1 = 0.0f;
    pill_slide_step(s, 1, 0, 123.4f, 321.7f, 0.0f, false, false);
    pill_slide_at(s, &x0, &x1);
    CHECK(s.t == 0.0f);
    CHECK(x0 == 0.0f);
    CHECK(x1 == 100.0f);
    // Half the time in: at the eased fraction of the way, both edges (the width changes too).
    pill_slide_step(s, 1, 0, 123.4f, 321.7f, tok::kPillSlide * 0.5f, false, false);
    pill_slide_at(s, &x0, &x1);
    CHECK(x0 == doctest::Approx(123.4f * 0.875f));
    CHECK(x1 == doctest::Approx(100.0f + (321.7f - 100.0f) * 0.875f));
    // The end: exactly the cell's edges, and the move is over after about 200 ms.
    PillSlide t;
    pill_slide_step(t, 0, 0, 0.0f, 100.0f, 0.016f, true, false);
    const int frames = run_slide(t, 1, 123.4f, 321.7f);
    CHECK(frames >= 12);
    CHECK(frames <= 13);
    pill_slide_at(t, &x0, &x1);
    CHECK(x0 == 123.4f);
    CHECK(x1 == 321.7f);
    CHECK(tok::kPillSlide >= 0.18f);
    CHECK(tok::kPillSlide <= 0.22f);
}

TEST_CASE("pill_slide: reduced effects jump at once, and so does another row") {
    PillSlide s;
    pill_slide_step(s, 0, 0, 0.0f, 100.0f, 1.0f / 60.0f, true, true);
    pill_slide_step(s, 2, 0, 200.0f, 300.0f, 1.0f / 60.0f, false, true);
    float x0 = 0.0f;
    float x1 = 0.0f;
    pill_slide_at(s, &x0, &x1);
    CHECK(s.t == 1.0f);
    CHECK(x0 == 200.0f);
    CHECK(x1 == 300.0f);
    PillSlide r;
    pill_slide_step(r, 0, 0, 0.0f, 100.0f, 1.0f / 60.0f, true, false);
    pill_slide_step(r, 3, 1, 10.0f, 110.0f, 1.0f / 60.0f, false, false);
    pill_slide_at(r, &x0, &x1);
    CHECK(x0 == 10.0f);
    CHECK(x1 == 110.0f);
}

TEST_CASE("pill_slide: a new target mid-slide re-aims from where the pill is now") {
    PillSlide s;
    pill_slide_step(s, 0, 0, 0.0f, 100.0f, 1.0f / 60.0f, true, false);
    pill_slide_step(s, 1, 0, 200.0f, 300.0f, 0.0f, false, false);
    pill_slide_step(s, 1, 0, 200.0f, 300.0f, tok::kPillSlide * 0.4f, false, false);
    float here0 = 0.0f;
    float here1 = 0.0f;
    pill_slide_at(s, &here0, &here1);
    CHECK(here0 > 0.0f);
    CHECK(here0 < 200.0f);
    // The tap on another cell: the move starts at that point, not at the old cell, with no jump.
    pill_slide_step(s, 2, 0, 400.0f, 500.0f, 0.0f, false, false);
    float x0 = 0.0f;
    float x1 = 0.0f;
    pill_slide_at(s, &x0, &x1);
    CHECK(s.t == 0.0f);
    CHECK(x0 == doctest::Approx(here0));
    CHECK(x1 == doctest::Approx(here1));
    for (int i = 0; i < 20; ++i) {
        pill_slide_step(s, 2, 0, 400.0f, 500.0f, 1.0f / 60.0f, false, false);
    }
    pill_slide_at(s, &x0, &x1);
    CHECK(x0 == 400.0f);
    CHECK(x1 == 500.0f);
}

TEST_CASE("pill_slide: a resize mid-slide ends on the cell's new edges, and a resize at rest follows") {
    PillSlide s;
    pill_slide_step(s, 0, 0, 0.0f, 100.0f, 1.0f / 60.0f, true, false);
    pill_slide_step(s, 1, 0, 100.0f, 200.0f, 0.0f, false, false);
    pill_slide_step(s, 1, 0, 100.0f, 200.0f, 0.05f, false, false);
    // The window narrows while the pill moves: the same chosen cell, new edges.
    for (int i = 0; i < 30; ++i) {
        pill_slide_step(s, 1, 0, 80.0f, 160.0f, 1.0f / 60.0f, false, false);
    }
    float x0 = 0.0f;
    float x1 = 0.0f;
    pill_slide_at(s, &x0, &x1);
    CHECK(x0 == 80.0f);
    CHECK(x1 == 160.0f);
    // At rest the pill follows its cell at once: no slide for a resize.
    pill_slide_step(s, 1, 0, 60.0f, 140.0f, 1.0f / 60.0f, false, false);
    pill_slide_at(s, &x0, &x1);
    CHECK(s.t == 1.0f);
    CHECK(x0 == 60.0f);
    CHECK(x1 == 140.0f);
}

TEST_CASE("cell_weight_step: the words fade with the pill, and jump when fresh or reduced") {
    float w = 0.0f;
    CHECK(cell_weight_step(&w, true, 0.0f, false, false) == 0.0f);
    CHECK(cell_weight_step(&w, true, tok::kPillSlide * 0.5f, false, false) == doctest::Approx(0.875f));
    CHECK(cell_weight_step(&w, true, tok::kPillSlide, false, false) == 1.0f);
    // Out again: from 1 down to exactly 0, and in between it is mid-way.
    float out = 1.0f;
    const float mid = cell_weight_step(&out, false, tok::kPillSlide * 0.5f, false, false);
    CHECK(mid > 0.0f);
    CHECK(mid < 1.0f);
    CHECK(cell_weight_step(&out, false, tok::kPillSlide, false, false) == 0.0f);
    float fresh = 0.3f;
    CHECK(cell_weight_step(&fresh, true, 0.016f, true, false) == 1.0f);
    float reduced = 0.3f;
    CHECK(cell_weight_step(&reduced, false, 0.016f, false, true) == 0.0f);
}

TEST_CASE("pill_bold_cell: one cell's words are bold in every frame, and each changes once") {
    // Three cells, as the Progress | Household | Medals tabs: centres 52, 156, 260.
    const std::array<float, 3> centres{52.0f, 156.0f, 260.0f};
    const std::span<const float> cells(centres);
    CHECK(pill_bold_cell(52.0f, cells) == 0);
    CHECK(pill_bold_cell(103.9f, cells) == 0);
    CHECK(pill_bold_cell(104.1f, cells) == 1);
    CHECK(pill_bold_cell(260.0f, cells) == 2);
    CHECK(pill_bold_cell(500.0f, cells) == 2);
    CHECK(pill_bold_cell(0.0f, std::span<const float>()) == -1);
    // Slide the pill from the last cell to the first and, at every 60 Hz frame, count the cells with
    // bold words (always exactly one: the words of a cell are never drawn in two faces) and the
    // changes of face per cell (the cell that loses the choice and the one that gains it change once;
    // the one the pill passes over is bold only while the pill's centre is nearest it).
    PillSlide s;
    pill_slide_step(s, 2, 0, 208.0f, 312.0f, 1.0f / 60.0f, true, false);
    pill_slide_step(s, 0, 0, 0.0f, 104.0f, 0.0f, false, false);
    std::array<int, 3> changes{};
    std::array<bool, 3> was{false, false, true};
    int frames = 0;
    do {
        pill_slide_step(s, 0, 0, 0.0f, 104.0f, 1.0f / 60.0f, false, false);
        float x0 = 0.0f;
        float x1 = 0.0f;
        pill_slide_at(s, &x0, &x1);
        const int bold = pill_bold_cell((x0 + x1) * 0.5f, cells);
        int bold_count = 0;
        for (int i = 0; i < 3; ++i) {
            const bool now = i == bold;
            bold_count += now ? 1 : 0;
            changes[static_cast<std::size_t>(i)] += now != was[static_cast<std::size_t>(i)] ? 1 : 0;
            was[static_cast<std::size_t>(i)] = now;
        }
        CHECK(bold_count == 1);
        ++frames;
    } while (s.t < 1.0f && frames < 100);
    CHECK(changes[0] == 1); // gains bold once
    CHECK(changes[1] == 2); // bold only while the pill is nearest it: on, then off
    CHECK(changes[2] == 1); // loses it once
    CHECK(was[0]);
}
