// SPDX-License-Identifier: GPL-3.0-or-later
// The geometry the release rule rests on (the touch rules): a control fires when the finger
// lifts inside the control the press started in, sliding off cancels, a movement under the slop is
// still a tap. The controls get "released over me" from ImGui's button (which fires on the release,
// over the item the press began on) and "was a tap" from `tapped` (tap.hpp, core TapTracker). What is
// shared with the tests is the geometry those buttons are made from: the settings row's parts
// (`row_tap_parts`, here), a key's hit area (`keyboard_key_hit`), a card's and a row's controls and tap
// areas (`card_parts`, `list_row`), and the key ring's decision (`key_ring_release`); tests/
// release_rule_test.cpp checks that no hit area reaches over another, so a lift on one can never fire
// the other. That no control fires on the press is guarded by tests/touch_fire_guard_test.cpp.
#pragma once

#include "face_layout.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

// A settings row's tap area: the whole row, or with an info button in it (`info`, its hit square)
// two parts either side of the button and a dead gap `gap` clear of it, so no part reaches over the
// button. `count` parts are used; the first has the row's id and the second "-r" after it.
struct RowTapParts {
    std::array<Box, 2> part{};
    int count = 0;
};
inline RowTapParts row_tap_parts(const Box& row, const Box* info, float gap) {
    RowTapParts p;
    if (info == nullptr) {
        p.part[0] = row;
        p.count = 1;
        return p;
    }
    p.part[0] = {row.x0, row.y0, std::min(row.x1, info->x0 - gap), row.y1};
    p.part[1] = {std::max(row.x0, info->x1 + gap), row.y0, row.x1, row.y1};
    p.count = 2;
    return p;
}

} // namespace etude::app::ui
