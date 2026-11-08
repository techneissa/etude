// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_screen.hpp"

#include "bar.hpp"
#include "crash_layout.hpp"
#include "crash_screen_layout.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "item_probe.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>

namespace etude::app::ui {

CrashScreenTap CrashScreenUi::draw(Kit& k, const CrashScreenView& view, const CrashView* report,
                                   ImVec2 display) {
    CrashScreenTap tap;
    const int step = tok::wide_step(display.x);
    const CrashScreenSizes z = crash_screen_sizes(step);
    const float body = k.s(z.body_pt);
    const float title_size = k.s(z.title_pt);
    const float col_w = crash_screen_column_w(display.x, k.u, step);
    // The words wrap to the column: never cut, never an ellipsis.
    if (lines_w_ != col_w || lines_size_ != body || lines_from_ != view.detail) {
        lines_ = wrap_report(view.detail, col_w, [&](const std::string& s) {
            return text_size(k.regular, body, s.c_str()).x;
        });
        lines_w_ = col_w;
        lines_size_ = body;
        lines_from_ = view.detail;
    }
    const std::vector<std::string> title_lines = wrap_report(view.title, col_w, [&](const std::string& s) {
        return text_size(k.bold, title_size, s.c_str()).x;
    });
    const std::size_t count = std::min<std::size_t>(view.buttons.size(), 2);
    std::array<float, 2> widths{};
    for (std::size_t i = 0; i < count; ++i) {
        widths[i] = crash_button_w(k, crash_button_words(view.buttons[i]), z.button_pt, z.grow);
    }
    const CrashScreenGeom g = crash_screen_geometry(display.x, display.y, k.u, static_cast<int>(lines_.size()),
                                                    static_cast<int>(count), widths, step,
                                                    static_cast<int>(title_lines.size()));
    const bool sheet = card_.report_open();
    if (begin_layer("##crash-screen", {0.0f, 0.0f}, {display.x, display.y})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({0.0f, 0.0f}, {display.x, display.y}, u32(col::kScreenBg));
        // With the report's sheet open the page under it is not drawn at all: the sheet covers it, and
        // nothing hidden is measured or pressed.
        if (sheet && report != nullptr) {
            ImGui::End();
            tap.report = card_.draw_report_sheet(k, *report, display, 0.0f);
            return tap;
        }
        // The badge: the warning on a tile in the warning colour.
        const float face = g.badge.w;
        const ImVec2 a{g.badge.x, g.badge.y};
        dl->AddRectFilled(a, {a.x + face, a.y + face}, u32(with_alpha(col::kWarning, tok::kSelectedFill)),
                          k.s(tok::kRadiusCard));
        dl->AddRect(a, {a.x + face, a.y + face}, u32(with_alpha(col::kWarning, tok::kSelectedLine)),
                    k.s(tok::kRadiusCard), 0, k.s(tok::kHairline));
        glyph(k, dl, icon::kWarning, {a.x + face * 0.5f, a.y + face * 0.5f}, k.s(z.icon), col::kWarning);
        const float title_h = g.title.h / static_cast<float>(std::max<std::size_t>(title_lines.size(), 1));
        for (std::size_t i = 0; i < title_lines.size(); ++i) {
            const float y = g.title.y + static_cast<float>(i) * title_h;
            label_at(k, dl, k.bold, title_size, {g.title.x, y, g.title.right(), y + title_h}, LabelAlign::Centre,
                     col::kText, title_lines[i].c_str());
        }
        const float line_h = body * crash_screen_tok::kLineRatio;
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const float y = g.detail.y + static_cast<float>(i) * line_h;
            label_at(k, dl, k.regular, body, {g.detail.x, y, g.detail.right(), y + line_h}, LabelAlign::Centre,
                     col::kText2, lines_[i].c_str());
        }
        for (std::size_t i = 0; i < count; ++i) {
            const CrashButton which = view.buttons[i];
            const Rect& r = g.button[i];
            ChipOpts o = crash_button_opts(k, z.button_pt, z.grow, r.w);
            o.on = i == 0;
            o.live = !sheet;
            if (chip(k, crash_button_id(which), crash_button_words(which), {r.x, r.y}, o)) {
                tap.button = which;
            }
        }
        probe::add_rect("##crash-screen-rect", {g.column.x, g.column.y}, {g.column.right(), g.column.bottom()});
    }
    ImGui::End();
    if (report != nullptr) {
        tap.report = card_.draw_report_sheet(k, *report, display, 0.0f);
    } else {
        card_.close_report();
    }
    return tap;
}

} // namespace etude::app::ui
