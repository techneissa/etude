// SPDX-License-Identifier: GPL-3.0-or-later
// Buttons: icon, text and action buttons, the sheet header, the two-tap confirm.
#pragma once

#include "kit_core.hpp"

#include <imgui.h>

namespace etude::app::ui {

// An icon button with a long-press label, `size` square hit, drawn smaller. `danger` turns red.
bool icon_action(Kit& k, const char* id, const char* icon, const char* label, float size,
                 bool enabled, bool danger = false, bool armed = false);
// A full-width quiet button: icon + text. A second tap within 3 s is needed when `confirm`.
bool text_button(Kit& k, const char* id, const char* icon, const char* label, bool confirm,
                 const char* confirm_label = nullptr);
// A button with an icon and a caption at `pos`, `size` big (the touch target; the face is a little
// smaller). `primary` fills it with the accent, `danger` tints it red. With `confirm`, a second
// tap within 3 s is needed and the caption changes to `confirm_label`. Returns true on a tap; a
// disabled one never does. Several can sit side by side, unlike text_button's full-width row. A
// bigger button gives its face (`face_h`, in design points, up to `size`) and its words (`label_pt`);
// its icon grows with the words. `icon_ink` (may be null) is a colour the icon takes: while the
// button is held, or always with `icon_ink_always`.
bool action_button(Kit& k, const char* id, const char* icon, const char* label, ImVec2 pos,
                   ImVec2 size, bool enabled, bool primary = false, bool danger = false,
                   bool confirm = false, const char* confirm_label = nullptr, float face_h = 0.0f,
                   float label_pt = 0.0f, const ImVec4* icon_ink = nullptr,
                   bool icon_ink_always = false);
// Sheet or card header: back chevron (when `back`), title, close (when `close`). Returns 1 for
// back, 2 for close, 0 otherwise.
int header(Kit& k, const char* title, bool back, bool close);
// Two-tap confirm state for `id`.
bool armed(const Kit& k, ImGuiID id);

} // namespace etude::app::ui
