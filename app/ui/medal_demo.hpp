// SPDX-License-Identifier: GPL-3.0-or-later
// The medals on a screen of their own (`--panel medal-sheet` and `--panel medal-titles`, a developer's
// view): every material large and small, the title medals with their piano emblems, one badge up the
// ladder, and Diamond beside Obsidian, drawn by the app's own code so a shot of it is the medals as the
// app draws them.
#pragma once

#include "bar.hpp"

namespace etude::app::ui {

class MedalDemo {
  public:
    enum class Mode { Closed, Sheet, Titles };
    void open(Mode mode) { mode_ = mode; }
    void close() { mode_ = Mode::Closed; }
    bool is_open() const { return mode_ != Mode::Closed; }
    // Draws it under the top row, over everything under it.
    void draw(Kit& k, const Frame& f);

  private:
    void draw_sheet(Kit& k, ImDrawList* dl, const Box& body);
    void draw_titles(Kit& k, ImDrawList* dl, const Box& body);
    Mode mode_ = Mode::Closed;
};

} // namespace etude::app::ui
