// SPDX-License-Identifier: GPL-3.0-or-later
// The two ImGui backends' new frame (Ui::draw).
#pragma once

#include "exact_shot.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

#include <algorithm>

namespace etude::app::ui {

// The first frame after the loop waited for an event has the wait in its delta, and no animation
// may jump to its end because of it. A screenshot steps a fixed 60 Hz, whatever the wall clock
// says.
inline void new_imgui_frame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::GetIO().DeltaTime =
        exact_shot() ? 1.0f / 60.0f : std::min(ImGui::GetIO().DeltaTime, 0.1f);
}

} // namespace etude::app::ui
