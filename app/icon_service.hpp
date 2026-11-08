// SPDX-License-Identifier: GPL-3.0-or-later
// The app icon's colour at run time: puts the chosen colour's icon on the window, which is the Dock
// icon on a Mac, and on Linux and the Pi also switches the user's launcher entry to that colour's
// icon set (desktop_entry.hpp); and holds the best title the device's profiles reached, which the
// About page's colour row asks for through ui::IconPort. The window calls `sync` every frame, so the
// choice is applied at start-up and on every change, and none of it needs root.
#pragma once

#include "etude/icon_colour.hpp"
#include "etude/medals.hpp"
#include "ui/icon_port.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace etude::app {

class IconService final : public ui::IconPort {
  public:
    // `quiet`: a developer run (a script, the audit, a headless window) changes no
    // icon and writes no file, so nothing a run does outlives it. `fixed_title` >= 0 stands for the
    // best title with no history behind it (`--icon-title`, and the About demo).
    IconService(bool quiet, int fixed_title)
        : quiet_(quiet), best_(fixed_title >= 0 ? fixed_title : 0), fixed_(fixed_title >= 0) {
        if (fixed_title >= 1) {
            points_ = etude::title_points(std::min(fixed_title, etude::kTierCount));
        }
    }

    // IconPort: the page asking for it is what makes the window work it out.
    int best_title() override {
        asked_ = true;
        return best_;
    }

    int best_points() override { return points_; }
    void open_medals() override { medals_asked_ = true; }
    bool take_medals() { return std::exchange(medals_asked_, false); }

    // Whether the page asked since the last call (it is open), and the answer from the history.
    bool take_asked() {
        const bool was = asked_;
        asked_ = false;
        return was;
    }
    bool fixed() const { return fixed_; }
    void set_best(int title, int points) {
        best_ = title;
        points_ = points;
    }

    // Applies `colour` when it is not the one applied last. Returns whether it changed anything.
    bool sync(SDL_Window* window, etude::IconColour colour);

  private:
    bool quiet_;
    int best_ = 0;
    int points_ = 0;
    bool fixed_ = false;
    bool asked_ = false;
    bool medals_asked_ = false;
    std::optional<etude::IconColour> applied_;
};

} // namespace etude::app
