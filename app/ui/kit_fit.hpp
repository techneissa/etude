// SPDX-License-Identifier: GPL-3.0-or-later
// Text cut to a width.
#pragma once

#include "fit_text.hpp"

#include <imgui.h>

#include <string_view>

namespace etude::app::ui {

// `text` cut to `max_w` with an ellipsis. Cached per (font, size, width, text), so a label that did
// not change is neither measured nor copied into the heap.
FitText fit(ImFont* font, float size, std::string_view text, float max_w);
// `fit` without telling the audit about a cut: for a caller that cuts part of a label and reports the
// whole label itself (`row_label`).
FitText fit_unreported(ImFont* font, float size, std::string_view text, float max_w);

} // namespace etude::app::ui
