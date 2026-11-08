// SPDX-License-Identifier: GPL-3.0-or-later
// The medal wall of the Progress page (#396): every achievement as a medal, in two views
// (a category at a time as large medals with the closest-to-next strip; or every
// category in a row on one screen), and the ladder card a tap on a medal opens. The geometry is
// medal_wall_layout.hpp's; the numbers are core's MedalSet, cached here with their words so no frame
// builds a string. The view is the player's, kept in the profile's settings.
#pragma once

#include "kit.hpp"
#include "medal_wall_layout.hpp"

#include "etude/medal_eval.hpp"
#include "etude/session_state.hpp"

#include <array>
#include <string>
#include <vector>

namespace etude::app::ui {

class MedalWall {
  public:
    // Draws the wall in `body`; the view toggle writes `app.badge_wall_view`.
    void draw(Kit& k, ImDrawList* dl, const Box& body, etude::AppSettings& app,
              const etude::MedalSet& set);
    // The page was left: the ladder closes.
    void close_ladder() { ladder_ = -1; }
    bool ladder_open() const { return ladder_ >= 0; }
    // The ladder of an achievement opened without a tap (the --panel shots).
    void open_ladder(int achievement) { ladder_ = achievement; }

  private:
    struct Words {
        std::string caption;   // "Silver · 14 days", or "2 days to start"
    };
    // The words of the open ladder, built when it opens or the tier held changes, not each frame.
    struct LadderWords {
        int achievement = -1;
        int held = -1;
        std::string rule;                                      // what the next tier asks
        std::array<std::string, etude::kTierCount> asks;     // what each rung asks
    };
    void refresh(const etude::MedalSet& set);
    void refresh_ladder(const etude::AchievementInfo& info, int held);
    void draw_tabs(Kit& k, ImDrawList* dl, const WallLayout& lay);
    void draw_category(Kit& k, ImDrawList* dl, const WallLayout& lay, const etude::MedalSet& set);
    void draw_rows(Kit& k, ImDrawList* dl, const WallLayout& lay, const etude::MedalSet& set);
    void draw_strip(Kit& k, ImDrawList* dl, const WallLayout& lay);
    WallLines lines_under(const Kit& k, const Box& body, const std::array<int, 5>& counts) const;
    void draw_ladder(Kit& k, ImDrawList* dl, const Box& body, const etude::MedalSet& set);
    // One medal's press: opens the ladder.
    bool press_slot(Kit& k, const WallSlot& slot, int achievement);

    int category_ = 0;
    int ladder_ = -1; // Achievement index, -1: closed
    etude::MedalSet cached_;
    bool have_cache_ = false;
    std::array<Words, etude::kAchievementCount> words_{};
    std::vector<etude::MedalProgress> near_;
    std::array<std::string, 3> near_have_{}; // "12 of 14 days" for each of `near_`, built with it
    LadderWords ladder_words_;
    std::array<std::vector<int>, etude::kMedalCategoryCount> members_; // achievements of each category
};

} // namespace etude::app::ui
