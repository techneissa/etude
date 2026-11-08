// SPDX-License-Identifier: GPL-3.0-or-later
// The status text of the top row (Ui::draw's stage): the label and the notes measured once, then
// fitted to the room the bar and the count-in indicator leave, and drawn.
#include "host_impl.hpp"

#include "bar_metrics.hpp"
#include "kit.hpp"
#include "status_fit.hpp"
#include "etude/top_indicator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace etude::app {

namespace {

// What joins the notes of the status text: a middle dot inside a group (the song's line), wider
// blank space between groups.
constexpr const char* kStatusDotGap = " \xC2\xB7 ";
constexpr const char* kStatusGroupGap = "   ";
constexpr float kStatusMinCut = bar_metrics::kStatusMinCut;

// The status text ends before the indicator and the bar: the notes the room cannot hold go whole,
// lowest rank first (status_fit.hpp), never cut mid-word or run under either.
// `pill_w` > 0 leaves room after the label for the scale pill (0: none); returns where the pill
// goes (its left edge), or -1 when there is none.
float draw_status_text(const DrawFrame& d, float x, float right, float pill_w) {
    Kit& k = d.k;
    const Frame& f = d.f;
    const float status_size = d.status_size;
    const FitText& label_text = d.label_text;
    const std::span<const StatusPart> status_parts = d.status_parts;
    const StatusFitInput& status_in = d.status_in;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float cy = (f.row_min.y + f.row_max.y) * 0.5f;
    StatusFitInput in = status_in;
    const float gap = k.s(tok::kS4);
    const float reserve = pill_w > 0.0f ? pill_w + gap : 0.0f;
    in.room = right - x - 1.0f - reserve; // a pixel of slack for the measuring's rounding
    const StatusFit shown = fit_status(in);
    float pill_x = pill_w > 0.0f ? x : -1.0f;
    if (shown.label) {
        const FitText l = shown.label_w < status_in.label_w
                              ? fit(k.bold, status_size, label_text, shown.label_w)
                              : label_text;
        const ImVec2 ls = text_size(k.bold, status_size, l.c_str());
        const float ly = std::round(cy - ls.y * 0.5f);
        dl->AddText(k.bold, status_size, {x, ly}, ImGui::GetColorU32(with_alpha(col::kText, 0.82f)),
                    l.c_str());
        if (pill_w > 0.0f) {
            pill_x = x + ls.x + gap;
        }
        if (shown.shown != 0) {
            FitText notes;
            int last = -1;
            for (int i = 0; i < static_cast<int>(status_parts.size()); ++i) {
                if (!shown.shows(i)) {
                    continue;
                }
                const StatusPart& part = status_parts[static_cast<std::size_t>(i)];
                if (last >= 0) {
                    notes.append(part.group == status_parts[static_cast<std::size_t>(last)].group
                                     ? kStatusDotGap
                                     : kStatusGroupGap);
                }
                if (i == shown.cut) {
                    notes.append(fit(k.regular, status_size, part.text, shown.cut_w));
                } else {
                    notes.append(screen_text(part.text));
                }
                last = i;
            }
            const float nx = x + ls.x + gap + reserve;
            dl->AddText(k.regular, status_size, {nx, ly}, ImGui::GetColorU32(col::kText2),
                        notes.c_str());
        }
    }
    return pill_x;
}

} // namespace

