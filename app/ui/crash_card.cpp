// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_card.hpp"

#include "about_layout.hpp"
#include "bar.hpp"
#include "crash_layout.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "item_probe.hpp"
#include "qr_draw.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace etude::app::ui {

namespace {

constexpr const char* kTitle = "Etude closed unexpectedly";
constexpr const char* kDetail = "A report is saved on this device. Nothing is sent unless you send it.";

Box box_of(const Rect& r) { return {r.x, r.y, r.right(), r.bottom()}; }

// A chip whose hit area is `r` (the geometry is the hit area's, and the chip's own width is the
// rect's: it was measured from the same words).
bool chip_in(Kit& k, const char* id, std::string_view text, const Rect& r, ChipOpts o = {}) {
    o.width = r.w;
    return chip(k, id, text, {r.x, r.y}, o);
}

// `text` wrapped to `max_w` in `font` at `size`.
std::vector<std::string> wrapped(ImFont* font, float size, const char* text, float max_w) {
    return wrap_report(text, max_w, [&](const std::string& s) { return text_size(font, size, s.c_str()).x; });
}

const char* action_words(const CrashView& v) {
    return v.opens_browser ? "Open in browser" : "Show QR code";
}

ChipOpts action_opts(const CrashView& v) {
    ChipOpts o;
    o.icon = v.opens_browser ? nullptr : icon::kPhone;
    o.on = true;
    return o;
}

} // namespace

float crash_button_w(const Kit& k, std::string_view words, float label_pt, float grow) {
    if (grow <= 1.0f) {
        return chip_width(k, nullptr, words);
    }
    const float words_w = text_size(k.bold, k.s(label_pt), std::string(words).c_str()).x;
    return std::ceil(words_w + 2.0f * k.s(tok::kS5) + k.s(tok::kHairline));
}

ChipOpts crash_button_opts(const Kit& k, float label_pt, float grow, float w) {
    ChipOpts o;
    o.width = w;
    if (grow > 1.0f) {
        o.large = true;
        o.label_pt = label_pt;
        o.hit_h = k.s(tok::kHit) * grow;
        o.face_h = k.s(tok::kFace) * grow;
    }
    return o;
}

CrashChoice CrashCard::draw(Kit& k, const CrashView* view, const RectF& area, ImVec2 display,
                            float top) {
    want_ = view != nullptr;
    shown_ = approach(shown_, want_ ? 1.0f : 0.0f, k.dt, tok::kMove);
    if (!want_) {
        if (shown_ <= 0.0f) {
            stage_ = Stage::Notice;
        }
        return CrashChoice::None;
    }
    if (shown_ <= 0.0f) {
        return CrashChoice::None;
    }
    const float alpha = ease_out(shown_);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    const CrashChoice choice = stage_ == Stage::Notice ? draw_notice(k, area, display.x, alpha)
                                                       : draw_sheet(k, *view, display, top, alpha);
    ImGui::PopStyleVar();
    return choice;
}

CrashChoice CrashCard::draw_report_sheet(Kit& k, const CrashView& v, ImVec2 display, float top) {
    want_ = report_open();
    if (!want_) {
        return CrashChoice::None;
    }
    shown_ = 1.0f;
    return draw_sheet(k, v, display, top, 1.0f);
}

