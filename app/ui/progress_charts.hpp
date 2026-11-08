// SPDX-License-Identifier: GPL-3.0-or-later
// What the Progress page draws inside its cards: the practice calendar, the accuracy and speed
// graph, the best run of each song, the goal ring and the tiles. Each takes the card it fills (its
// box from progress_layout.hpp) and the numbers (core ProgressView); none presses anything.
#pragma once

#include "kit.hpp"
#include "progress_layout.hpp"

#include "etude/progress_view.hpp"

#include <span>

namespace etude::app::ui {

namespace progress_col {
inline constexpr ImVec4 kTeal{0.17f, 0.78f, 0.69f, 1.0f};  // practice done, the goal met
inline constexpr ImVec4 kGold{1.0f, 0.80f, 0.30f, 1.0f};   // first place
inline constexpr ImVec4 kFlame{1.0f, 0.60f, 0.24f, 1.0f};  // a streak
} // namespace progress_col

// A card's face: the surface, the corner radius of cards, and its title in the card's top left.
void progress_card(Kit& k, ImDrawList* dl, const Box& box, const char* title);

// "Practice calendar": a column a week, Monday at the top, the cells shaded by their level against
// the goal, today ringed.
void draw_calendar(Kit& k, ImDrawList* dl, const Box& box, const etude::ProgressView& view);
// "Accuracy and speed": the accuracy of the latest counting runs as a line over its area, the speed
// of each as a grey tick under it.
void draw_graph(Kit& k, ImDrawList* dl, const Box& box, const etude::ProgressView& view);
// "Best per song": a row for each song `rows` has a box for.
void draw_best(Kit& k, ImDrawList* dl, const Box& box, std::span<const Box> rows,
               const etude::ProgressView& view);
// The time practised: a ring against the goal, the week's time and the month's.
void draw_ring(Kit& k, ImDrawList* dl, const Box& box, const etude::ProgressView& view);
// A small card with an icon, a bold line and a caption under it (songs, rank).
void draw_tile(Kit& k, ImDrawList* dl, const Box& box, const char* icon, const char* bold,
               const char* caption);

} // namespace etude::app::ui
