// SPDX-License-Identifier: GPL-3.0-or-later
// The Settings > View "Screen size" choices: the display's width in millimetres for a panel that
// reports the wrong size (`screen_width_mm`; 0 reads it from the display). A tap on the row moves to
// the next choice. No ImGui here, so a test can run it.
#pragma once

#include "etude/session_state.hpp"

#include <array>
#include <cstdio>
#include <string>

namespace etude::app::ui {

struct ScreenSizeChoice {
    int width_mm; // 0: Auto
    const char* label;
};

// Common panels by their diagonal, 16:9 (a 7 inch 800x480 panel is 154 mm wide).
inline constexpr std::array<ScreenSizeChoice, 8> kScreenSizes{{
    {0, "Auto"},
    {154, "7\""},
    {222, "10\""},
    {294, "13.3\""},
    {344, "15.6\""},
    {476, "21.5\""},
    {598, "27\""},
    {1000, "40\"+"},
}};

// The row's value: a choice's label, or the width when a hand-edited file holds another.
inline std::string screen_size_text(int width_mm) {
    for (const ScreenSizeChoice& c : kScreenSizes) {
        if (c.width_mm == width_mm) {
            return c.label;
        }
    }
    char buf[24];
    std::snprintf(buf, sizeof buf, "%d mm", width_mm);
    return buf;
}

// The width after a tap: the next larger choice, and Auto after the last. A width that is not a
// choice moves to the next larger one.
inline int next_screen_size(int width_mm) {
    for (const ScreenSizeChoice& c : kScreenSizes) {
        if (c.width_mm > width_mm) {
            return c.width_mm;
        }
    }
    return 0;
}

} // namespace etude::app::ui
