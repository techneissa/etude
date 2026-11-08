// SPDX-License-Identifier: GPL-3.0-or-later
// The order the chosen trouble bars are practised in one by one, and the drag that changes it.
// Plain values and a pure state machine: the results card draws the list and feeds this the
// pointer, so the same input always gives the same order.
#pragma once

#include "etude/results_card.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace etude {

// The chosen bars in the order they are played: `slot[i]` is the index into `ResultsCard::bars` of
// the i-th bar. Starts in song order (a card's bars are sorted by how many misses they had, so
// the order is made from their bar numbers).
struct BarOrder {
    std::array<std::uint8_t, kMaxCardBars> slot{};
    int count = 0;
    friend bool operator==(const BarOrder&, const BarOrder&) = default;
};

// The bars of `card` in `chosen` (bit i for bars[i]), in song order: the default order.
BarOrder song_order(const ResultsCard& card, std::uint32_t chosen);

// Moves the bar at place `from` to place `to`, the bars between shifting by one. False, and nothing
// changed, for a place outside the order or `from == to`.
bool order_move(BarOrder& order, int from, int to);
// One place earlier or later; false at the ends.
bool order_up(BarOrder& order, int place);
bool order_down(BarOrder& order, int place);

// A drag of one row of the order list. A press picks a row; nothing moves until the pointer has
// travelled the drag threshold (so a tap never nudges a row); after that the row follows the
// pointer, and the place under it is where the row goes (`place_at`).
//
//   press(place, x, y)       a pointer went down on the row at `place`
//   move(x, y, rows, slop)   once the drag is on, the place the row takes now when that changed
//   release()                ends the drag; true when it moved the row
class ReorderDrag {
  public:
    // The places run down `columns` columns (1 or 2, the places filling the first column before the
    // second), each column `column_w` wide from `left` with `column_gap` between, from `top` down
    // in steps of `row_h` (the same units as the pointer).
    struct Rows {
        float top = 0.0f;
        float row_h = 1.0f;
        int count = 0;
        int columns = 1;
        float left = 0.0f;
        float column_w = 0.0f;
        float column_gap = 0.0f;
    };

    void press(int place, float x, float y);
    // The pointer moved to (`x`, `y`). Once the drag threshold has been passed, returns the place
    // the held row takes now when that is not the place it had (`place()` is the new one, the
    // caller moves the row from the old to it with `order_move`); nothing before that or while the
    // row stays.
    std::optional<int> move(float x, float y, const Rows& rows, float threshold);
    // The pointer lifted, or the touch was taken away (`cancelled`). True when the drag moved the
    // row (the caller keeps what it did), false for a tap.
    bool release(bool cancelled = false);

    bool active() const { return active_; }     // a row is held (it may not have moved yet)
    bool dragging() const { return dragging_; } // past the threshold
    int place() const { return place_; }        // the held row's place now

  private:
    bool active_ = false;
    bool dragging_ = false;
    int place_ = 0;
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
};

// The place under the pointer at (`x`, `y`) in `rows`, clamped to 0..count-1 (0 for an empty list).
// With one column `x` does not matter; with two, the column the pointer is over (the nearer one
// outside both).
int place_at(const ReorderDrag::Rows& rows, float x, float y);

} // namespace etude
