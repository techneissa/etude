// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet.hpp"

#include "confirm_arm.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "item_probe.hpp"
#include "row_layout.hpp"
#include "settings_catalogue.hpp"
#include "colours_layout.hpp"
#include "sheet_layout.hpp"

#include <imgui_internal.h> // a window's content height

#include <algorithm>
#include <cmath>

namespace etude::app::ui {

void SettingsSheet::open_at(Page p) {
    nav_ = Nav{};
    nav_.page = p;
    last_ = parent_of(p);
    reveal_rail_row_ = true;
    raise_ = true;
    learn_colours_sheet_opened(); // the View page's Learn colours tab starts on Scales again
}

namespace {

// Brings a scrolling rail to rest on a row edge once the finger or the wheel lets go: a wheel
// notch moves it one row, a drag and its coast end on the nearest row edge, eased so it never
// jumps. Called inside the rail's child window after `drag_scroll`.
void settle_rail_impl(Kit& k, const RailLayout& rail, float& to, bool& easing) {
    const ImGuiIO& io = ImGui::GetIO();
    const float max_scroll = rail.content_h - rail.view_h; // not ImGui's: it is 0 on the first frame
    if (io.MouseWheel != 0.0f && ImGui::IsWindowHovered()) {
        if (!easing) {
            to = rail_rest(ImGui::GetScrollY(), rail.row_h, max_scroll);
        }
        to = rail_rest(to + (io.MouseWheel < 0.0f ? rail.row_h : -rail.row_h), rail.row_h, max_scroll);
        easing = true;
    }
    if (ImGui::IsMouseDown(0) || k.scroll.busy()) {
        easing = false; // a finger or its coast has the rail
        return;
    }
    if (!easing) {
        to = rail_rest(ImGui::GetScrollY(), rail.row_h, max_scroll);
    }
    const float now = ImGui::GetScrollY();
    if (std::fabs(to - now) < 0.5f) {
        ImGui::SetScrollY(to);
        easing = false;
        return;
    }
    easing = true;
    ImGui::SetScrollY(now + (to - now) * std::min(1.0f, k.dt / 0.08f)); // a short ease out
}

// The height page `p` needs, measured against the cap's body `body`, after the current window drew it: what it drew,
// except for the Effects page, which fills its body and lays itself out to the height
// its arrangement needs (colours_layout).
float page_height(Page p, ImVec2 body, float u) {
    if (p == Page::Colours) {
        return colours_layout(body.x, body.y, u).height;
    }
    const ImGuiWindow* w = ImGui::GetCurrentWindowRead();
    return std::max(0.0f, w->DC.CursorMaxPos.y - w->DC.CursorStartPos.y);
}

// A thin cue at the right edge of the page being drawn, where its view is in its content, when it
// overflows. Call it last, inside the page's window.
void scroll_cue(const Kit& k) {
    const float max_y = ImGui::GetScrollMaxY();
    if (max_y <= 0.0f) {
        return;
    }
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float width = k.s(tok::kS1);
    const float grab = std::max(k.s(32.0f), size.y * size.y / (size.y + max_y));
    const float top = pos.y + (size.y - grab) * (ImGui::GetScrollY() / max_y);
    ImGui::GetWindowDrawList()->AddRectFilled({pos.x + size.x - width, top},
                                              {pos.x + size.x, top + grab}, u32({1, 1, 1, 0.16f}),
                                              width * 0.5f);
}

// A wide rail row: the page's icon, then its name beside it in the row `a`..`b`.
void rail_row_wide(const Kit& k, ImDrawList* rdl, Page p, bool on, ImVec2 a, ImVec2 b,
                   ImVec4 ink) {
    glyph(k, rdl, page_icon(p), {a.x + k.s(22.0f), (a.y + b.y) * 0.5f}, k.s(tok::kIcon), ink);
    // Never cut: the rail is as wide as the longest name needs, and a name that did not fit would
    // end in an ellipsis, which the audit reports.
    ImFont* const face = on ? k.bold : k.regular;
    const float text_x = a.x + k.s(44.0f);
    const FitText name = fit(face, k.s(tok::kBody), page_title(p), b.x - text_x);
    label_at(k, rdl, face, k.s(tok::kBody), {text_x, a.y, b.x, b.y}, LabelAlign::Left,
             on ? col::kText : col::kText2, name.c_str());
}

// A soft glow on the end of a scrolling rail that has more rows past it: a faint light strip over the
// first or last row's blank edge, lighter than the rail and clear of the ink. `view` is the rail's
// visible box.
void rail_glow(const Kit& k, ImDrawList* dl, const RailLayout& rail, float row_h, const Box& view) {
    const float max_scroll = rail.content_h - rail.view_h;
    const RailFade fade = rail_fade(true, ImGui::GetScrollY(), max_scroll, row_h, k.reduced_motion);
    const float fh = k.s(kRailFadeH);
    const ImU32 clear = u32({1, 1, 1, 0.0f});
    if (fade.top > 0.0f) {
        const ImU32 s = u32({1, 1, 1, kRailFadeGlow * fade.top});
        dl->AddRectFilledMultiColor({view.x0, view.y0}, {view.x1, view.y0 + fh}, s, s, clear, clear);
    }
    if (fade.bottom > 0.0f) {
        const ImU32 s = u32({1, 1, 1, kRailFadeGlow * fade.bottom});
        dl->AddRectFilledMultiColor({view.x0, view.y1 - fh}, {view.x1, view.y1}, clear, clear, s, s);
    }
}

// While the sheet draws a page, the popups it raises (the info bubble) know they belong to Settings.
struct InSettings {
    Kit& k;
    explicit InSettings(Kit& kit) : k(kit) { k.in_settings = true; }
    ~InSettings() { k.in_settings = false; }
    InSettings(const InSettings&) = delete;
    InSettings& operator=(const InSettings&) = delete;
};

// The scrim dims what the sheet does not cover, from under the top row down. True on a tap on it.
bool draw_scrim(const Frame& f, bool raise) {
    bool tapped_scrim = false;
    if (begin_scrim("##sheet-scrim", {0.0f, f.row_max.y},
                    {f.display.x, f.display.y - f.row_max.y})) {
        // Over every other layer as the sheet opens (the song library, the results card, made
        // after the sheet, would otherwise stay in front of it); the sheet goes over it below.
        if (raise) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        }
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), {f.display.x, f.display.y},
                                                  u32(col::kScrim));
        ImGui::SetCursorPos({0, 0});
        // Closes on release without movement, so a swipe that starts outside the sheet does not.
        if (tapped(ImGui::InvisibleButton("##scrim", ImGui::GetWindowSize()))) {
            tapped_scrim = true;
        }
    }
    ImGui::End();
    return tapped_scrim;
}

} // namespace

