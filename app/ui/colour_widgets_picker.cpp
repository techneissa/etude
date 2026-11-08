// SPDX-License-Identifier: GPL-3.0-or-later
#include "colour_widgets.hpp"

#include "colour_widgets_internal.hpp"

#include "colours_layout.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "preview.hpp"
#include "row_layout.hpp"
#include "settings_catalogue.hpp"

#include "etude/accidental_text.hpp"
#include "etude/palette.hpp"
#include "etude/slider_drag.hpp"
#include "etude/square_drag.hpp"
#include "etude/touch_gesture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace etude::app::ui {

namespace {

using colour_detail::kRingGap;
using colour_detail::kRingWidth;
using colour_detail::kFaceInset;
using colour_detail::kHeadingDrop;
using colour_detail::Picker;
using colour_detail::g_picker;
using colour_detail::g_recent;
using colour_detail::vec;
using colour_detail::cell_span;
using colour_detail::face;
using colour_detail::heading;
using colour_detail::note_name;
using colour_detail::swatch_labels;
using colour_detail::scheme_heading;
using colour_detail::scheme_chips;
using colour_detail::palette_rows;
using colour_detail::draw_swatch;
using colour_detail::swatch_grid;
using colour_detail::picker_block;
using colour_detail::quick_picks_block;
using colour_detail::hit_tiles;
using colour_detail::palette_base_name;
using colour_detail::press_rows;
using colour_detail::screen_rows;
using colour_detail::preview;
using colour_detail::gap;

constexpr float kBarTrackH = tok::kS5;          // the hue and lightness bars

constexpr float kHueStops = 6.0f;               // the hue bar's gradient: exact at each sixth

constexpr float kThumbR = tok::kS3;             // the square's thumb

constexpr float kThumbHeld = 1.25f;             // and its growth while it is held

constexpr float kQuickInset = tok::kS1 * 0.5f;  // a quick pick's tile inside its cell

constexpr float kQuickHeld = tok::kS1 * 1.25f;  // and while it is pressed

SquareDrag g_square;

// Puts `c` on the swatch being changed and into the recent colours.
void apply_colour(LookSettings& look, RGBA c) {
    set_swatch_color(look, look.color_scheme, g_picker.swatch, c);
    g_picker.pick = pick_keeping_hue(c, g_picker.pick);
    g_recent->add(c);
}

// The halo a held (or, for a mouse, hovered) thumb has: the kit's state helper on a face the size
// of a hit area round the thumb, kept inside `area`.
void thumb_halo(const Kit& k, ImDrawList* dl, const char* name, ImVec2 centre, ImVec2 area_a,
                ImVec2 area_b) {
    const float half = k.s(tok::kHit) * 0.5f;
    const ImVec2 a{std::max(area_a.x, centre.x - half), std::max(area_a.y, centre.y - half)};
    const ImVec2 b{std::min(area_b.x, centre.x + half), std::min(area_b.y, centre.y + half)};
    draw_state(k, dl, name, {a.x, a.y, b.x, b.y}, {area_a.x, area_a.y, area_b.x, area_b.y},
               item_state(k, false), Idle::None, half);
}

// A bar dragged along by a finger or a mouse, `a`..`b` the drawn track: the touch rules of the
// kit's sliders (core slider_drag.hpp, the touch slop of touch_gesture.hpp). The hit area is the
// whole row. Returns the new position 0..1 when it changes; `*ended` is set when the touch lifts.
// The thumb's halo is drawn here, the item being the one just made.
std::optional<float> bar_drag(Kit& k, ImDrawList* dl, const char* id, ImVec2 a, ImVec2 b,
                              float row_top, float row_h, float t, bool* ended) {
    const float r = (b.y - a.y) * 0.5f;
    ImGui::SetCursorScreenPos({a.x, row_top});
    ImGui::InvisibleButton(id, {b.x - a.x, row_h});
    const ImGuiIO& io = ImGui::GetIO();
    // The thumb's centre runs from one rounded end's centre to the other's.
    const SliderTrack track{a.x + r, b.x - r};
    std::optional<float> out;
    const auto apply = [&](std::optional<float> v) {
        if (v && *v != t) {
            out = *v;
            t = *v;
        }
    };
    if (ImGui::IsItemActivated()) {
        const SliderPointer who =
            io.MouseSource == ImGuiMouseSource_Mouse ? SliderPointer::Mouse : SliderPointer::Finger;
        apply(k.slider.press(who, io.MouseClickedPos[0].x, io.MouseClickedPos[0].y, track, t,
                             k.s(tok::kHit) * 0.5f));
    }
    if (ImGui::IsItemActive()) {
        apply(k.slider.move(io.MousePos.x, io.MousePos.y, track, k.s(kTouchSlopPt)));
        k.slider_active = k.slider.dragging();
    }
    if (ImGui::IsItemDeactivated()) {
        apply(k.slider.release(io.MousePos.x, track, io.MouseDown[0]));
        *ended = true;
    }
    thumb_halo(k, dl, id, {track.x0 + (track.x1 - track.x0) * t, (a.y + b.y) * 0.5f},
               {a.x, row_top}, {b.x, row_top + row_h});
    return out;
}

void bar_thumb(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float t, RGBA c, bool active) {
    const float r = (b.y - a.y) * 0.5f;
    const float x = a.x + r + (b.x - a.x - 2.0f * r) * t;
    const float cy = (a.y + b.y) * 0.5f;
    const float kr = active ? r * 1.15f : r;
    dl->AddCircleFilled({x, cy + 1.0f}, kr + 1.0f, u32({0, 0, 0, 0.3f}), 24);
    dl->AddCircleFilled({x, cy}, kr, u32({0.96f, 0.97f, 1.0f, 1.0f}), 24);
    dl->AddCircleFilled({x, cy}, kr - k.s(kRingWidth) * 1.5f, u32(vec(c)), 24);
}

// A rounded track filled with `stops` colours of `at(f)`, f 0..1, mixed between them.
template <class At>
void gradient_track(ImDrawList* dl, ImVec2 a, ImVec2 b, float stops, At at) {
    const float r = (b.y - a.y) * 0.5f;
    const float x0 = a.x + r;
    const float x1 = b.x - r;
    dl->AddCircleFilled({x0, a.y + r}, r, u32(vec(at(0.0f))), 24);
    dl->AddCircleFilled({x1, a.y + r}, r, u32(vec(at(1.0f))), 24);
    const int n = static_cast<int>(stops);
    for (int i = 0; i < n; ++i) {
        const float f0 = static_cast<float>(i) / stops;
        const float f1 = static_cast<float>(i + 1) / stops;
        const ImU32 c0 = u32(vec(at(f0)));
        const ImU32 c1 = u32(vec(at(f1)));
        dl->AddRectFilledMultiColor({x0 + (x1 - x0) * f0, a.y}, {x0 + (x1 - x0) * f1, b.y}, c0, c1,
                                    c1, c0);
    }
}

// The saturation and brightness square at full saturation on the hue: white to the hue across, then
// a clear-to-black wash down, two vertex-coloured quads (exactly HSV, smooth at any size, nothing
// allocated).
void square_gradient(ImDrawList* dl, ImVec2 a, ImVec2 b, double hue) {
    const ImU32 white = u32({1.0f, 1.0f, 1.0f, 1.0f});
    const ImU32 pure = u32(vec(from_hsv(hue, 1.0, 1.0)));
    const ImU32 clear = u32({0.0f, 0.0f, 0.0f, 0.0f});
    const ImU32 black = u32({0.0f, 0.0f, 0.0f, 1.0f});
    dl->AddRectFilledMultiColor(a, b, white, pure, pure, white);
    dl->AddRectFilledMultiColor(a, b, clear, clear, black, black);
    dl->AddRect(a, b, u32(col::kHairline), 0.0f, 0, 1.0f);
}

void square_thumb(const Kit& k, ImDrawList* dl, ImVec2 centre, RGBA c, bool held) {
    const float r = k.s(kThumbR) * (held ? kThumbHeld : 1.0f);
    dl->AddCircleFilled({centre.x, centre.y + 1.0f}, r + 1.0f, u32({0, 0, 0, 0.3f}), 28);
    dl->AddCircleFilled(centre, r, u32({0.96f, 0.97f, 1.0f, 1.0f}), 28);
    dl->AddCircleFilled(centre, r - k.s(kRingWidth) * 1.5f, u32(vec(c)), 28);
}

// One cell of the quick picks or the recent colours: a hit area with a tile of `c` in it that
// shrinks while pressed, a ring when `c` is the swatch's colour now. True on a tap.
bool colour_cell(const Kit& k, ImDrawList* dl, const char* id, int index, ImVec2 a, ImVec2 b,
                 RGBA c, RGBA current) {
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(index);
    const bool pressed = ImGui::InvisibleButton(id, {b.x - a.x, b.y - a.y});
    face(k, dl, id, a, b, a, b, false, Idle::None);
    const float inset = k.s(ImGui::IsItemActive() ? kQuickHeld : kQuickInset);
    ImGui::PopID();
    const ImVec2 ta{a.x + inset, a.y + inset};
    const ImVec2 tb{b.x - inset, b.y - inset};
    dl->AddRectFilled(ta, tb, u32(vec(c)), k.s(tok::kRadiusInner));
    dl->AddRect(ta, tb, u32(col::kHairline), k.s(tok::kRadiusInner), 0, 1.0f);
    if (c == current) {
        const float ring = k.s(kRingWidth);
        dl->AddRect({ta.x - ring, ta.y - ring}, {tb.x + ring, tb.y + ring}, u32(col::kText),
                    k.s(tok::kRadiusInner) + ring, 0, ring);
    }
    return tapped(pressed);
}

// The colour control's heading row: the swatch being changed, then Reset (that swatch back to its
// palette's colour) and Done. True when Done was tapped.
bool picker_heading(Kit& k, LookSettings& look, ImDrawList* dl, ImVec2 pos, float width,
                    RGBA current) {
    const float hit = k.s(tok::kHit);
    const ColorScheme scheme = look.color_scheme;
    const int index = g_picker.swatch;
    std::array<char, 16> inside{};
    std::array<char, 32> name{};
    swatch_labels(look, scheme, index, inside, name);
    const float r = k.s(tok::kS2) + k.s(tok::kS1) * 0.5f;
    draw_swatch(k, dl, {pos.x + k.s(tok::kS1) + r, pos.y + hit * 0.5f}, r, current, nullptr, false);
    const float tsize = k.s(tok::kBody);
    const float lsize = k.s(tok::kLabel);
    const float isize = k.s(tok::kIconSm);
    const auto button_w = [&](const char* label) {
        return std::max(hit, k.s(tok::kS3) * 2.0f + isize + k.s(tok::kS2) +
                                 text_size(k.regular, lsize, label).x);
    };
    // A heading button: its hit area `bw` wide ending at `right`, the icon and the word inside.
    const auto header_button = [&](const char* id, const char* label, const char* icon_glyph,
                                   float right, ImVec4 ink, ImVec4 word, Idle idle) {
        const float bw = button_w(label);
        const ImVec2 ba{right - bw, pos.y};
        ImGui::SetCursorScreenPos(ba);
        const bool pressed = ImGui::InvisibleButton(id, {bw, hit});
        face(k, dl, id, {ba.x, ba.y + k.s(kFaceInset)}, {ba.x + bw, ba.y + hit - k.s(kFaceInset)},
             ba, {ba.x + bw, ba.y + hit}, false, idle);
        const float ix = ba.x + k.s(tok::kS3) + isize * 0.5f;
        audit::note(id, {ix - isize * 0.5f, pos.y + (hit - isize) * 0.5f},
                    {ix + isize * 0.5f, pos.y + (hit + isize) * 0.5f});
        if (!audit::icons_hidden()) {
            glyph(k, dl, icon_glyph, {ix, pos.y + hit * 0.5f}, isize, ink);
        }
        label_at(k, dl, k.regular, lsize,
                 {ix + isize * 0.5f + k.s(tok::kS2), pos.y, ba.x + bw, pos.y + hit},
                 LabelAlign::Left, word, label);
        return std::pair{pressed, ba.x};
    };
    const auto [done_pressed, done_x] =
        header_button("##picker-done", "Done", icon::kCheck, pos.x + width, col::kAccent, col::kText,
                      Idle::Fill);
    // Pressable either way, like the page's Reset: the idle fill says it does something.
    const bool can_reset = !swatch_is_palette_colour(look, scheme, index);
    const ImVec4 reset_ink = can_reset ? col::kText : with_alpha(col::kText3, 0.6f);
    const auto [reset_pressed, reset_x] =
        header_button("##picker-reset", "Reset", icon::kReset, done_x - k.s(tok::kHitGap),
                      reset_ink, reset_ink, can_reset ? Idle::Fill : Idle::None);
    const float tx = pos.x + k.s(tok::kS1) + r * 2.0f + k.s(tok::kS2);
    const FitText title = fit(k.bold, tsize, name.data(), std::max(0.0f, reset_x - k.s(tok::kS1) - tx));
    label_at(k, dl, k.bold, tsize, {tx, pos.y, reset_x, pos.y + hit}, LabelAlign::Left, col::kText,
             title.c_str());
    if (tapped(reset_pressed)) {
        if (can_reset) {
            const std::string base = palette_base_name(look, scheme);
            reset_swatch(look, scheme, index);
            k.say(std::string(name.data()) + " is back to " + base);
        } else {
            k.say(std::string(name.data()) + " already has its " + palette_base_name(look, scheme) +
                  " colour");
        }
    }

    return tapped(done_pressed);
}

} // namespace

