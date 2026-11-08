// SPDX-License-Identifier: GPL-3.0-or-later
// Where the replay bar's controls go: five touch targets and the speed chip in the top row, the run
// bar, its time and four tools in the band's row, at every size and UI scale, pinned strip layout.
#include "ui/replay_bar_layout.hpp"

#include "etude/look.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct ScreenSize {
    float w, h;
};
constexpr ScreenSize kSizes[] = {{800, 480}, {1024, 600}, {1366, 768}, {1920, 1080}, {2560, 1440}};

bool overlap(const RectF& a, const RectF& b) {
    constexpr float kEps = 0.01f;
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

} // namespace

TEST_CASE("replay bar layout: the controls fit the pinned strip at every size and scale") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            CAPTURE(sz.w);
            CAPTURE(scale);
            const float px_per_mm = kDefaultPxPerMm * scale;
            const ScreenLayout lay = compute_layout(
                sz.w, sz.h, px_per_mm, LookSettings{}, PinnedBar{true, BarStyle::Strip, true});
            REQUIRE_FALSE(lay.bar_under.empty());
            const float u = ui_scale(sz.w, sz.h, px_per_mm);
            const float bar_w = replay_bar_width(u);
            const TopRowSplit split = split_top_row(lay, bar_w, 0.0f, false);
            const ReplayBarLayout l = replay_bar_layout(split.bar, lay.bar_under, u);
            const float hit = tok::kHit * u;
            std::vector<RectF> top(l.cells.begin(), l.cells.end());
            top.push_back(l.speed);
            std::vector<RectF> band{l.track, l.time};
            band.insert(band.end(), l.tools.begin(), l.tools.end());
            for (std::size_t i = 0; i < l.cells.size(); ++i) {
                CHECK(l.cells[i].w >= hit - 0.01f);
                CHECK(l.cells[i].h >= hit - 0.01f);
            }
            for (std::size_t i = 0; i < l.tools.size(); ++i) {
                CHECK(l.tools[i].w >= hit - 0.01f);
                CHECK(l.tools[i].h >= hit - 0.01f);
                CHECK(l.tools[i].bottom() <= lay.bar_under.bottom() + 0.01f);
            }
            for (std::size_t i = 0; i < top.size(); ++i) {
                CHECK(top[i].x >= split.bar.x - 0.01f);
                // The row's split lands on whole pixels: a control may overhang by less than one.
                CHECK(top[i].right() <= split.bar.right() + 1.0f);
                for (std::size_t j = i + 1; j < top.size(); ++j) {
                    CHECK_FALSE(overlap(top[i], top[j]));
                }
            }
            if (l.fits) {
                for (std::size_t i = 0; i < band.size(); ++i) {
                    CHECK(band[i].x >= lay.bar_under.x - 0.01f);
                    CHECK(band[i].right() <= lay.bar_under.right() + 0.01f);
                    for (std::size_t j = i + 1; j < band.size(); ++j) {
                        CHECK_FALSE(overlap(band[i], band[j]));
                    }
                }
                CHECK(l.track.w >= hit - 0.01f);
            }
        }
    }
}

TEST_CASE("replay bar layout: the run bar takes what the time and the tools leave") {
    const ScreenLayout lay = compute_layout(1366.0f, 768.0f, kDefaultPxPerMm, LookSettings{},
                                            PinnedBar{true, BarStyle::Strip, true});
    const float u = ui_scale(1366.0f, 768.0f, kDefaultPxPerMm);
    const TopRowSplit split = split_top_row(lay, replay_bar_width(u), 0.0f, false);
    const ReplayBarLayout l = replay_bar_layout(split.bar, lay.bar_under, u);
    REQUIRE(l.fits);
    CHECK(l.track.x < l.time.x);
    CHECK(l.time.right() <= l.tools[0].x + 0.01f);
    CHECK(l.track.w > 6.0f * tok::kHit * u); // a long bar on a wide screen
    // The controls run in order: restart, back, play, forward, sound, then the speed.
    for (std::size_t i = 1; i < l.cells.size(); ++i) {
        CHECK(l.cells[i].x > l.cells[i - 1].x);
    }
    CHECK(l.speed.x > l.cells.back().right() - 0.01f);
}

TEST_CASE("replay bar layout: the pin is the fourth tool, between the summary and practise") {
    for (const ScreenSize& sz : {ScreenSize{1366, 768}, ScreenSize{800, 480}}) {
        CAPTURE(sz.w);
        const float px_per_mm = kDefaultPxPerMm;
        const ScreenLayout lay = compute_layout(sz.w, sz.h, px_per_mm, LookSettings{},
                                                PinnedBar{true, BarStyle::Strip, true});
        const float u = ui_scale(sz.w, sz.h, px_per_mm);
        const TopRowSplit split = split_top_row(lay, replay_bar_width(u), 0.0f, false);
        const ReplayBarLayout l = replay_bar_layout(split.bar, lay.bar_under, u);
        REQUIRE(l.fits);
        static_assert(replay_tok::kTools == 5);
        const float hit = tok::kHit * u;
        const RectF& pin = l.tools[3];
        CHECK(pin.w >= hit - 0.01f);
        CHECK(pin.h >= hit - 0.01f);
        CHECK(pin.x > l.tools[2].x); // after the summary
        CHECK(pin.x < l.tools[4].x); // before practise
        CHECK_FALSE(overlap(pin, l.tools[2]));
        CHECK_FALSE(overlap(pin, l.tools[4]));
        CHECK(pin.right() <= lay.bar_under.right() + 0.01f);
        CHECK(l.track.w >= hit - 0.01f); // the run bar keeps its room with the extra tool
    }
}

TEST_CASE("replay bar layout: a row too narrow for the run bar says so") {
    const RectF bar{0, 0, 300, 44};
    const RectF band{0, 44, 100, 44};
    const ReplayBarLayout l = replay_bar_layout(bar, band, 1.0f);
    CHECK_FALSE(l.fits);
}
