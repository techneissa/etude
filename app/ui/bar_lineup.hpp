// SPDX-License-Identifier: GPL-3.0-or-later
// The one loop that draws a bar's controls, for all three styles: the controls of the form
// (bar_metrics::lineup) left to right, each in the cell bar_metrics gives it in that style. A style
// draws only what goes round them: the strip's dividers, the dock's card, the groups' cards.
#pragma once

#include "accompaniment.hpp"
#include "bar.hpp"
#include "bar_metrics.hpp"
#include "tempo.hpp"

namespace etude::app::ui {

// How a style draws the controls, in pixels except `cells` (design points).
struct LineupLook {
    bar_metrics::StyleCells cells;
    float song_face = 0.0f;   // the faces of the song's controls
    float switch_face = 0.0f; // the metronome's, the look shortcuts' and the pin's
    float chip_face = 0.0f;   // the tempo chip's and the Flow/Wait chip's face height
    bool captions = false;    // a caption under each control (bar_metrics::caption_of)
    bool prominent_play = false;
};

// What a style draws round the controls; called as the loop reaches each group.
class LineupFrame {
  public:
    virtual ~LineupFrame() = default;
    // Before a group's controls: the group runs from `x0` to `x1` (its padding included).
    virtual void group(bar_metrics::Group g, float x0, float x1) {
        (void)g;
        (void)x0;
        (void)x1;
    }
    // Between two groups, in the middle of the gap.
    virtual void divider(float x) { (void)x; }
};

// Draws the controls of `f.form` from `x` (the bar's left end), in the top row from `row_top`,
// `h` tall. A look shortcut or a long press on the metronome asks `request` to open its page.
// Returns the right end of the last control.
float draw_lineup(Kit& k, UiModel& model, SongPort& song, const Frame& f, const LineupLook& look,
                  float x, float row_top, float h, TempoControls& tempo,
                  AccompanimentMenu& accomp, OpenRequest& request,
                  LineupFrame& frame);

} // namespace etude::app::ui
