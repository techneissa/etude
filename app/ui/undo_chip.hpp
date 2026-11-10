// SPDX-License-Identifier: GPL-3.0-or-later
// The Undo chip of a toast and its width. `chip()` draws a chip as wide
// as the larger of what its words need at the kit's own size and the width it was asked for. From
// 1366 points wide the toast's words and its chip's step up (the popup rule), so the chip must be
// asked for what the larger words need, or "Undo" is cut to "Un...".
#pragma once

#include <algorithm>

namespace etude::app::ui {

// The width the toast asks its Undo chip for: `words_chip_w` (what the chip's words need at the
// popup rule's size) from `step` 1 up; at step 0 the words are the kit's own and need no asking.
inline float undo_chip_asked_width(int step, float words_chip_w) {
    return step > 0 ? words_chip_w : 0.0f;
}

// What `chip()` draws for a chip whose words need `natural_w` at the kit's size, asked for
// `asked_w`.
inline float drawn_chip_w(float natural_w, float asked_w) {
    return std::max(natural_w, asked_w);
}

} // namespace etude::app::ui
