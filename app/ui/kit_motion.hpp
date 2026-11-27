// SPDX-License-Identifier: GPL-3.0-or-later
// The kit's motion helpers: easing and the per-widget tween.
#pragma once

#include <imgui.h>

namespace etude::app::ui {

// --- Motion ------------------------------------------------------------------------------------

float ease_out(float t);
// Turns every motion below into a jump (reduced effects). Set once per frame.
void set_reduced_motion(bool reduced);
// Moves linear progress `p` toward `target` (0 or 1) over `duration` seconds.
float approach(float p, float target, float dt, float duration);
// A per-widget float that eases toward `target` (stored in ImGui's state storage under `id`).
float tween(ImGuiID id, float target, float duration);

} // namespace etude::app::ui
