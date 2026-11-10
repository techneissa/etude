// SPDX-License-Identifier: GPL-3.0-or-later
// "How it went": the summary page the results card opens in its own place. It shows the timing of
// the run (a ruler of how early or late the presses fell), the two hands side by side and the worst
// bars as slim rows with a watch and a practise chip each, then Watch replay and Practise these
// bars. What it shows is core's `RunSummary` (built on the history thread); where its parts go is
// summary_layout.hpp. A Timing | Board track in the header swaps the body for the song's board
// (summary_board.hpp); where the board has no room for the runs chart a third cell, Runs, shows
// the chart alone. It holds nothing but how far in it is and which tab and period are showing:
// a tap comes back as a choice the app acts on.
#pragma once

#include "kit.hpp"
#include "replay_port.hpp"
#include "summary_board.hpp"
#include "summary_chart.hpp"
#include "summary_layout.hpp"

namespace etude::app::ui {

class SummaryPageView {
  public:
    // Draws the page for `review` centred in `area`, core's `summary_area` in ImGui points, at the
    // in-play card's scale `card_scale` (the UI's when 0), and returns what was tapped this frame. `Back` is not an action: the page reports it in `back()` and the
    // card closes it. A page that has just opened fades and scales in over `tok::kMove`.
    ReviewChoice draw(Kit& k, const ReviewInfo& review, const RectF& area, float card_scale = 0.0f);

    // Starts the next draw's fade-in (the page was just opened).
    void open() {
        progress_ = 0.0f;
        back_ = false;
        tab_ = 0;
    }
    // Back was tapped in the last draw.
    bool back() const { return back_; }
    // The page waits out of sight (the replay is up): it is not drawn, and keeps its state.
    void hide() { shown_ = false; }
    bool shown() const { return shown_; }
    float scale() const { return scale_; }
    // The page shows its Board tab (it opened on Timing).
    void show_board() { tab_ = 1; }
    // The Runs tab is the one showing (it exists only where the Board tab has no room for the chart).
    bool runs_tab() const { return tab_ == 2; }
    void show_runs() { tab_ = 2; }

  private:
    ReviewChoice draw_body(Kit& k, ImDrawList* dl, const ReviewInfo& review, const SummaryLayout& l);

    bool shown_ = false;
    bool back_ = false;
    float scale_ = 0.0f;
    float progress_ = 0.0f;
    int tab_ = 0; // 0 Timing, 1 Board, 2 Runs
    Period period_ = Period::Month;
    SummaryChart chart_;
    SegmentFit tab_fit_;    // the header's tab words, cut to their cells
    SegmentFit period_fit_; // the same for the Board's Week | Month | All
};

} // namespace etude::app::ui
