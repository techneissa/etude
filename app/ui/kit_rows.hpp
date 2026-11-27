// SPDX-License-Identifier: GPL-3.0-or-later
// Settings rows: sections, switches, navigation, choices, help text, readouts.
#pragma once

#include "kit_core.hpp"

#include <imgui.h>

#include <string_view>

namespace etude::app::ui {

void section(Kit& k, const char* text);
bool switch_row(Kit& k, const char* icon, const char* label, bool* value,
                const char* info = nullptr);
bool nav_row(Kit& k, const char* icon, const char* label, std::string_view value,
             const char* info = nullptr);
bool radio_row(Kit& k, const char* label, const char* detail, bool on, bool dim = false);
// The same, true for a tap on the chosen row too (a choice that greys, and says why on any tap).
bool radio_choice(Kit& k, const char* label, const char* detail, bool on, bool dim = false);
// A row like a switch row, with the current choice written in a small pill at the right instead
// of a knob; a tap anywhere on the row returns true and the caller moves to the next choice. For a
// setting whose choices would not fit as a segmented control on one row.
bool cycle_row(Kit& k, const char* icon, const char* label, const char* value,
               const char* info = nullptr);
// A paragraph at the cursor, under the words of a row (past its icon), broken at spaces into at
// most `max_lines` lines (the rest is dropped: the caller keeps its text short).
void help_text(Kit& k, const char* text, ImVec4 ink, int max_lines);
// How tall `help_text` is for `text` in a row `width` wide.
float help_text_height(Kit& k, const char* text, float width, int max_lines);
// A row of the same height with an icon and words and nothing to press, for what a setting
// currently is.
void readout_row(Kit& k, const char* icon, const char* text);

} // namespace etude::app::ui
