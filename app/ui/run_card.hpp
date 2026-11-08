// SPDX-License-Identifier: GPL-3.0-or-later
// The two cards of a trouble-bar run, drawn over the note area while the song waits: the check-in
// after each bar (how it went, then Next bar, Again, Skip, End) and the summary of the whole run
// (every bar's accuracy before and after, then Done). What they show comes from core's TroubleRun;
// where their parts go from run_card_layout.hpp. They hold no run state: a tap comes back as an
// action and the app applies it to the run.
#pragma once

#include "kit.hpp"
#include "run_card_layout.hpp"

#include "etude/trouble_run.hpp"

#include <cstdint>

namespace etude::app::ui {

class RunCardView {
  public:
    // Draws the card for `run` (null: none) centred in `area`, the note area in ImGui points: the
    // check-in while the run waits after a bar, the summary once it is done; nothing for a bar that
    // is playing or no run. Returns what was tapped this frame. A card that is new (another bar,
    // or the summary) fades and scales in over `tok::kMove` (reduced effects: at once).
    RunCardAction draw(Kit& k, const TroubleRun* run, const RectF& area);

    bool shown() const { return shown_; }
    // The scale the card was last drawn at, in pixels per design point; 0 before the first.
    float scale() const { return scale_; }

  private:
    bool shown_ = false;
    bool summary_ = false;
    float scale_ = 0.0f;
    float progress_ = 0.0f;
    // What the card last showed, to know when a new one begins.
    RunPhase last_phase_ = RunPhase::Idle;
    int last_current_ = -1;
};

} // namespace etude::app::ui