CrashChoice CrashCard::draw_notice(Kit& k, const RectF& area, float display_w, float alpha) {
    const int step = tok::wide_step(display_w);
    const CrashNoticeSizes z = crash_notice_sizes(step);
    const float dismiss_w = crash_button_w(k, "Dismiss", z.button_pt, z.grow);
    const float send_w = crash_button_w(k, "Send report", z.button_pt, z.grow);
    // The words wrap to the room beside the badge: never cut, never an ellipsis.
    const float text_w = crash_notice_text_w(area.w, k.u, step);
    const float title_size = k.s(z.title_pt);
    const float detail_size = k.s(z.detail_pt);
    const std::vector<std::string> title_lines = wrapped(k.bold, title_size, kTitle, text_w);
    const std::vector<std::string> detail_lines = wrapped(k.regular, detail_size, kDetail, text_w);
    const CrashNoticeGeom g = crash_notice_geometry(area.x, area.y, area.w, area.h, k.u, dismiss_w, send_w,
                                                    step, static_cast<int>(title_lines.size()),
                                                    static_cast<int>(detail_lines.size()));
    CrashChoice choice = CrashChoice::None;
    const float lift = (1.0f - alpha) * -k.s(tok::kS2);
    if (begin_layer("##crash-notice", {g.card.x, g.card.y + lift}, {g.card.w, g.card.h})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const float dy = wp.y - g.card.y;
        const auto lifted = [&](Rect r) {
            r.y += dy;
            return r;
        };
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        // The badge: the warning on a tile in the warning colour.
        const Rect badge = lifted(g.badge);
        const float face = k.s(z.tile);
        const ImVec2 a{badge.x + (badge.w - face) * 0.5f, badge.y + (badge.h - face) * 0.5f};
        dl->AddRectFilled(a, {a.x + face, a.y + face},
                          u32(with_alpha(col::kWarning, tok::kSelectedFill)), k.s(tok::kRadiusControl));
        dl->AddRect(a, {a.x + face, a.y + face}, u32(with_alpha(col::kWarning, tok::kSelectedLine)),
                    k.s(tok::kRadiusControl), 0, k.s(tok::kHairline));
        glyph(k, dl, icon::kWarning, {badge.x + badge.w * 0.5f, badge.y + badge.h * 0.5f},
              k.s(z.icon), col::kWarning);

        const auto lines = [&](const Rect& r, const std::vector<std::string>& text, ImFont* font,
                               float size, ImVec4 colour) {
            const Rect at = lifted(r);
            const float line_h = at.h / static_cast<float>(std::max<std::size_t>(text.size(), 1));
            for (std::size_t i = 0; i < text.size(); ++i) {
                const float y = at.y + static_cast<float>(i) * line_h;
                label_at(k, dl, font, size, {at.x, y, at.right(), y + line_h}, LabelAlign::Left, colour,
                         text[i].c_str());
            }
        };
        lines(g.title, title_lines, k.bold, title_size, col::kText);
        lines(g.detail, detail_lines, k.regular, detail_size, col::kText2);
        ChipOpts dismiss_o = crash_button_opts(k, z.button_pt, z.grow, g.dismiss.w);
        if (chip(k, "##crash_dismiss", "Dismiss", {g.dismiss.x, g.dismiss.y + dy}, dismiss_o)) {
            choice = CrashChoice::Dismiss;
        }
        ChipOpts send_o = crash_button_opts(k, z.button_pt, z.grow, g.send.w);
        send_o.on = true;
        if (chip(k, "##crash_send", "Send report", {g.send.x, g.send.y + dy}, send_o)) {
            stage_ = Stage::Report;
        }

        dl->ChannelsSetCurrent(0);
        k.toast_avoid({wp.x, wp.y, g.card.w, g.card.h});
        overlay_card(k, dl, wp, {wp.x + g.card.w, wp.y + g.card.h}, k.s(tok::kRadiusCard));
        dl->ChannelsMerge();
        probe::add_rect("##crash-notice-rect", wp, {wp.x + g.card.w, wp.y + g.card.h});
    }
    ImGui::End();
    return choice;
}

