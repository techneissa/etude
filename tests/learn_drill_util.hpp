// SPDX-License-Identifier: GPL-3.0-or-later
// What the Learn layout tests share about the drill's controls: the boxes a card puts on the screen.
#pragma once

#include "ui/learn_layout.hpp"

#include <string>
#include <utility>
#include <vector>

namespace learn_test {

using Controls = std::vector<std::pair<std::string, etude::app::ui::Box>>;

// The drill's settings where the card has them: the stepper, the three directions, the octaves and
// (on the scale card) scale / arpeggio in the column or the header; or the one button that opens them.
inline void add_drill_controls(Controls& out, const etude::app::ui::LearnCardLayout& c) {
    using etude::app::ui::LearnColumn;
    if (c.column == LearnColumn::Popover) {
        out.push_back({"settings", c.settings});
        return;
    }
    out.push_back({"tempo", c.drill.tempo});
    for (std::size_t i = 0; i < c.drill.dir.size(); ++i) {
        out.push_back({"direction " + std::to_string(i), c.drill.dir[i]});
    }
    for (std::size_t i = 0; i < c.drill.oct.size(); ++i) {
        out.push_back({"octaves " + std::to_string(i), c.drill.oct[i]});
    }
    for (std::size_t i = 0; i < c.drill.kind.size(); ++i) {
        if (c.drill.kind[i].w() > 0.0f) {
            out.push_back({"kind " + std::to_string(i), c.drill.kind[i]});
        }
    }
}

} // namespace learn_test
