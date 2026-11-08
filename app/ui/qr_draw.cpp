// SPDX-License-Identifier: GPL-3.0-or-later
#include "qr_draw.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app::ui {

const QrGrid& qr_for(const std::string& url) {
    static std::string last;
    static QrGrid grid;
    if (url != last) {
        last = url;
        grid = make_qr(url);
    }
    return grid;
}

void draw_qr(const Kit& k, ImDrawList* dl, ImVec2 a, float side, const QrGrid& grid) {
    const float fb = std::max(k.fb_scale, 1.0f);
    dl->AddRectFilled(a, {a.x + side, a.y + side}, u32({1, 1, 1, 1}), k.s(tok::kRadiusInner));
    if (grid.empty()) {
        return;
    }
    const int modules = grid.size + 2 * phone_tok::kQuietModules;
    const float module_px = std::max(1.0f, std::floor(side * fb / static_cast<float>(modules)));
    const float code_px = module_px * static_cast<float>(grid.size);
    const float ox = std::round((a.x + side * 0.5f) * fb - code_px * 0.5f);
    const float oy = std::round((a.y + side * 0.5f) * fb - code_px * 0.5f);
    const ImU32 ink = u32({0, 0, 0, 1});
    for (int y = 0; y < grid.size; ++y) {
        for (int x = 0; x < grid.size; ++x) {
            if (!grid.at(x, y)) {
                continue;
            }
            int run = 1; // a row of dark modules is one rectangle
            while (grid.at(x + run, y)) {
                ++run;
            }
            const float px = ox + module_px * static_cast<float>(x);
            const float py = oy + module_px * static_cast<float>(y);
            dl->AddRectFilled(
                {px / fb, py / fb},
                {(px + module_px * static_cast<float>(run)) / fb, (py + module_px) / fb}, ink);
            x += run - 1;
        }
    }
}

} // namespace etude::app::ui
