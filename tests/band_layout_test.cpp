// SPDX-License-Identifier: GPL-3.0-or-later
// The section band's parts (app/ui/band_layout.hpp) at the supported sizes and UI scales: every
// tool keeps 44 pt, nothing overlaps, everything stays on the band, and the track keeps its room.
#include "ui/band_layout.hpp"
#include "ui/face_layout.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr float kEps = 1e-3f;

bool overlap(const RectF& a, const RectF& b) {
    return !a.empty() && !b.empty() && a.x < b.right() - kEps && b.x < a.right() - kEps;
}

bool inside(const RectF& in, const RectF& out) {
    return in.x >= out.x - kEps && in.right() <= out.right() + kEps && in.y >= out.y - kEps &&
           in.bottom() <= out.bottom() + kEps;
}

} // namespace

TEST_CASE(
    "band layout: 44 pt tools, no overlap, on the band, at every size and scale, full and slim") {
    for (const auto& [w, h] :
         {std::pair{800.0f, 480.0f}, std::pair{1366.0f, 768.0f}, std::pair{1920.0f, 1080.0f}}) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            for (const bool slim : {false, true}) {
                CAPTURE(w);
                CAPTURE(scale);
                CAPTURE(slim);
                const float u = ui_scale(w, h, kDefaultPxPerMm * scale);
                // A headless window: one pixel a point.
                const RectF band{0.0f, 57.0f, w, bar_under_row_pt(true, slim) * u};
                const float time_w = 80.0f * u; // "12:34 / 45:67" at the caption size
                const BandLayout l = band_layout(band, time_w, u);
                std::vector<RectF> tools{l.loop, l.tools.cut, l.tools.list, l.tools.editor};
                // A narrow band gives up the suggested cuts and the lane's button.
                for (const RectF* t : {&l.tools.suggest, &l.tools.lane}) {
                    if (!t->empty()) {
                        tools.push_back(*t);
                    }
                }
                for (const RectF& t : tools) {
                    CHECK(t.w >= tok::kHit * u - kEps);
                    CHECK(t.h >= tok::kHit * u - kEps);
                    CHECK(inside(t, band));
                    CHECK_FALSE(overlap(t, l.track));
                    CHECK_FALSE(overlap(t, l.time));
                    // The tool's face fits on the band, its inset clear of the edge.
                    const Box area{t.x, band.y, t.right(), band.bottom()};
                    const FaceLayout f = lay_face(area, area.cx(), kBandFace * u, kBandFace * u,
                                                  tok::kIcon * u, 0.0f, 0.0f, u);
                    CHECK_FALSE(f.hidden);
                    CHECK(area.holds(f.face, -kEps));
                }
                for (std::size_t i = 0; i < tools.size(); ++i) {
                    for (std::size_t j = i + 1; j < tools.size(); ++j) {
                        CHECK_FALSE(overlap(tools[i], tools[j]));
                    }
                }
                CHECK(inside(l.track, band));
                CHECK(l.track.w >= kBandTrackMinPt * u - kEps);
                CHECK_FALSE(overlap(l.track, l.time));
                // The three sizes keep the time at scale 1.
                if (scale == 1.0f) {
                    CHECK_FALSE(l.time.empty());
                }
            }
        }
    }
}

TEST_CASE("band layout: the time goes before the track drops under its least width") {
    const float u = 1.0f;
    const RectF band{0.0f, 0.0f, 600.0f, 52.0f};
    const BandLayout wide = band_layout(band, 80.0f, u);
    CHECK(wide.track.w >= kBandTrackMinPt);
    // A time label that would leave the track under its least width is dropped whole.
    const float too_wide = wide.track.w + 80.0f - kBandTrackMinPt + 1.0f;
    const BandLayout tight = band_layout(band, too_wide, u);
    CHECK(tight.time.empty());
    CHECK(tight.track.w > wide.track.w);
    // Exactly at the least width it stays.
    const float exact = wide.track.w + 80.0f - kBandTrackMinPt;
    CHECK_FALSE(band_layout(band, exact, u).time.empty());
}

TEST_CASE(
    "band layout: a trouble-bar run adds skip and end of 44 pt between the cut and the list") {
    for (const auto& [w, h] :
         {std::pair{800.0f, 480.0f}, std::pair{1366.0f, 768.0f}, std::pair{1920.0f, 1080.0f}}) {
        for (const float scale : {1.0f, 1.5f}) {
            CAPTURE(w);
            CAPTURE(scale);
            const float u = ui_scale(w, h, kDefaultPxPerMm * scale);
            const RectF band{0.0f, 57.0f, w, bar_under_row_pt(true, false) * u};
            const BandLayout plain = band_layout(band, 80.0f * u, u);
            const BandLayout run = band_layout(band, 80.0f * u, u, true);
            // Without a run there are no such tools.
            CHECK(plain.tools.skip.empty());
            CHECK(plain.tools.end.empty());
            REQUIRE_FALSE(run.tools.skip.empty());
            REQUIRE_FALSE(run.tools.end.empty());
            for (const RectF& t : {run.tools.skip, run.tools.end}) {
                CHECK(t.w >= tok::kHit * u - kEps);
                CHECK(t.h >= tok::kHit * u - kEps);
                CHECK(inside(t, band));
                CHECK_FALSE(overlap(t, run.track));
                CHECK_FALSE(overlap(t, run.time));
            }
            // Left to right: cut, skip, end, list; the other tools stay where they were.
            CHECK(run.tools.cut.right() <= run.tools.skip.x + kEps);
            CHECK(run.tools.skip.right() <= run.tools.end.x + kEps);
            CHECK(run.tools.end.right() <= run.tools.list.x + kEps);
            // Where the band has the room for every tool the others stay where they were and the
            // track gives up the room; a narrow band lets the run's tools take the room of the ones
            // it gives up first (the lane, the suggested cuts), the cut, the list and the editor
            // staying.
            if (!run.tools.lane.empty() && !run.tools.suggest.empty()) {
                CHECK(run.tools.list.x == doctest::Approx(plain.tools.list.x));
                CHECK(run.tools.editor.x == doctest::Approx(plain.tools.editor.x));
                CHECK(run.track.w < plain.track.w);
            }
            for (const RectF& t : {run.tools.cut, run.tools.list, run.tools.editor}) {
                CHECK_FALSE(t.empty());
            }
            CHECK(run.track.w > 0.0f);
            if (scale == 1.0f) {
                CHECK(run.track.w >= kBandTrackMinPt * u - kEps);
            }
        }
    }
}

