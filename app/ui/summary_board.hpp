// SPDX-License-Identifier: GPL-3.0-or-later
// The summary page's Board tab: the run's points and how they compare, the household's best run of
// the song per period as a table, and the player's own best runs of it as a chart
// (summary_chart.hpp). What goes where is summary_board_layout.hpp (placed by summary_layout.hpp);
// what it shows is core's `SongBoard` (song_board.hpp), built by the app and handed over in
// `ReviewInfo::board`.
#pragma once

#include "kit.hpp"
#include "replay_port.hpp"
#include "summary_board_text.hpp"
#include "summary_chart.hpp"
#include "summary_layout.hpp"

#include "etude/leaderboard.hpp"

#include <array>
#include <span>

namespace etude::app::ui {

// A segmented track drawn into the cells the layout placed, each a hit tall with its face inside:
// the header's Timing | Board and the Board's Week | Month | All. Cell i is the button
// "<id_prefix>i". True when another cell than `*current` was tapped; `*current` is then it. A
// cell's hit area is the cell less half the dead gap `dead` at the side of a neighbour.
// The cells' words cut to their width, for `segment_cells` to reuse while the type size, the cell
// width and the labels stay the same (a lookup per cell and font each frame is saved).
struct SegmentFit {
    static constexpr std::size_t kMax = 3;
    float size = 0.0f;
    float width = 0.0f;
    const char* const* labels = nullptr;
    std::size_t count = 0;
    std::array<FitText, kMax> regular;
    std::array<FitText, kMax> bold;
};
// With `fits`, the cut words are kept there (at most `SegmentFit::kMax` cells).
bool segment_cells(Kit& k, const char* id_prefix, std::span<const RectF> cells,
                   std::span<const char* const> labels, int* current, float dead,
                   SegmentFit* fits = nullptr);

// The Board tab's body, as `l.board` placed it. `*period` is the Week | Month | All track's cell
// (core `Period`), which a tap on the track changes. True when the chart's view button was tapped:
// the app keeps the view (`BoardInfo::chart_view`).
bool draw_board_tab(Kit& k, ImDrawList* dl, const BoardInfo& board, const SummaryLayout& l,
                    Period* period, SummaryChart* chart, SegmentFit* period_fit);

// The Runs tab's body, as `l.runs` placed it: the player's chart alone, under its label. True when
// the chart's view button was tapped. `period` picks the board the chart comes from.
bool draw_runs_tab(Kit& k, ImDrawList* dl, const BoardInfo& board, const SummaryLayout& l,
                   Period period, SummaryChart* chart);

} // namespace etude::app::ui
