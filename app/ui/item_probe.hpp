// SPDX-License-Identifier: GPL-3.0-or-later
// The items (buttons, rows, sliders) Dear ImGui laid out in a frame, with their boxes, for the
// settings check (settings_check_taps.cpp), which taps them the way a person would. Off unless `enable` is called;
// the hooks are ImGui's own test-engine hooks (the etude_imgui target defines
// IMGUI_ENABLE_TEST_ENGINE), which cost one flag test per item while off.
#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace etude::app::ui::probe {

struct Item {
    ImGuiID id = 0;
    ImVec2 min{0, 0}; // the item's box, in ImGui points
    ImVec2 max{0, 0};
    ImVec2 clip_min{0, 0}; // the part of its window that shows (a scrolled-out item is outside)
    ImVec2 clip_max{0, 0};
    std::string window;    // the name of the window or child window it is drawn in
    float scroll_y = 0.0f; // that window's scroll position and its end
    float scroll_max_y = 0.0f;

    ImVec2 centre() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
    // The centre is inside the visible part, `inset` points clear of its edges.
    bool centre_visible(float inset) const {
        const ImVec2 c = centre();
        return c.x >= clip_min.x + inset && c.x <= clip_max.x - inset &&
               c.y >= clip_min.y + inset && c.y <= clip_max.y - inset;
    }
};

// The id ImGui gave the item `leaf` ("##plus", "##track") of the settings row labelled `label` (a
// slider or a stepper) in the page window `window`, with or without the two-column table around the
// rows: how the scripted run finds a row's button, and how the settings check proves it can.
ImGuiID row_item_id(const std::string& window, const char* label, const char* leaf, bool in_table);

// Starts or stops collecting.
void enable(bool on);
// The items collected since the last call become `items()` (the frame before this one).
void next_frame();
const std::vector<Item>& items();
// Adds a box the UI decides itself (a card's rectangle, as it tests a tap against it) under the
// window name `window`. Does nothing while the probe is off.
void add_rect(const char* window, ImVec2 min, ImVec2 max);

} // namespace etude::app::ui::probe
