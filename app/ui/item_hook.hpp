// SPDX-License-Identifier: GPL-3.0-or-later
// Every pressable item Dear ImGui draws, reported to the UI audit as it is drawn: the library is
// built with its item hooks on (IMGUI_ENABLE_TEST_ENGINE; item_probe.cpp implements them). Off
// until the audit sets a function; then each button, slider or text field that is not clipped away
// calls it with its id, its label and its rectangle in ImGui points.
#pragma once

#include <imgui.h>

namespace etude::app::ui {

using ItemHook = void (*)(ImGuiID id, const char* label, ImVec2 min, ImVec2 max);

// The function each item is reported to; nullptr turns the reports off (the default).
void set_item_hook(ItemHook fn);

} // namespace etude::app::ui
