// SPDX-License-Identifier: GPL-3.0-or-later
// The gear and the icon toggle.
#pragma once

#include "kit_core.hpp"
#include "kit_icon.hpp"

#include <imgui.h>

namespace etude::app::ui {

// --- Controls ----------------------------------------------------------------------------------
// All of these lay out at ImGui's cursor and take the full content width unless a width is given.

// The always-visible settings button: icon only, no fill, no border. `active` while settings are
// open, when the gear turns into a cross. Pressed: a little smaller and brighter. Returns true on a
// tap.
bool gear(Kit& k, ImVec2 centre, float hit, bool active, float alpha);

// An icon in a square hit area at `centre`. `on` draws the selected state (accent icon on a soft
// accent square); `caption` (may be null) is drawn under the icon; `label` shows on long press.
// `live` false: drawn but not pressable (a bar that is still fading in).
// `tint` (may be null) is the colour the on state lights up in instead of the accent;
// `shake_x` moves the icon sideways (a refused tap); `hit_dy` moves the hit area down from the icon
// (a touch target kept clear of the controls above it).
bool icon_toggle(Kit& k, const char* id, Icon icon, const char* label, ImVec2 centre, ImVec2 hit,
                 float drawn, bool on, bool live, const char* caption = nullptr,
                 const ImVec4* tint = nullptr, float shake_x = 0.0f, float hit_dy = 0.0f,
                 float glyph_dy = 0.0f);

} // namespace etude::app::ui