namespace colour_detail {

// The colour control: the swatch being changed with Done, the square and the hue bar.
void picker_block(Kit& k, LookSettings& look, float width) {
    catalogue_sweep::drawn(Setting::SchemeColours);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float hit = k.s(tok::kHit);
    const ColorScheme scheme = look.color_scheme;
    const int index = g_picker.swatch;
    // The colour may have changed under the control (a reset, a palette, a quick pick): follow it.
    const RGBA current = swatch_color(look, scheme, index);
    if (pick_color(g_picker.pick) != current) {
        g_picker.pick = pick_keeping_hue(current, g_picker.pick);
    }

    const bool done_tapped = picker_heading(k, look, dl, pos, width, current);

    // The square, then the hue bar under it. The hue bar is handled first, so the square is drawn
    // in the hue it has this frame.
    const float square_h = picker_square_height(width, k.u);
    const ImVec2 sa{pos.x, pos.y + hit + k.s(tok::kHitGap)}; // a dead gap under the heading row
    const ImVec2 sb{pos.x + width, sa.y + square_h};
    const float hue_top = sb.y + k.s(tok::kHitGap);
    const float track_h = k.s(kBarTrackH);
    const ImVec2 ha{pos.x, hue_top + (hit - track_h) * 0.5f};
    const ImVec2 hb{pos.x + width, ha.y + track_h};

    bool ended = false;
    gradient_track(dl, ha, hb, kHueStops, [](float f) {
        return from_hsv(static_cast<double>(f) * 360.0, 1.0, 1.0);
    });
    const auto hue = bar_drag(k, dl, "##hue", ha, hb, hue_top, hit,
                              static_cast<float>(hue_position(g_picker.pick)), &ended);
    const bool hue_active = ImGui::IsItemActive();
    if (hue) {
        g_picker.pick = with_hue_at(g_picker.pick, static_cast<double>(*hue));
    }

    square_gradient(dl, sa, sb, g_picker.pick.hue);
    ImGui::SetCursorScreenPos(sa);
    ImGui::InvisibleButton("##square", {width, square_h});
    const ImGuiIO& io = ImGui::GetIO();
    // The thumb's centre runs over the square less its radius, so it is never cut off at an edge
    // (as the bars' thumbs run from one rounded end's centre to the other's).
    const float rt = k.s(kThumbR);
    const SquareArea area{sa.x + rt, sa.y + rt, sb.x - rt, sb.y - rt};
    std::optional<SquarePos> moved;
    const auto apply = [&](std::optional<SquarePos> v) {
        if (v) {
            moved = v;
        }
    };
    if (ImGui::IsItemActivated()) {
        const SliderPointer who =
            io.MouseSource == ImGuiMouseSource_Mouse ? SliderPointer::Mouse : SliderPointer::Finger;
        apply(g_square.press(who, io.MouseClickedPos[0].x, io.MouseClickedPos[0].y, area,
                             {static_cast<float>(square_x(g_picker.pick)),
                              static_cast<float>(square_y(g_picker.pick))},
                             hit * 0.5f));
    }
    const bool square_active = ImGui::IsItemActive();
    if (square_active) {
        apply(g_square.move(io.MousePos.x, io.MousePos.y, area, k.s(kTouchSlopPt)));
        k.slider_active = k.slider_active || g_square.dragging();
    }
    if (ImGui::IsItemDeactivated()) {
        apply(g_square.release(io.MousePos.x, io.MousePos.y, area, io.MouseDown[0]));
        ended = true;
    }
    if (moved) {
        g_picker.pick = with_square_at(g_picker.pick, static_cast<double>(moved->x),
                                       static_cast<double>(moved->y));
    }
    const ImVec2 thumb{
        area.x0 + (area.x1 - area.x0) * static_cast<float>(square_x(g_picker.pick)),
        area.y0 + (area.y1 - area.y0) * static_cast<float>(square_y(g_picker.pick))};
    thumb_halo(k, dl, "##square", thumb, sa, sb);

    const RGBA picked = pick_color(g_picker.pick);
    if (picked != current) {
        set_swatch_color(look, scheme, index, picked);
        g_picker.changed = true;
    }
    square_thumb(k, dl, thumb, picked, square_active);
    bar_thumb(k, dl, ha, hb, static_cast<float>(hue_position(g_picker.pick)),
              from_hsv(g_picker.pick.hue, 1.0, 1.0), hue_active);
    if (ended && g_picker.changed) {
        g_recent->add(picked);
        g_picker.changed = false;
    }

    if (done_tapped) {
        g_picker.swatch = -1;
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, hue_top + hit - pos.y});
}