// The status text: the label, then the notes, each a gap apart, at the layout's font size. The
// notes are parts with a drop rank (status_parts.hpp): measured here, fitted to the row later.
void Ui::Impl::measure_status(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    const UiFrameInfo& info = d.info;
    const float scale = d.scale;
    d.status_size = lay.status_font_px * scale;
    const float status_size = d.status_size;
    FitText& label_text = d.label_text;
    if (!label_text.assign(screen_text(info.status_label))) {
        label_text.end_with_ellipsis();
    }
    d.status_parts = std::span<const StatusPart>(info.status_parts.part.data(),
                                                 static_cast<std::size_t>(info.status_parts.count));
    const std::span<const StatusPart> status_parts = d.status_parts;
    std::array<float, kMaxStatusParts>& status_part_w = d.status_part_w;
    for (std::size_t i = 0; i < status_parts.size(); ++i) {
        FitText part;
        part.assign(screen_text(status_parts[i].text));
        status_part_w[i] = text_size(k.regular, status_size, part.c_str()).x;
    }
    StatusFitInput& status_in = d.status_in;
    status_in.label_w =
        label_text.empty() ? 0.0f : text_size(k.bold, status_size, label_text.c_str()).x;
    status_in.gap = k.s(tok::kS4);
    status_in.group_gap = text_size(k.regular, status_size, kStatusGroupGap).x;
    status_in.dot_gap = text_size(k.regular, status_size, kStatusDotGap).x;
    status_in.min_cut = k.s(kStatusMinCut);
    status_in.parts = status_parts;
    status_in.part_w = std::span<const float>(status_part_w.data(), status_parts.size());
    // The library's button sits at the start of the status area and the text after it: the part
    // the status area keeps when the bar comes in holds the button too, so the bar never covers it.
    d.library_button = im.library_port != nullptr && !lay.library.empty();
    d.learn_button = im.learn_port != nullptr && !lay.learn.empty();
    place_menu_button(d);
    const float buttons_right = d.learn_button      ? lay.learn.right()
                                : d.library_button ? lay.library.right()
                                : d.menu_button    ? lay.menu.right()
                                                    : 0.0f;
    d.status_lead = d.library_button || d.learn_button || d.menu_button
                        ? std::max(0.0f, (buttons_right - lay.status_text.x) * scale + k.s(tok::kS2))
                        : 0.0f;
    // The profile's chip follows the buttons, inside the same lead. A narrow row (one menu button)
    // has none: the menu holds the profile.
    d.profile_chip = (d.library_button || d.learn_button) && d.info.profiles != nullptr &&
                     d.info.profiles->set != nullptr;
    if (d.profile_chip) {
        const float x = buttons_right * scale + k.s(tok::kHitGap);
        const float w = im.profile.chip_hit_width(k, *d.info.profiles);
        // The hit area is the row's share of 12.2 mm, as the buttons' beside it, so a dead gap is
        // left to what is under the row.
        const float row_h = (lay.library.bottom() - lay.library.y) * scale;
        const float mid = (lay.library.y + lay.library.bottom()) * 0.5f * scale;
        d.profile_box = {x, mid - row_h * kTopRowHitShare * 0.5f, x + w,
                         mid + row_h * kTopRowHitShare * 0.5f};
        d.status_lead = std::max(d.status_lead, d.profile_box.x1 - lay.status_text.x * scale + k.s(tok::kS2));
    }
    // What the status text would take with every note whole: the bar leaves it at most this much.
    float status_w = d.status_lead;
    if (lay.status_visible) {
        status_w += status_in.label_w;
        if (!status_parts.empty()) {
            status_w += status_in.gap +
                        status_notes_width(status_in, all_status_parts(status_parts.size()));
        }
    }
    // The scale pill (free play's) follows the label: the bar leaves it room too.
    d.pill_w = scale_pill_wanted(d) ? im.scale_pill.width(k, d.model.app) : 0.0f;
    if (d.pill_w > 0.0f) {
        status_w += d.pill_w + k.s(tok::kS4);
    }
    d.status_w = status_w;
}

// The scale overlay's pill is free play's: not while a song is loaded, and not under anything that
// covers the screen.
bool Ui::Impl::scale_pill_wanted(const DrawFrame& d) const {
    return !d.view.has_song && d.lay.status_visible && !open && !editor->is_open() &&
           !(library.is_open() && library_port != nullptr) && d.info.replay == nullptr;
}

// The status area: it ends where the bar begins (or before the gear while the bar is hidden).
// Over the falling notes a count-in or the resume countdown shows large in the count band at
// their top (count_band.hpp). With no falling notes on screen (sheet music alone) the count-in's
// beat dots or the countdown's ring sit at the right end of that area, in the top row: never
// over the notes. With no room (a tiny window) nothing shows there.
void Ui::Impl::draw_status(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    const Frame& f = d.f;
    const SongView& view = d.view;
    const UiFrameInfo& info = d.info;
    const float scale = d.scale;
    const bool library_up = d.library_up;
    const float status_size = d.status_size;
    const FitText& label_text = d.label_text;
    const float status_x = lay.status_text.x * scale + d.status_lead;
    const float status_right = [&] {
        const float hidden_right = lay.status_text.right() * scale;
        const float shown_right = d.split.status_text.empty() ? lay.status_text.x * scale
                                                              : d.split.status_text.right() * scale;
        return hidden_right + (shown_right - hidden_right) * f.reveal;
    }();
    const bool in_band = info.count_band.visible();
    d.in_band = in_band;
    TopIndicator& indicator = d.indicator;
    if (!in_band && lay.status_visible && !im.open && !im.editor->is_open() && !library_up) {
        const int dots = view.countdown_left_s > 0.0 ? 0
                         : view.count_in_beat > 0
                             ? std::max(view.count_in_bar_beats, view.count_in_beat)
                             : -1;
        if (dots >= 0) {
            indicator = place_top_indicator(
                {status_x, f.row_min.y, status_right - status_x, f.row_max.y - f.row_min.y},
                status_size, dots);
        }
    }
    // Free play: the scale overlay's pill follows the label in the top row, and its popover hangs
    // under it; both wait out a song and anything that covers the screen. It stays inside the
    // status area, so it is never under the bar, the gear or the count-in's dots.
    const float pill_w = d.pill_w;
    const float room_end = indicator.visible() ? indicator.rect.x - k.s(tok::kS2) : status_right;
    float pill_x = status_x;
    if (lay.status_visible && !label_text.empty()) {
        pill_x = draw_status_text(d, status_x, room_end, pill_w);
    }
    const float at = std::max(status_x, std::min(pill_x, room_end - pill_w));
    if (pill_w <= 0.0f || !im.scale_pill.pill(k, d.model, f, at, room_end - at, true)) {
        im.scale_pill.close();
        im.scale_pill.forget();
    }
}

} // namespace etude::app
