// SPDX-License-Identifier: GPL-3.0-or-later
// Slider and stepper rows.
#pragma once

#include "kit_core.hpp"
#include "reset_to.hpp"

#include "etude/unit_format.hpp"

#include <optional>

namespace etude::app::ui {

// A slider; the value shown is `*value * shown_scale` in `unit` with `decimals` places
// (unit_format.hpp). `reset_to` is the setting's own default: with it the row has a reset button at
// its trailing end, greyed and doing nothing while the value is the default, active once it differs,
// and a tap on it puts the default back (reset_to.hpp). The default comes from where the setting
// defines it, never a second copy of the number.
bool slider_row(Kit& k, const char* icon, const char* label, float* value, float lo, float hi,
                float shown_scale, Unit unit, int decimals = 0, const char* info = nullptr,
                std::optional<ResetTo<float>> reset_to = std::nullopt);
bool slider_row_int(Kit& k, const char* icon, const char* label, int* value, int lo, int hi,
                    Unit unit, const char* info = nullptr,
                    std::optional<ResetTo<int>> reset_to = std::nullopt);
// A number with a minus and a plus button beside the label, one per tap, kept in `lo`..`hi`;
// `unit` follows the number. Never stacks. `skip_dir` -1 or 1 draws a skip arrow (back or forward)
// with the value inside instead of `icon`; its number morphs as the value changes.
bool stepper_row(Kit& k, const char* icon, const char* label, int* value, int lo, int hi,
                 Unit unit, int skip_dir = 0, std::optional<ResetTo<int>> reset_to = std::nullopt);
// The same with a value that is words (a key's name): the text between the buttons, each button
// greyed when its direction is shut. Returns -1 or 1 for the button tapped, else 0.
int stepper_text_row(Kit& k, const char* icon, const char* label, const char* text, bool can_minus,
                     bool can_plus);

} // namespace etude::app::ui
