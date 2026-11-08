// SPDX-License-Identifier: GPL-3.0-or-later
// When the bar shows and where it goes (Ui::draw's stages): the hover, tap and swipe that reveal
// it, the pin and the hold time that keep it, how far in it is, and the form it takes in the top
// row.
#include "host_impl.hpp"

#include "bar_metrics.hpp"
#include "kit.hpp"
#include "replay_bar_layout.hpp"
#include "tap.hpp"
#include "toast_regions.hpp"
#include "etude/bar_reveal.hpp"
#include "etude/screen_layout.hpp"
#include "etude/toast.hpp"
#include "etude/touch_gesture.hpp"

#include <algorithm>
#include <cfloat>

namespace etude::app {

namespace {

// The status text keeps at least this much of the row when the bar comes in (bar_metrics.hpp).
constexpr float kKeepStatus = bar_metrics::kKeepStatus;
constexpr float kKeepStatusCentred = bar_metrics::kKeepStatusCentred;
constexpr float kMinStatus = bar_metrics::kMinStatus;

// The point `m` (points) is in the layout's rectangle `r` (pixels).
bool in_rect(const RectF& r, ImVec2 m, float scale) {
    return m.x >= r.x * scale && m.x < r.right() * scale && m.y >= r.y * scale &&
           m.y < r.bottom() * scale;
}

} // namespace

// The bar shows on a hover over the top row (a mouse), a tap on it (a finger) or a downward
// swipe from the top edge, and hides a few seconds after the last use (longer after a finger,
// which has no hover to keep it open) or on an upward swipe; never while the settings are open.
// Pinned ("Keep the bar shown"), it stays.
void Ui::Impl::decide_bar(DrawFrame& d) {
    Impl& im = *this;
    const ImGuiIO& io = ImGui::GetIO();
    UiModel& model = d.model;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    Frame& f = d.f;
    const SongView& view = d.view;
    const float scale = d.scale;
    const ImVec2 gear_min = d.gear_min;
    const ImVec2 gear_max = d.gear_max;
    const bool library_button = d.library_button;
    const ImVec2 m = io.MousePos;
    const bool mouse_valid = m.x > -FLT_MAX * 0.5f && m.y > -FLT_MAX * 0.5f;
    const bool on_gear = box_has({gear_min.x, gear_min.y, gear_max.x, gear_max.y}, m);
    // While the bar shows, the section band under the top row (and the lane under it, when open)
    // counts as part of it. A band with no row of its own hangs under the revealed bar over the top
    // of the notes, in its slim form: one touch target tall, so it covers as little as it can.
    d.band_reserved = !lay.bar_under.empty();
    const bool band_reserved = d.band_reserved;
    d.hanging_band = band_height_pt(true, k.u);
    const float hanging_band = d.hanging_band;
    d.under_bar = !view.has_song  ? 0.0f
                  : band_reserved ? (lay.bar_under.h + lay.lane.h) * scale
                                  : hanging_band;
    const float reach = im.revealed ? d.under_bar : 0.0f;
    // The library's, Learn's and the menu's buttons are not part of the row that reveals the bar.
    const bool on_library = library_button && in_rect(lay.library, m, scale);
    const bool on_learn = d.learn_button && in_rect(lay.learn, m, scale);
    const bool on_profile = (d.profile_chip && box_has(d.profile_box, m)) ||
                            (d.menu_button && box_has(d.menu_box, m)); // the chip, or the menu button
    const bool in_row = mouse_valid && m.y >= f.row_min.y && m.y < f.row_max.y + reach &&
                        m.x >= 0.0f && m.x < f.display.x && !on_gear && !on_library && !on_learn &&
                        !on_profile && !im.scale_pill.contains(m);
    const bool hovering = in_row && !k.touch;
    d.library_up = (im.library.is_open() && im.library_port != nullptr) || im.learn.is_open();
    const bool modal = d.modal =
        im.open || im.editor->is_open() || d.library_up || im.crash.sheet_up();
    // Pinned, unless the window cannot hold the bar and its section band (core BarFit): then the
    // bar hides after a while as usual, the toast says so once, and the setting stays for a bigger
    // window.
    const bool fell_back = model.app.bar_pinned && lay.bar_fit == BarFit::Fallback;
    // The replay's bar is always shown (the app lays the screen out with the bar pinned).
    d.pinned = (model.app.bar_pinned && !fell_back) || d.info.replay != nullptr;
    const bool pinned = d.pinned;
    k.slim_band = lay.bar_fit == BarFit::Slim;
    k.sheet = {d.info.sheet.song, d.info.sheet.known, d.info.sheet.offered, d.info.sheet.reason};
    // The lane asked for but with no room at this size (core LaneFit): the full editor instead.
    if (im.band.lane() && view.has_song && lay.lane_fit == LaneFit::NoRoom) {
        im.band.set_lane(false);
        k.open_editor = true;
        k.say_free("No room for the lane at this size: the section editor opened");
    }
    // The lane keeps the bar shown, as the pin does.
    d.lane = im.band.lane() && !lay.lane.empty();
    if (fell_back && !im.fallback_told) {
        k.say_free("The bar can't stay shown at this size");
    }
    im.fallback_told = fell_back;
    // A warning that found no room for its words while the bar was shown hides the bar, as its
    // timeout does (a pinned bar too), so the status row is free for the whole text. The bar stays
    // down until that warning is gone; the pin setting stays on.
    if (bar_gives_way(k.toast_needs_room, im.revealed)) {
        im.revealed = false;
        im.bar->close_popovers();
        im.band.close_popovers();
        im.bar_down_for_toast = true;
    }
    if (!k.toast.state.active()) {
        im.bar_down_for_toast = false;
    }
    if (!modal && (pinned || (ImGui::IsMouseClicked(0) && in_row) || hovering ||
                   (im.revealed && in_row && ImGui::IsMouseDown(0)) || im.bar->holds_bar() ||
                   (view.has_song && im.band.holds_bar()) || im.want_band)) {
        use_bar(k);
    }
    im.want_band = false;
}

// The swipes, judged by core from the touches process_event fed the recogniser.
void Ui::Impl::swipe_bar(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const Frame& f = d.f;
    const bool modal = d.modal;
    const bool pinned = d.pinned;
    const bool lane = d.lane;
    GestureRecognizer::Config cfg;
    cfg.drag_threshold = kTouchSlopPt * k.u;
    cfg.long_press = false;
    im.swipe_input.set_config(cfg);
    BarSwipeArea area;
    area.bar_shown = im.revealed;
    area.row_bottom = f.row_max.y;
    area.bar_bottom = f.row_max.y + d.under_bar;
    area.unit = k.u;
    for (const GestureRecognizer::Event& e : im.swipe_input.events()) {
        const BarSwipe seen = im.swipe.on_event(e, area);
        if (modal || seen == BarSwipe::None) {
            continue;
        }
        // From the claim on the touch is the bar's: no tap on the button under the finger, no
        // scrub, long press or seek on the section band.
        ImGui::ClearActiveID();
        tap_cancel();
        im.band.cancel_touch();
        if (seen == BarSwipe::Reveal) {
            use_bar(k);
        } else if (seen == BarSwipe::Hide && !pinned && !lane) {
            im.revealed = false;
            im.bar->close_popovers();
            im.band.close_popovers();
        }
    }
    im.swipe_input.clear_events();
}

// How far in the bar and the settings sheet are, after the hold time has run out.
void Ui::Impl::ease_bar(DrawFrame& d) {
    Impl& im = *this;
    const Kit& k = d.k;
    Frame& f = d.f;
    d.at_keys = d.info.playing || d.view.playing;
    const double hold = bar_hold_s(im.last_use_touch, d.at_keys);
    if (d.modal || (im.revealed && !d.pinned && k.now - im.last_use > hold)) {
        im.revealed = false;
        im.bar->close_popovers();
        im.band.close_popovers();
    }
    im.reveal_p = approach(im.reveal_p, im.revealed ? 1.0f : 0.0f, k.dt, tok::kMove);
    f.reveal = ease_out(im.reveal_p);
    f.live = im.revealed && im.reveal_p > 0.6f;

    im.open_p = approach(im.open_p, im.open ? 1.0f : 0.0f, k.dt, tok::kSheet);
    d.open_ease = ease_out(im.open_p);
}

// Where the bar goes: the layout gives it the right end of the status area (or its middle),
// and the status text keeps the rest. Without room for the full bar it goes compact.
void Ui::Impl::place_bar(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    Frame& f = d.f;
    const SongView& view = d.view;
    const float scale = d.scale;
    const float status_w = d.status_w;
    TopRowSplit& split = d.split;
    split = TopRowSplit{lay.status_text, {}};
    if (f.reveal > 0.001f && d.info.replay != nullptr) {
        // The replay's controls, in the room the bar would have: the status text keeps its least.
        const float least_keep = std::max(std::min(status_w, k.s(kMinStatus)), d.status_lead);
        const float bar_w = replay_bar_width(k.u) / scale;
        split = split_top_row(lay, bar_w, least_keep / scale, false);
        f.bar_min = to_points(split.bar.x, split.bar.y, scale);
        f.bar_max = to_points(split.bar.right(), split.bar.bottom(), scale);
    } else if (f.reveal > 0.001f) {
        // The status text keeps a comfortable share of the row for the full bar and its least for
        // any other form; core picks the richest form that fits (fit_bar_form).
        // The scale pill is never squeezed out: the status area keeps the library's button and a
        // hit of it besides. The library button and the profile chip are never covered either: the
        // status area keeps them whatever the bar's form.
        const float pill_keep =
            d.pill_w > 0.0f ? d.status_lead + k.s(tok::kHit) + k.s(tok::kS2) : 0.0f;
        const float comfy_keep = std::max(
            std::min(status_w, std::max(k.s(im.bar->centred() ? kKeepStatusCentred : kKeepStatus),
                                        pill_keep)),
            d.status_lead);
        const float least_keep =
            std::max(std::min(status_w, std::max(k.s(kMinStatus), pill_keep)), d.status_lead);
        const BarFormWidths widths{im.bar->width(k, view, BarForm::Full) / scale,
                                   im.bar->width(k, view, BarForm::Compact) / scale,
                                   im.bar->width(k, view, BarForm::Tight) / scale,
                                   im.bar->width(k, view, BarForm::Minimal) / scale,
                                   im.bar->width(k, view, BarForm::Minimal, false) / scale,
                                   im.bar->width(k, view, BarForm::Lean, false) / scale,
                                   im.bar->width(k, view, BarForm::Bare, false) / scale,
                                   im.bar->width(k, view, BarForm::Least, false) / scale};
        const float least_room = bar_room(lay, least_keep / scale);
        f.form = fit_bar_form(bar_room(lay, comfy_keep / scale), least_room, widths);
        f.pin = bar_keeps_pin(f.form, widths, least_room);
        f.compact = f.form != BarForm::Full;
        const float keep_pt = f.form == BarForm::Full ? comfy_keep : least_keep;
        const float bar_w = im.bar->width(k, view, f.form, f.pin) / scale;
        split = split_top_row(lay, bar_w, keep_pt / scale, im.bar->centred());
        f.bar_min = to_points(split.bar.x, split.bar.y, scale);
        f.bar_max = to_points(split.bar.right(), split.bar.bottom(), scale);
    }
}

} // namespace etude::app
