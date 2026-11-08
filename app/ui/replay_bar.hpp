// SPDX-License-Identifier: GPL-3.0-or-later
// The replay's bar: while a run's replay is up it takes the place of the control bar and the section
// band. The top row holds restart, back five seconds, play and pause, forward five, the player's
// own sound and the speed; the band row holds the run bar (the run bar by bar, coloured by how each
// bar went, with the playhead on it: tap or drag to seek), the time, the previous and next mistake,
// the summary page and practise. Where the parts go is replay_bar_layout.hpp; what is shown comes
// from the app (`ReplayInfo`), which also acts on the choice that comes back.
#pragma once

#include "kit.hpp"
#include "replay_bar_layout.hpp"
#include "replay_port.hpp"

namespace etude::app::ui {

class ReplayBarView {
  public:
    // Draws the two rows: `bar` is the part of the top row the controls may use and `band` the row
    // under it (both ImGui points), `row` the whole top row. Returns what was tapped this frame.
    ReplayChoice draw(Kit& k, const ReplayInfo& info, const RectF& row, const RectF& bar,
                      const RectF& band, float display_w);
};

} // namespace etude::app::ui
