// SPDX-License-Identifier: GPL-3.0-or-later
// What Ui::draw lays over the screen after the top row (its stages): the count-in and countdown,
// the beat pulse, the bar's handle, and the regions a toast keeps clear of.
#include "host_impl.hpp"

#include "bar.hpp"
#include "kit.hpp"
#include "tempo.hpp"
#include "toast_regions.hpp"
#include "transport.hpp"

#include <algorithm>
#include <cassert>

namespace etude::app {

// The count-in and countdown over the notes or beside the status text, the beat pulse, and the
// bar's handle.
void Ui::Impl::draw_overlays(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    const Frame& f = d.f;
    const SongView& view = d.view;
    const UiFrameInfo& info = d.info;
    const float scale = d.scale;
    const bool library_up = d.library_up;
    countdown_indicator(k, view, d.indicator, d.status_size);
    count_in_indicator(k, view, d.indicator, d.status_size);
    if (d.in_band && !im.open && !im.editor->is_open() && !library_up) {
        count_in_band(k, view, info.count_band, info.count_band_shown, scale);
    }
    if (!im.open && !im.editor->is_open() && !library_up && d.model.app.metronome_pulse) {
        // Below the reserved band and the lane too, so the pulse never sits on them.
        const float under_bottom = lay.lane.empty() ? lay.bar_under.bottom() : lay.lane.bottom();
        const float ring_top = std::max(f.top, under_bottom * scale + k.s(tok::kS2));
        beat_pulse_overlay(k, view, {0.0f, ring_top}, {f.display.x, f.keyboard_top});
    }
    // The bar hidden: a faint handle at the top edge says it is there, and where to swipe from.
    if (!d.modal && !d.pinned) {
        reveal_handle(k, {f.display.x * 0.5f, f.row_min.y}, 1.0f - f.reveal);
    }
}

namespace {

// The tallest pill the toast slot must hold: the song-opened toast's two lines, a warning's
// finger-wide icon, the Undo toast's taller pill; at the popup rule's sizes for the window.
float tallest_toast(const Kit& k, float display_w) {
    using ui::tok::PopupText;
    const int step = ui::tok::wide_step(display_w);
    const auto height = [&](ImFont* font, float pt, PopupText role) {
        return ui::text_size(font, k.s(ui::tok::popup_type(pt, step, role)), "Ag").y;
    };
    const float pill_pad = 2.0f * (k.s(ui::tok::kS2) + k.s(ui::tok::kS1) * 0.5f);
    const float line = height(k.regular, ui::tok::kLabel, PopupText::Text) + pill_pad;
    const float song = height(k.bold, ui::tok::kBody, PopupText::Text) +
                       height(k.regular, ui::tok::kLabel, PopupText::Detail) +
                       k.s(ui::tok::kToastLineGap) + pill_pad;
    return std::max({line, song, k.s(ui::tok::kHit) * ui::tok::popup_grow(step)});
}

} // namespace

// What a toast keeps clear of (core place_toast): the notes and the keyboard under the top row
// (the open sheet and editor, which add their own rects as they draw, are where it may stand
// over them), the bar's buttons and its section band while it shows, the lane, and the top
// row's status line, and the slot under all of those where it goes by default.
void Ui::Impl::place_toasts(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    const Frame& f = d.f;
    const UiFrameInfo& info = d.info;
    const float scale = d.scale;
    ToastRegionsInput in;
    in.display_w = f.display.x;
    in.display_h = f.display.y;
    in.row_top = f.row_min.y;
    in.row_bottom = f.row_max.y;
    in.bar_shown = f.reveal > 0.001f;
    in.bar = {f.bar_min.x, f.bar_min.y, f.bar_max.x - f.bar_min.x, f.bar_max.y - f.bar_min.y};
    in.has_song = d.view.has_song;
    in.slim = k.slim_band || !d.band_reserved; // a hanging band is slim
    in.pinned_band = {lay.bar_under.x * scale, lay.bar_under.y * scale, lay.bar_under.w * scale,
                      lay.bar_under.h * scale};
    in.lane = d.lane_rect;
    in.key_label = {info.key_label.x * scale, info.key_label.y * scale,
                    info.key_label.w * scale, info.key_label.h * scale};
    in.combo = {info.combo.x * scale, info.combo.y * scale, info.combo.w * scale,
                info.combo.h * scale};
    in.sheet_button = {info.sheet_button.x * scale, info.sheet_button.y * scale,
                       info.sheet_button.w * scale, info.sheet_button.h * scale};
    for (std::size_t i = 0; i < in.chords.size() && i < info.chord_labels.size(); ++i) {
        const RectF& c = info.chord_labels[i];
        in.chords[i] = {c.x * scale, c.y * scale, c.w * scale, c.h * scale};
    }
    in.toast_h = tallest_toast(k, f.display.x);
    in.gap = k.s(tok::kS2);
    in.u = k.u;
    const bool kept = fill_toast_regions(k.toast_regions, in);
    assert(kept && "ToastRegions::kMaxToastAvoid is too small");
    (void)kept;
    im.toast_slot = k.toast_regions.slot;
}

} // namespace etude::app
