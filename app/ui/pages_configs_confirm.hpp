// SPDX-License-Identifier: GPL-3.0-or-later
// The Configs page's copy question: "Copy Dani's config?" over the page, with Cancel and Copy.
#pragma once

#include "configs_logic.hpp"
#include "kit.hpp"

#include <cstdint>
#include <optional>

namespace etude::app::ui {

// Draws the question while `ask` is open, over a dimmed window, and answers it: the profile to copy
// from when Copy was tapped. Cancel, a tap outside the card, a profile that is gone and a page that
// stopped drawing it (the sheet closed, another page opened) close it and copy nothing.
std::optional<std::int64_t> draw_copy_confirm(Kit& k, CopyAsk& ask);
// Where the card is while it shows (the audit leaves what is under it out); empty when it does not.
bool copy_confirm_covers(ImVec2* min, ImVec2* max);

} // namespace etude::app::ui
