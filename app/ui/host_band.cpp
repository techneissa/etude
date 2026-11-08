// SPDX-License-Identifier: GPL-3.0-or-later
// The section band and its lane (Ui::draw's stages): where they sit under the top row, and
// drawing them.
#include "host_impl.hpp"

#include "editor.hpp"
#include "kit.hpp"
#include "section_band.hpp"

namespace etude::app {

// The section band: its own row while reserved (a pinned bar, or the lane open), else hanging
// under the revealed bar over the top of the notes; with the lane under it when that is open.
// Never while the settings or the full editor cover the screen.
void Ui::Impl::place_band(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    Frame& f = d.f;
    const float scale = d.scale;
    const bool band_reserved = d.band_reserved;
    const float hanging_band = d.hanging_band;
    BandPlace& band_place = d.band_place;
    d.band_shown = d.view.has_song && !d.modal && (band_reserved || f.reveal > 0.001f);
    const bool band_shown = d.band_shown;
    if (band_shown) {
        const float slide = band_reserved ? 0.0f : (1.0f - f.reveal) * -k.s(tok::kS2);
        band_place.band = band_reserved ? RectF{lay.bar_under.x * scale, lay.bar_under.y * scale,
                                                lay.bar_under.w * scale, lay.bar_under.h * scale}
                                        : RectF{0.0f, f.row_max.y + slide, f.display.x,
                                                hanging_band};
        band_place.alpha = band_reserved ? 1.0f : f.reveal;
        band_place.live = band_reserved || f.live;
        band_place.difficulty = d.model.app.band_difficulty;
        band_place.keyboard_top = f.keyboard_top;
        band_place.display_w = f.display.x;
        band_place.below = band_place.band.bottom();
    }
    d.lane_rect = band_shown && d.lane ? RectF{lay.lane.x * scale, lay.lane.y * scale,
                                               lay.lane.w * scale, lay.lane.h * scale}
                                       : RectF{};
    if (!d.lane_rect.empty()) {
        band_place.below = d.lane_rect.bottom();
    }
    f.band_bottom = band_shown ? band_place.below : f.row_max.y;
    im.hanging_band_bottom = band_shown && !band_reserved ? band_place.band.bottom() : 0.0f;
    // The editor docks as the lane while the band asks for it.
    if (im.band.lane() != im.editor->lane_open()) {
        if (im.band.lane()) {
            im.editor->open_lane(k);
        } else {
            im.editor->close_lane(k);
        }
    }
}

// The section band, and the lane under it while the editor docks as one.
void Ui::Impl::draw_band(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const Frame& f = d.f;
    const bool band_shown = d.band_shown;
    const RectF& lane_rect = d.lane_rect;
    // The replay's bar draws the band's row itself (draw_bar).
    const bool replay = d.info.replay != nullptr;
    if (band_shown && !replay) {
        im.band.draw(k, d.song, d.band_place);
        if (!lane_rect.empty() && im.editor->lane_open()) {
            // The magnifier may float anywhere under the top row while a handle is dragged.
            const RectF bubble{0.0f, f.row_max.y, f.display.x, f.keyboard_top - f.row_max.y};
            im.editor->draw_lane(k, lane_rect, bubble);
        }
    }
}

} // namespace etude::app