// The quick picks and the recent colours under them. The quick picks are a chart: the hues across
// in colour-wheel order, light, bright and deep down each column, the greys last. The recent colours
// are `kRecentRows` rows of the same cells, newest first; the cells not yet used are drawn faint.
// One tap puts a colour on the swatch being changed. Every cell is a hit area.
void quick_picks_block(Kit& k, LookSettings& look, float width) {
    catalogue_sweep::drawn(Setting::SchemeColours);
    heading(k, width, "Quick picks");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float hit = k.s(tok::kHit);
    const int cols = static_cast<int>(kQuickColumns);
    // A cell is its hit area and the dead gap; the last one's gap is outside the width.
    const float dead = k.s(tok::kHitGap);
    const float cw = (width + dead) / static_cast<float>(cols);
    const float pitch = hit + dead;
    const RGBA current = swatch_color(look, look.color_scheme, g_picker.swatch);
    const auto cell = [&](int row, int col) -> std::pair<ImVec2, ImVec2> {
        const ImVec2 a{pos.x + std::round(cw * static_cast<float>(col)),
                       pos.y + pitch * static_cast<float>(row)};
        return {a, {pos.x + std::round(cw * static_cast<float>(col + 1)) - dead, a.y + hit}};
    };
    std::optional<RGBA> chosen;
    const QuickPickGrid& picks = quick_picks();
    for (int row = 0; row < static_cast<int>(kQuickRows); ++row) {
        for (int col = 0; col < cols; ++col) {
            const auto [a, b] = cell(row, col);
            const RGBA c = picks[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
            if (colour_cell(k, dl, "##quick", row * cols + col, a, b, c, current)) {
                chosen = c;
            }
        }
    }
    ImGui::SetCursorScreenPos({pos.x, pos.y + pitch * static_cast<float>(kQuickRows) - dead});
    ImGui::Dummy({width, 0.0f});
    heading(k, width, "Recent", g_recent->empty() ? "the colours you pick show here" : nullptr);
    pos = ImGui::GetCursorScreenPos();
    const auto items = g_recent->items();
    for (int row = 0; row < kRecentRows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const auto i = static_cast<std::size_t>(row * cols + col);
            const auto [a, b] = cell(row, col);
            if (i < items.size()) {
                if (colour_cell(k, dl, "##recent", static_cast<int>(i), a, b, items[i], current)) {
                    chosen = items[i];
                }
            } else {
                const float inset = k.s(kQuickInset);
                dl->AddRect({a.x + inset, a.y + inset}, {b.x - inset, b.y - inset},
                            u32(with_alpha(col::kText3, 0.3f)), k.s(tok::kRadiusInner), 0, 1.0f);
            }
        }
    }
    if (chosen) {
        apply_colour(look, *chosen);
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, pitch * static_cast<float>(kRecentRows) - dead});
}

} // namespace colour_detail

} // namespace etude::app::ui