TEST_CASE("band layout: a narrow band gives up the suggested cuts, then the lane, never the cut, the list or the editor") {
    // The 7 inch panel at 800x480 and x1.5: 388 design points, where the track had 72 px with
    // every tool on. The tools that stay keep their 12 mm and the track keeps its least width.
    const float u = ui_scale(800.0f, 480.0f, 800.0f / 154.0f * 1.5f);
    const RectF band{0.0f, 117.0f, 800.0f, bar_under_row_pt(true, false) * u};
    const BandLayout l = band_layout(band, 80.0f * u, u);
    CHECK(l.tools.suggest.empty());
    CHECK(l.tools.lane.empty());
    for (const RectF& t : {l.loop, l.tools.cut, l.tools.list, l.tools.editor}) {
        CHECK(t.w >= tok::kHit * u - kEps);
        CHECK(inside(t, band));
    }
    // The loop toggle and three tools take 4 cells of the 388 points: the track keeps what is left
    // (156 points, from 72 px at 800x480 x1.5 with every tool on).
    CHECK(l.track.w >= 150.0f * u);
    // One tool fewer would have been enough where only the suggested cuts are over the room.
    const RectF wider{0.0f, 117.0f, 960.0f, band.h};
    const BandLayout one = band_layout(wider, 0.0f, u);
    CHECK(one.tools.suggest.empty());
    CHECK_FALSE(one.tools.lane.empty());
    CHECK(one.track.w >= kBandTrackMinPt * u - kEps);
    // With room for everything nothing goes.
    const BandLayout all = band_layout({0.0f, 0.0f, 1920.0f, band.h}, 0.0f, u);
    CHECK_FALSE(all.tools.suggest.empty());
    CHECK_FALSE(all.tools.lane.empty());
}

TEST_CASE("band layout: an open lane keeps its button on a narrow band, the track gives up the cell") {
    const float u = ui_scale(800.0f, 480.0f, 800.0f / 154.0f * 1.5f);
    const RectF band{0.0f, 117.0f, 800.0f, bar_under_row_pt(true, false) * u};
    const BandLayout shut = band_layout(band, 80.0f * u, u, false, false);
    const BandLayout open = band_layout(band, 80.0f * u, u, false, true);
    CHECK(shut.tools.lane.empty());
    REQUIRE_FALSE(open.tools.lane.empty()); // the touch that closes the lane
    CHECK(open.tools.lane.w >= tok::kHit * u - kEps);
    CHECK(open.tools.lane.h >= tok::kHit * u - kEps);
    CHECK(inside(open.tools.lane, band));
    CHECK(open.tools.suggest.empty()); // the suggestions still go first
    for (const RectF& t : {open.loop, open.tools.cut, open.tools.list, open.tools.editor}) {
        CHECK_FALSE(t.empty());
        CHECK_FALSE(overlap(t, open.tools.lane));
    }
    CHECK_FALSE(overlap(open.track, open.tools.lane));
    CHECK(open.track.w > 0.0f);
    CHECK(open.track.w < shut.track.w); // it gave up the lane's cell
    // A band with room never drops it, open or not.
    CHECK_FALSE(band_layout({0.0f, 0.0f, 1920.0f, band.h}, 0.0f, u, false, false).tools.lane.empty());
}

TEST_CASE("band layout: a trouble-bar run on a narrow band keeps the cut, skip, end, list and editor whole") {
    const float u = ui_scale(800.0f, 480.0f, 800.0f / 154.0f * 1.5f);
    const RectF band{0.0f, 117.0f, 800.0f, bar_under_row_pt(true, false) * u};
    const BandLayout l = band_layout(band, 80.0f * u, u, true);
    CHECK(l.tools.suggest.empty());
    CHECK(l.tools.lane.empty());
    const std::vector<RectF> tools{l.loop,       l.tools.cut,  l.tools.skip,
                                   l.tools.end,  l.tools.list, l.tools.editor};
    for (const RectF& t : tools) {
        REQUIRE_FALSE(t.empty());
        CHECK(t.w >= tok::kHit * u - kEps);
        CHECK(t.h >= tok::kHit * u - kEps);
        CHECK(inside(t, band));
        CHECK_FALSE(overlap(t, l.track));
        CHECK_FALSE(overlap(t, l.time));
    }
    for (std::size_t i = 0; i < tools.size(); ++i) {
        for (std::size_t j = i + 1; j < tools.size(); ++j) {
            CHECK_FALSE(overlap(tools[i], tools[j]));
        }
    }
    // Left to right: cut, skip, end, list, editor.
    CHECK(l.tools.cut.right() <= l.tools.skip.x + kEps);
    CHECK(l.tools.skip.right() <= l.tools.end.x + kEps);
    CHECK(l.tools.end.right() <= l.tools.list.x + kEps);
    CHECK(l.tools.list.right() <= l.tools.editor.x + kEps);
    CHECK(inside(l.track, band));
    CHECK(l.track.w > 0.0f);
}
