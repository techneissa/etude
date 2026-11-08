// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/bar_order.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

BarOrder song_order(const ResultsCard& card, std::uint32_t chosen) {
    BarOrder out;
    for (int i = 0; i < card.bar_count && i < kMaxCardBars; ++i) {
        if ((chosen & (1u << i)) != 0) {
            out.slot[static_cast<std::size_t>(out.count++)] = static_cast<std::uint8_t>(i);
        }
    }
    // Insertion sort by the bar's place in the song: at most kMaxCardBars entries.
    const auto measure = [&](int place) {
        return card.bars[out.slot[static_cast<std::size_t>(place)]].measure;
    };
    for (int i = 1; i < out.count; ++i) {
        for (int j = i; j > 0 && measure(j) < measure(j - 1); --j) {
            std::swap(out.slot[static_cast<std::size_t>(j)],
                      out.slot[static_cast<std::size_t>(j - 1)]);
        }
    }
    return out;
}

bool order_move(BarOrder& order, int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= order.count || to >= order.count) {
        return false;
    }
    const std::uint8_t held = order.slot[static_cast<std::size_t>(from)];
    if (from < to) {
        for (int i = from; i < to; ++i) {
            order.slot[static_cast<std::size_t>(i)] = order.slot[static_cast<std::size_t>(i) + 1];
        }
    } else {
        for (int i = from; i > to; --i) {
            order.slot[static_cast<std::size_t>(i)] = order.slot[static_cast<std::size_t>(i) - 1];
        }
    }
    order.slot[static_cast<std::size_t>(to)] = held;
    return true;
}

bool order_up(BarOrder& order, int place) { return order_move(order, place, place - 1); }

bool order_down(BarOrder& order, int place) { return order_move(order, place, place + 1); }

int place_at(const ReorderDrag::Rows& rows, float x, float y) {
    if (rows.count <= 0 || !(rows.row_h > 0.0f)) {
        return 0;
    }
    const int columns = std::clamp(rows.columns, 1, 2);
    const int per_column = (rows.count + columns - 1) / columns;
    const float at = std::floor((y - rows.top) / rows.row_h);
    const int line = std::clamp(static_cast<int>(std::isfinite(at) ? at : 0.0f), 0, per_column - 1);
    int column = 0;
    if (columns == 2) {
        // The second column starts where the first one and the gap end.
        column = x >= rows.left + rows.column_w + rows.column_gap * 0.5f ? 1 : 0;
    }
    return std::clamp(column * per_column + line, 0, rows.count - 1);
}

void ReorderDrag::press(int place, float x, float y) {
    active_ = true;
    dragging_ = false;
    place_ = place;
    origin_x_ = x;
    origin_y_ = y;
}

std::optional<int> ReorderDrag::move(float x, float y, const Rows& rows, float threshold) {
    if (!active_) {
        return std::nullopt;
    }
    if (!dragging_) {
        if (std::hypot(x - origin_x_, y - origin_y_) <= threshold) {
            return std::nullopt;
        }
        dragging_ = true;
    }
    const int now = place_at(rows, x, y);
    if (now == place_) {
        return std::nullopt;
    }
    place_ = now;
    return now;
}

bool ReorderDrag::release(bool cancelled) {
    const bool moved = dragging_ && !cancelled;
    active_ = false;
    dragging_ = false;
    return moved;
}

} // namespace etude