void SettingsSheet::measure(Kit& k, UiModel& model, const Frame& f, bool shown) {
    heights_.at(f.display.x, f.display.y, k.u);
    // The settings checks compare what the pages draw; a page drawn to be measured stays out.
    if (column_probe::on() || catalogue_sweep::recording()) {
        return;
    }
    std::size_t open_index = kRail.size();
    for (std::size_t i = 0; i < kRail.size(); ++i) {
        if (shown && kRail[i] == nav_.page) {
            open_index = i; // the open page is measured where it is drawn
        }
    }
    const std::size_t next = heights_.next_stale(open_index);
    if (next == kRail.size()) {
        return;
    }
    const SheetFrame cap = sheet_frame(f.display.x, f.display.y, f.top, k.u);
    const RailLayout rail = rail_layout(cap.w, cap.h, static_cast<int>(kRail.size()), k.u);
    const PageArea area = page_area(cap, rail, k.u);
    // Laid out at the page's size just past the window's right edge: its clip is empty, so nothing
    // of it is drawn (nor the live preview rendered), and it takes no input.
    const audit::Unseen unseen;
    ImGui::SetNextWindowPos({f.display.x + area.w, cap.y + area.body_y});
    ImGui::SetNextWindowSize({area.w, area.body_h});
    if (ImGui::Begin("##sheet-measure", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::BeginDisabled(true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
        const InSettings in_settings(k);
        draw_page(k, kRail[next], model, area.columns, {area.w, area.body_h});
        ImGui::PopStyleVar();
        heights_.set(next, page_height(kRail[next], {area.w, area.body_h}, k.u), k.u);
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void SettingsSheet::stop_input() {
    block_input("##sheet-scrim");
    block_input("##sheet");
}

// An armed two-tap row does not outlive its page, nor the sheet being closed (confirm_arm.hpp).
void SettingsSheet::end_stale_arm(Kit& k) {
    const int frame_no = ImGui::GetFrameCount();
    if (!arm_kept(arm_page_, arm_frame_, static_cast<int>(nav_.page), frame_no)) {
        k.confirm_id = 0;
    }
    // A press anywhere but on the armed control puts it back (the tap on it fired last frame's).
    const bool clicked = ImGui::IsMouseClicked(0);
    if (!arm_kept_by_press(k.confirm_id, clicked, GImGui->HoveredIdPreviousFrame)) {
        k.confirm_id = 0;
    }
    arm_page_ = static_cast<int>(nav_.page);
    arm_frame_ = frame_no;
}

// The open page's height as it is drawn, grown or shrunk (a saved look, a microphone plugged in),
// once its switch has settled.
void SettingsSheet::note_open_height(const PageArea& cap_area, float u) {
    for (std::size_t i = 0; i < kRail.size(); ++i) {
        if (kRail[i] == nav_.page && nav_.t >= 1.0f) {
            heights_.set(i, page_height(nav_.page, {cap_area.w, cap_area.body_h}, u), u);
        }
    }
}

bool SettingsSheet::draw(Kit& k, UiModel& model, const Frame& f, float open) {
    nav_.update(k.dt);
    const SheetFrame cap_frame = sheet_frame(f.display.x, f.display.y, f.top, k.u);
    const SheetFrame frame =
        fit_sheet_height(cap_frame, heights_.tallest(k.u), static_cast<int>(kRail.size()), k.u);
    // The body every page is measured in: the cap's, so a page that fills its body (Effects) is
    // measured the same open or off screen, and the sheet's own height does not feed back into it.
    const PageArea cap_area =
        page_area(cap_frame, rail_layout(cap_frame.w, cap_frame.h, static_cast<int>(kRail.size()), k.u), k.u);
    const float w = frame.w;
    const float h = frame.h;
    const float x = frame.x;
    const float y = frame.y + (1.0f - open) * k.s(12.0f);
    bool close = false;

    // The scrim dims what the sheet does not cover, from under the top row down.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, open);
    if (draw_scrim(f, raise_)) {
        close = true;
    }

    if (begin_layer("##sheet", {x, y}, {w, h})) {
        const audit::SettingsScope audit_scope; // the audit reports what this sheet cuts
        if (raise_) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            raise_ = false;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        probe::add_rect("##sheet-card-rect", {x, y}, {x + w, y + h});
        sheet_card(k, dl, {x, y}, {x + w, y + h}, k.s(tok::kRadiusSheet), col::kSurface);
        // The rail: names beside icons when there is room, captions under icons when not. Every
        // row keeps its full height; when they do not all fit, the rail scrolls (sheet_layout.hpp).
        const RailLayout rail = rail_layout(w, h, static_cast<int>(kRail.size()), k.u);
        const bool wide = rail.wide;
        const float rail_w = rail.w;
        const float pad = rail.pad;
        const float row_h = rail.row_h;
        dl->AddRectFilled({x, y}, {x + rail_w, y + h}, u32({1, 1, 1, 0.025f}), k.s(tok::kRadiusSheet),
                          ImDrawFlags_RoundCornersLeft);
        dl->AddLine({x + rail_w, y + k.s(tok::kS3)}, {x + rail_w, y + h - k.s(tok::kS3)},
                    u32(col::kHairline));
        // Its own child window, a pad (and what whole rows leave over) inside the sheet's top and
        // bottom, clear of the sheet's rounded corners. A scrolling rail shows whole rows only.
        ImGui::SetCursorScreenPos({x, y + pad + rail.lead});
        ImGui::BeginChild(
            "##rail", {rail_w, rail.view_h}, ImGuiChildFlags_None,
            // The wheel moves the rail a row at a time (rail_settle), not ImGui's own scroll.
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse |
                (rail.scrolls ? 0 : ImGuiWindowFlags_NoScrollbar));
        ImDrawList* rdl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float view_top = y + pad + rail.lead;
        const float view_bottom = view_top + rail.view_h;
        const bool revealed = reveal_rail_row_ && rail.scrolls; // ImGui applies it next frame
        for (std::size_t i = 0; i < kRail.size(); ++i) {
            const Page p = kRail[i];
            const bool on = parent_of(nav_.page) == p;
            const ImVec2 a{origin.x + pad, origin.y + row_h * static_cast<float>(i)};
            if (on && reveal_rail_row_ && rail.scrolls) {
                // The chosen page's row in the middle of the rail, as far as the ends allow.
                ImGui::SetScrollY(rail_scroll_to(static_cast<int>(i), static_cast<int>(kRail.size()),
                                                 rail.rows_in_view, row_h));
            }
            // The row's hit area is the row less the dead gap to the next one, centred.
            const float rail_gap = k.s(tok::kHitGap);
            ImGui::SetCursorScreenPos({a.x, a.y + rail_gap * 0.5f});
            ImGui::PushID(static_cast<int>(i));
            const bool pressed =
                ImGui::InvisibleButton("##rail", {rail_w - 2.0f * pad, row_h - rail_gap});
            const State st = item_state(k, on);
            ImGui::PopID();
            const ImVec2 b{a.x + rail_w - 2.0f * pad, a.y + row_h};
            const Box rail_hit{a.x, a.y + rail_gap * 0.5f, b.x, b.y - rail_gap * 0.5f};
            // The row's face, a little shorter than its hit so the selected rows do not touch.
            draw_state(k, rdl, page_title(p),
                       {rail_hit.x0, rail_hit.y0 + k.s(kRailFaceInset), rail_hit.x1,
                        rail_hit.y1 - k.s(kRailFaceInset)},
                       rail_hit, st);
            const ImVec4 ink = state_ink(st, col::kText2);
            if (wide) {
                rail_row_wide(k, rdl, p, on, a, b, ink);
            } else {
                const RailCaption cap = rail_caption(a.y, row_h, k.u);
                if (a.y >= view_top && b.y <= view_bottom) { // a row the rail cuts is not measured
                    audit::note(page_title(p), a, b, InkKind::Centred,
                                false); // above a caption: x only
                }
                glyph(k, rdl, page_icon(p), {(a.x + b.x) * 0.5f, cap.icon_cy},
                      k.s(tok::kIcon), ink);
                const char* label = page_caption(p);
                const float line_h = text_size(k.regular, cap.caption_size, label).y;
                label_at(k, rdl, k.regular, cap.caption_size,
                         {a.x, cap.caption_y, b.x, cap.caption_y + line_h}, LabelAlign::Centre,
                         on ? col::kText : col::kText3, label);
            }
            if (pressed && nav_.page != p) {
                nav_.go(p, 0.0f);
                last_ = p;
            }
        }
        reveal_rail_row_ = false;
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy({rail_w, rail.content_h});
        if (rail.scrolls) {
            drag_scroll(k);
            if (!revealed) {
                settle_rail_impl(k, rail, rail_to_, rail_easing_);
            }
        }
        if (rail.scrolls) {
            rail_glow(k, rdl, rail, row_h, {x, view_top, x + rail_w, view_bottom});
        }
        ImGui::EndChild();
        // The page.
        const PageArea area = page_area(frame, rail, k.u);
        const float cx = area.x;
        const float cw = area.w;
        ImGui::SetCursorPos({cx, area.head_y});
        // A toast keeps off the header and the rail; the card is where it may stand over the
        // paused, dimmed notes.
        k.toast_avoid({x + cx, y + area.head_y, cw + k.s(tok::kS1), area.head_h});
        k.toast_avoid({x, y, rail_w, h});
        k.toast_regions.surface = {x, y, w, h};
        ImGui::BeginChild("##sheet-head", {cw + k.s(tok::kS1), area.head_h}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        // The one close button is the gear's X in the top row; a tap on the dimmed screen closes
        // too.
        if (nav_.page == Page::Configs) {
            configs_page_cover_audit();
        }
        const int hd = header(k, page_title(nav_.page), is_subpage(nav_.page), false);
        // The open and switch lift moves the page down a little: only its window moves and
        // shortens (`lifted_body`); the header and the page lay out against the unlifted body, so
        // a page that nearly fits does not flip between whole and split during the slide.
        const float lift = (1.0f - ease_out(nav_.t)) * k.s(6.0f);
        const LiftedBody body = lifted_body(area, nav_.dir == 0.0f ? lift : 0.0f);
        draw_page_header(k, nav_.page, {cw, body.layout_h}, area.columns);
        ImGui::EndChild();
        if (hd == 1) {
            nav_.go(parent_of(nav_.page), -1.0f);
        }
        ImGui::SetCursorPos({cx, area.body_y});
        ImGui::SetCursorPosY(body.top);
        {
            const ImVec2 cur = ImGui::GetCursorPos();
            ImGui::SetCursorPos({cur.x + nav_.offset(k.s(28.0f)), cur.y});
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * nav_.alpha());
            // No scrollbar of its own: one takes width from the rows once the page overflows, a
            // frame after it opens. `scroll_cue` shows where the view is instead.
            ImGui::BeginChild("##sheet-page", {cw, body.window_h}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
            const InSettings in_settings(k);
            end_stale_arm(k);
            const Page next = draw_page(k, nav_.page, model, area.columns, {cw, body.layout_h});
            note_open_height(cap_area, k.u);
            if (next != nav_.page) {
                nav_.go(next, 1.0f);
            }
            drag_scroll(k);
            scroll_cue(k);
            if (open >= 1.0f && nav_.t >= 1.0f) {
                audit::page_scroll(page_title(nav_.page), ImGui::GetScrollMaxY());
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (close) {
        stop_input();
    }
    return close;
}

} // namespace etude::app::ui
