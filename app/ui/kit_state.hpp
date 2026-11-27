// SPDX-License-Identifier: GPL-3.0-or-later
// One helper draws every control's state.
#pragma once

#include "face_layout.hpp"
#include "kit_core.hpp"

#include <imgui.h>

#include <cstdint>

namespace etude::app::ui {

// --- States --------------------------------------------------------------------------------------
// One helper draws every control's state, inside its face and nowhere else:
//   idle      the face's idle fill (`Idle`), or nothing for an icon button;
//   hover     `col::kHover` over it, from a mouse only, never a finger;
//   pressed   `col::kPressed` from the frame the finger lands (no ease in), the content brighter;
//             it fades out over `tok::kFast`;
//   selected  the accent at `tok::kSelectedFill` with a hairline of it, the content in the accent;
//             a lit hand is its own colour (`tint`) with no line;
//   focus     a `tok::kFocusRing` accent ring inset `tok::kHairline` in the face;
//   disabled  no hover or pressed, the content at `tok::kDisabledAlpha`.
struct State {
    bool hover = false;
    float pressed = 0.0f;  // 1 while held, then fading
    float selected = 0.0f; // eased 0..1
    bool focus = false;
    bool disabled = false;
    const ImVec4* tint = nullptr; // a hand's colour: lit in it, no line
};
enum class Idle : std::uint8_t {
    None,   // an icon button: nothing until a state
    Fill,   // a filled control: `col::kFill`
    Accent, // the primary action: filled with the accent
};
// The state of the item just drawn (ImGui's last item): hover from a mouse only, pressed from its
// first frame, `selected` eased toward `on`, `disabled` as given; and the audit's forced state.
// `live` false (a bar still fading in) takes no hover or press.
State item_state(const Kit& k, bool on, bool live = true, bool disabled = false,
                 const ImVec4* tint = nullptr);
// Draws `s` on `face` with corners `radius` (0: the kit's `tok::kRadiusControl`, never more than
// half the face). `area` is what the face must stay inside (its card, pill or hit area), for the
// audit; `name` names the control there.
void draw_state(const Kit& k, ImDrawList* dl, const char* name, const Box& face, const Box& area,
                const State& s, Idle idle = Idle::None, float radius = 0.0f);
// The content colour in state `s`: `base`, the accent (or the hand's colour) as it is selected,
// brighter while pressed, faded when disabled. On the accent fill (`Idle::Accent`) pass
// `col::kAccentInk` and `on_accent`.
ImVec4 state_ink(const State& s, ImVec4 base, bool on_accent = false);
// The corner radius of a face `w` by `h`: `radius` (0 for the control radius), at most half the
// shorter side.
float face_radius(const Kit& k, const Box& face, float radius = 0.0f);

} // namespace etude::app::ui
