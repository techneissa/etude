// SPDX-License-Identifier: GPL-3.0-or-later
// Where the top row's menu popover puts its parts, in ImGui points (no ImGui here, so
// tests/top_menu_layout_test.cpp checks the real numbers). The popover hangs under the top row from
// the menu button's left edge, with one row for each destination the narrow top row has no button
// for: Library, Learn and Profile, in that order. Every row is a hit area at least a hit tall and
// as wide as the card's inside; two rows are a dead gap apart.
#pragma once

#include "face_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace etude::app::ui {

namespace top_menu_tok {
inline constexpr int kMaxRows = 3;
} // namespace top_menu_tok

// The popover's entries, which are the places a tap opens, in the order they are listed.
enum class TopMenuEntry : unsigned char { Library, Learn, Profile };

// The entries a row holds: the library's and Learn's screens when the app has them, and the profile
// in use's switcher when the household is known.
struct TopMenuPlan {
    int count = 0;
    std::array<TopMenuEntry, top_menu_tok::kMaxRows> entry{};
};

inline TopMenuPlan top_menu_plan(bool library, bool learn, bool profile) {
    TopMenuPlan plan;
    const auto add = [&plan](bool wanted, TopMenuEntry e) {
        if (wanted) {
            plan.entry[static_cast<std::size_t>(plan.count++)] = e;
        }
    };
    add(library, TopMenuEntry::Library);
    add(learn, TopMenuEntry::Learn);
    add(profile, TopMenuEntry::Profile);
    return plan;
}

struct TopMenuLayout {
    Box card;
    int rows = 0;
    std::array<Box, top_menu_tok::kMaxRows> row{}; // the hit areas, top to bottom
};

// `rows` rows (1 to 3) under `top`, the card's left edge at `anchor_x`, each row wide enough for
// `word_w` points of words after its icon. The card is kept `kS2` inside the display on every side:
// a card too tall for the room under `top` moves up, never shrinks a row under a hit.
inline TopMenuLayout top_menu_layout(float display_w, float display_h, float top, float anchor_x,
                                     int rows, float word_w, float u) {
    TopMenuLayout lay;
    lay.rows = std::clamp(rows, 0, top_menu_tok::kMaxRows);
    const float pad = tok::kS1 * u;
    const float edge = tok::kS2 * u;
    const float hit = tok::kHit * u;
    const float dead = tok::kHitGap * u;
    const float row_w = std::max(
        hit, (tok::kS3 + tok::kIcon + tok::kS3 + tok::kS3) * u + word_w);
    const float w = row_w + 2.0f * pad;
    const float count = static_cast<float>(lay.rows);
    const float h = lay.rows > 0 ? count * hit + (count - 1.0f) * dead + 2.0f * pad : 0.0f;
    const float x = std::clamp(anchor_x, edge, std::max(edge, display_w - w - edge));
    const float y0 = std::max(edge, std::min(top, display_h - h - edge));
    lay.card = {x, y0, x + w, y0 + h};
    for (int i = 0; i < lay.rows; ++i) {
        const float y = y0 + pad + static_cast<float>(i) * (hit + dead);
        lay.row[static_cast<std::size_t>(i)] = {x + pad, y, x + pad + row_w, y + hit};
    }
    return lay;
}

} // namespace etude::app::ui