void CrashCard::draw_report_body(Kit& k, const CrashView& v, const Rect& body) {
    const float size = k.s(tok::kCaption);
    const float line_h = size * crash_tok::kLineRatio;
    if (lines_w_ != body.w || lines_size_ != size || lines_from_ != v.report.data() ||
        lines_len_ != v.report.size()) {
        lines_ = wrap_report(std::string(v.report), body.w - k.s(tok::kS3), [&](const std::string& s) {
            return text_size(k.regular, size, s.c_str()).x;
        });
        lines_w_ = body.w;
        lines_size_ = size;
        lines_from_ = v.report.data();
        lines_len_ = v.report.size();
    }
    ImGui::SetCursorScreenPos({body.x, body.y});
    ImGui::BeginChild("##crash-text", {body.w, body.h}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float scroll = ImGui::GetScrollY();
    const float total = static_cast<float>(lines_.size()) * line_h + k.s(tok::kS3);
    const std::size_t first = static_cast<std::size_t>(std::max(0.0f, std::floor(scroll / line_h)));
    const std::size_t last =
        std::min(lines_.size(), static_cast<std::size_t>(std::ceil((scroll + body.h) / line_h)) + 1);
    for (std::size_t i = first; i < last; ++i) {
        const float y = origin.y + static_cast<float>(i) * line_h - scroll;
        label_at(k, dl, k.regular, size, {origin.x, y, origin.x + body.w, y + line_h},
                 LabelAlign::Left, col::kText2, lines_[i].c_str());
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({body.w - k.s(tok::kS3), total});
    drag_scroll(k);
    ImGui::EndChild();
}

CrashChoice CrashCard::draw_sheet(Kit& k, const CrashView& v, ImVec2 display, float top, float) {
    const float action_w = chip_width(k, v.opens_browser ? nullptr : icon::kPhone, action_words(v));
    CrashSheetGeom g = crash_sheet_geometry(display.x, display.y, top, k.u, action_w);
    CrashChoice choice = CrashChoice::None;
    if (begin_scrim("##crash-scrim", {0.0f, top}, {display.x, display.y - top})) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), {display.x, display.y},
                                                  u32(col::kScrim));
        // The window takes the pointer over it: a tap on the dimmed screen reaches nothing below.
    }
    ImGui::End();
    if (begin_layer("##crash-sheet", {g.sheet.x, g.sheet.y}, {g.sheet.w, g.sheet.h})) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        probe::add_rect("##crash-sheet-rect", {g.sheet.x, g.sheet.y}, {g.sheet.right(), g.sheet.bottom()});
        sheet_card(k, dl, {g.sheet.x, g.sheet.y}, {g.sheet.right(), g.sheet.bottom()},
                   k.s(tok::kRadiusSheet), col::kSurface);
        const bool code = stage_ == Stage::Qr;
        const float hit = k.s(tok::kHit);
        ChipOpts back;
        back.icon = icon::kBack;
        back.width = g.back.w;
        if (chip(k, "##crash_back", "Back", {g.back.x, g.back.y + (g.back.h - hit) * 0.5f}, back)) {
            stage_ = code ? Stage::Report : Stage::Notice;
        }
        const Box title = box_of(g.title);
        label_at(k, dl, k.bold, k.s(tok::kTitle), title, LabelAlign::Left, col::kText,
                 fit(k.bold, k.s(tok::kTitle), code ? "Report this crash" : "Crash report", title.w()).c_str());
        if (!code) {
            ChipOpts act = action_opts(v);
            const Rect at{g.action.x, g.action.y + (g.action.h - hit) * 0.5f, g.action.w, hit};
            if (chip_in(k, "##crash_open", action_words(v), at, act)) {
                if (v.opens_browser) {
                    choice = CrashChoice::OpenUrl;
                } else {
                    stage_ = Stage::Qr;
                }
            }
            draw_report_body(k, v, g.body);
        } else {
            // The code, the line saying what it is and what to do with it, in the sheet's body.
            const AboutSheet s = about_sheet(g.body.w, g.body.h + g.back.h + k.s(tok::kS2), k.u);
            const ImVec2 o{g.body.x, g.back.y};
            const auto at = [&](const Rect& r) { return Box{o.x + r.x, o.y + r.y, o.x + r.right(), o.y + r.bottom()}; };
            const Box qr = at(s.qr);
            draw_qr(k, dl, {qr.x0, qr.y0}, qr.w(), qr_for(std::string(v.qr_url)));
            const LabelAlign align = s.side_by_side ? LabelAlign::Left : LabelAlign::Centre;
            const Box address = at(s.address);
            label_at(k, dl, k.bold, k.s(tok::kTitle), address, align, col::kText,
                     fit(k.bold, k.s(tok::kTitle), "Scan to report it on GitHub", address.w()).c_str());
            const Box hint = at(s.hint);
            label_at(k, dl, k.regular, k.s(tok::kBody), hint, align, col::kText2,
                     fit(k.regular, k.s(tok::kBody), "Your phone writes the issue. Nothing is sent from here.",
                         hint.w()).c_str());
        }
    }
    ImGui::End();
    return choice;
}

} // namespace etude::app::ui
