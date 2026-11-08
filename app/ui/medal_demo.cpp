// SPDX-License-Identifier: GPL-3.0-or-later
#include "medal_demo.hpp"

#include "medal_draw.hpp"
#include "medal_emblem.hpp"

#include "etude/medals.hpp"

#include <cstdio>
#include <string>

namespace etude::app::ui {

namespace {


// The sheet's layout in design points, each named by its value (a developer's view, not a screen): 28 the small
// medal's disc and the title line, 56 twice that, 22 a heading or a line, 18 a caption line, the rest steps and sizes.
constexpr float kP2 = 2.0f;
constexpr float kP4 = 4.0f;
constexpr float kP6 = 6.0f;
constexpr float kP20 = 20.0f;
constexpr float kP22 = 22.0f;
constexpr float kP26 = 26.0f;
constexpr float kP28 = 28.0f;
constexpr float kP34 = 34.0f;
constexpr float kP40 = 40.0f;
constexpr float kP44 = 44.0f;
constexpr float kP46 = 46.0f;
constexpr float kP54 = 54.0f;
constexpr float kP56 = 56.0f;
constexpr float kP58 = 58.0f;
constexpr float kP62 = 62.0f;
constexpr float kP64 = 64.0f;
constexpr float kP66 = 66.0f;
constexpr float kP74 = 74.0f;
constexpr float kP84 = 84.0f;
constexpr float kP92 = 92.0f;
constexpr float kP100 = 100.0f;
constexpr float kP120 = 120.0f;

constexpr float kLargeAbove = 174.0f / 168.0f; // from a large medal's top to its disc's centre, over the disc
constexpr float kLargeH = 272.0f / 168.0f;

// The caption under a medal: its material and title, and what the title asks.
void tier_caption(Kit& k, ImDrawList* dl, const Box& cell, float y, int tier) {
    char line[64];
    std::snprintf(line, sizeof line, "%d \xC2\xB7 %s", tier, std::string(etude::tier_material_name(tier)).c_str());
    label_at(k, dl, k.bold, k.s(tok::kBody), {cell.x0, y, cell.x1, y + k.s(kP22)}, LabelAlign::Centre, col::kText, line);
    const std::string title(etude::tier_title(tier));
    label_at(k, dl, k.regular, k.s(tok::kCaption), {cell.x0, y + k.s(kP22), cell.x1, y + k.s(kP40)}, LabelAlign::Centre,
             col::kText2, title.c_str());
    std::snprintf(line, sizeof line, "from %d medals", etude::title_points(tier));
    label_at(k, dl, k.regular, k.s(tok::kCaption), {cell.x0, y + k.s(kP40), cell.x1, y + k.s(kP58)}, LabelAlign::Centre,
             col::kText3, line);
}

void heading(Kit& k, ImDrawList* dl, const Box& body, float y, const char* text) {
    label_at(k, dl, k.bold, k.s(tok::kLabel), {body.x0, y, body.x1, y + k.s(kP22)}, LabelAlign::Left, col::kText, text);
}

} // namespace

void MedalDemo::draw(Kit& k, const Frame& f) {
    if (mode_ == Mode::Closed) {
        return;
    }
    const ImVec2 from{0.0f, f.row_max.y};
    if (begin_layer("##medal-demo", from, {f.display.x, f.display.y - from.y})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(from, f.display, u32(col::kScreenBg));
        const float pad = k.s(tok::kS4);
        const Box body{from.x + pad, from.y + pad, f.display.x - pad, f.display.y - pad};
        if (mode_ == Mode::Sheet) {
            draw_sheet(k, dl, body);
        } else {
            draw_titles(k, dl, body);
        }
        ImGui::End();
    }
}

// The eight title medals large, and under them small at 28 pt and at twice that.
void MedalDemo::draw_titles(Kit& k, ImDrawList* dl, const Box& body) {
    label_at(k, dl, k.bold, k.s(tok::kTitle), {body.x0, body.y0, body.x1, body.y0 + k.s(kP28)}, LabelAlign::Left, col::kText,
             "The title medals: a piano emblem that builds up, tier by tier");
    const float cell_w = body.w() / 8.0f;
    const float d = std::min(cell_w * 0.72f, k.s(kP120));
    const float top = body.y0 + k.s(kP44);
    for (int t = 1; t <= 8; ++t) {
        const Box cell{body.x0 + cell_w * static_cast<float>(t - 1), top, body.x0 + cell_w * static_cast<float>(t), top + d * kLargeH};
        draw_title_medal(k, dl, t, {cell.cx(), top + d * kLargeAbove}, d, true);
        tier_caption(k, dl, cell, top + d * kLargeH + k.s(kP6), t);
    }
    const float row_y = top + d * kLargeH + k.s(kP84);
    heading(k, dl, body, row_y, "At 28 pt (the Household rows and the switcher), and twice that");
    const float small_d = k.s(kP28);
    for (int t = 1; t <= 8; ++t) {
        const float cx = body.x0 + cell_w * (static_cast<float>(t) - 0.5f);
        draw_title_medal_small(k, dl, t, {cx - k.s(kP34), row_y + k.s(kP54)}, small_d);
        draw_title_medal_small(k, dl, t, {cx + k.s(kP20), row_y + k.s(kP54)}, small_d * 2.0f);
    }
}

void MedalDemo::draw_sheet(Kit& k, ImDrawList* dl, const Box& body) {
    label_at(k, dl, k.bold, k.s(tok::kTitle), {body.x0, body.y0, body.x1, body.y0 + k.s(kP28)}, LabelAlign::Left, col::kText,
             "Medals: eight tiers, eight materials");
    label_at(k, dl, k.regular, k.s(tok::kCaption), {body.x0, body.y0 + k.s(kP28), body.x1, body.y0 + k.s(kP46)}, LabelAlign::Left,
             col::kText2, "Drawn by the app's own code: the medal once into a texture atlas, the emblem on its face at draw time.");
    const float cell_w = body.w() / 8.0f;
    float y = body.y0 + k.s(kP62);

    heading(k, dl, body, y, "1 \xC2\xB7 The title medals, large: a piano emblem for each tier");
    y += k.s(kP26);
    const float d1 = std::min(cell_w * 0.62f, k.s(kP92));
    for (int t = 1; t <= 8; ++t) {
        const Box cell{body.x0 + cell_w * static_cast<float>(t - 1), y, body.x0 + cell_w * static_cast<float>(t), y + d1 * kLargeH};
        draw_title_medal(k, dl, t, {cell.cx(), y + d1 * kLargeAbove}, d1, true);
        tier_caption(k, dl, cell, y + d1 * kLargeH + k.s(kP2), t);
    }
    y += d1 * kLargeH + k.s(kP66);

    heading(k, dl, body, y, "2 \xC2\xB7 One badge up the ladder (Streak: practise N days in a row)");
    y += k.s(kP26);
    const etude::AchievementInfo& streak = etude::achievement_info(etude::Achievement::Streak);
    const float d2 = std::min(cell_w * 0.5f, k.s(kP64));
    for (int t = 1; t <= 8; ++t) {
        const Box cell{body.x0 + cell_w * static_cast<float>(t - 1), y, body.x0 + cell_w * static_cast<float>(t), y + d2 * kLargeH};
        draw_medal(k, dl, etude::Achievement::Streak, t, {cell.cx(), y + d2 * kLargeAbove}, d2, true);
        const std::string words = etude::threshold_words(streak, streak.tiers[static_cast<std::size_t>(t - 1)]);
        label_at(k, dl, k.bold, k.s(tok::kLabel), {cell.x0, y + d2 * kLargeH + k.s(kP2), cell.x1, y + d2 * kLargeH + k.s(kP22)},
                 LabelAlign::Centre, col::kText, words.c_str());
    }
    y += d2 * kLargeH + k.s(kP34);

    heading(k, dl, body, y, "3 \xC2\xB7 Small, 28 pt (the Household rows and the switcher): the title medals, then the badge's");
    y += k.s(kP26);
    for (int t = 1; t <= 8; ++t) {
        const float cx = body.x0 + cell_w * (static_cast<float>(t) - 0.5f);
        draw_title_medal_small(k, dl, t, {cx - k.s(kP40), y + k.s(kP28)}, k.s(kP28));
        draw_title_medal_small(k, dl, t, {cx + k.s(kP4), y + k.s(kP28)}, k.s(kP56));
        draw_medal(k, dl, etude::Achievement::Streak, t, {cx + k.s(kP46), y + k.s(kP28)}, k.s(kP28), false);
    }
    y += k.s(kP74);

    heading(k, dl, body, y, "4 \xC2\xB7 Diamond and Obsidian side by side: large, and small at 28 pt and twice that");
    y += k.s(kP26);
    const float d4 = k.s(kP100);
    const float start = body.x0 + body.w() * 0.18f;
    for (int i = 0; i < 2; ++i) {
        const int t = 7 + i;
        const float x = start + static_cast<float>(i) * d4 * 3.0f;
        draw_title_medal(k, dl, t, {x + d4 * 0.65f, y + d4 * kLargeAbove}, d4, true);
        draw_title_medal_small(k, dl, t, {x + d4 * 1.75f, y + d4 * 0.65f}, k.s(kP28));
        draw_title_medal_small(k, dl, t, {x + d4 * 1.75f, y + d4 * 1.35f}, k.s(kP56));
        const std::string label = std::string(etude::tier_material_name(t)) + " \xC2\xB7 " + std::string(etude::tier_title(t));
        label_at(k, dl, k.bold, k.s(tok::kBody), {x, y + d4 * kLargeH + k.s(kP4), x + d4 * 2.2f, y + d4 * kLargeH + k.s(kP26)},
                 LabelAlign::Centre, col::kText, label.c_str());
    }
}

} // namespace etude::app::ui
