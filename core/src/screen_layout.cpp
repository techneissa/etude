// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/screen_layout.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

constexpr float kMaxWindowPx = 100000.0f;
constexpr float kMaxPxPerMm = 40.0f;

// Preferred text sizes, in millimeters of a physical screen, with a floor in pixels so a sparse
// display still gets readable text.
constexpr float kChordFontMm = 8.0f;
constexpr float kChordFontHeightShare = 0.09f; // of the window height, at size 1.0
// The chord label's names are a share of that: small and calm (the setting's 1.0 is this size, not
// the old banner's).
constexpr float kChordLabelShare = 0.46f;
// The key bed (fallboard strip above the keys): its height, the felt line on its lower edge and
// the shadow it casts on the keys, as shares of the keyboard height with floors in pixels.
constexpr float kKeyBedShare = 0.07f;
constexpr float kKeyBedMaxShare = 0.20f;
constexpr float kFeltLineShareOfBed = 0.20f;
constexpr float kKeyShadowShare = 0.08f;
constexpr float kNoteLabelFontMm = 4.5f;
constexpr float kNoteLabelFontFloorPx = 11.0f;
// Note labels may use this much of the keyboard height (key labels a little less, they sit on the
// key face next to the key's edge).
constexpr float kBlockLabelKeyboardShare = 0.30f;
constexpr float kKeyLabelKeyboardShare = 0.22f;

float whole_pixels(float v) {
    if (!std::isfinite(v) || v <= 0.0f) {
        return 0.0f;
    }
    return std::floor(std::min(v, kMaxWindowPx));
}

RectF row(float y, float h, float width) { return RectF{0.0f, y, h > 0.0f ? width : 0.0f, h}; }

} // namespace

namespace {

float fit_font(float max_font_px, float cell_width_px, int char_count, float min_font_px) {
    if (char_count <= 0 || !std::isfinite(max_font_px) || !std::isfinite(cell_width_px) ||
        max_font_px <= 0.0f || cell_width_px <= 0.0f) {
        return 0.0f;
    }
    const float room = cell_width_px - 2.0f * kLabelPaddingPx;
    const float fits = room / (static_cast<float>(char_count) * kLabelCharWidthEm);
    const float font = std::floor(std::min(max_font_px, fits));
    return font >= min_font_px ? font : 0.0f;
}

LabelPlacement place_label(const RectF& block, const RectF& clip, float max_font_px, int char_count,
                           float min_font_px) {
    const float values[] = {block.x, block.y, block.w, block.h, clip.x, clip.y, clip.w, clip.h};
    for (float v : values) {
        if (!std::isfinite(v)) {
            return {};
        }
    }
    const float x0 = std::max(block.x, clip.x);
    const float y0 = std::max(block.y, clip.y);
    const float x1 = std::min(block.right(), clip.right());
    const float y1 = std::min(block.bottom(), clip.bottom());
    if (!(x1 > x0) || !(y1 > y0)) {
        return {};
    }
    const float font = fit_font(max_font_px, x1 - x0, char_count, min_font_px);
    if (font <= 0.0f) {
        return {};
    }
    const float text_w = static_cast<float>(char_count) * kLabelCharWidthEm * font;
    const float text_h = font * kBlockLabelLineHeightEm;
    if (y1 - y0 < text_h + 2.0f * kLabelPaddingPx) {
        return {};
    }
    LabelPlacement out;
    out.visible = true;
    out.font_px = font;
    out.rect =
        RectF{x0 + ((x1 - x0) - text_w) / 2.0f, y1 - kLabelPaddingPx - text_h, text_w, text_h};
    return out;
}

} // namespace

float fit_label_font_px(float max_font_px, float cell_width_px, int char_count) {
    return fit_font(max_font_px, cell_width_px, char_count, kMinLabelFontPx);
}

LabelPlacement place_block_label(const RectF& block, const RectF& clip, float max_font_px,
                                 int char_count) {
    return place_label(block, clip, max_font_px, char_count, kMinLabelFontPx);
}

float min_name_font_px(float px_per_mm) {
    if (!std::isfinite(px_per_mm) || px_per_mm <= 0.0f) {
        return kMinNameFontPx;
    }
    return std::round(kMinNameFontPx * std::min(px_per_mm, kMaxPxPerMm) / kDefaultPxPerMm);
}

float min_small_name_font_px(float px_per_mm) {
    if (!std::isfinite(px_per_mm) || px_per_mm <= 0.0f) {
        return kMinSmallNameFontPx;
    }
    return std::round(kMinSmallNameFontPx * std::min(px_per_mm, kMaxPxPerMm) / kDefaultPxPerMm);
}

float ui_window_grow(float width_px, float height_px, float px_per_mm) {
    const float density = std::isfinite(px_per_mm) && px_per_mm > 0.0f
                              ? std::min(px_per_mm, kMaxPxPerMm) / kDefaultPxPerMm
                              : 1.0f;
    const float w = std::isfinite(width_px) && width_px > 0.0f ? width_px / density : 0.0f;
    const float h = std::isfinite(height_px) && height_px > 0.0f ? height_px / density : 0.0f;
    const float grow = std::min(w / kUiDesignWidthPt, h / kUiDesignHeightPt);
    return std::clamp(std::isfinite(grow) ? grow : 1.0f, 1.0f, kUiMaxGrow);
}

namespace {
// The scale floor in pixels: `kUiMinScale` points at `pixel_density` pixels a point.
float min_scale_px(float pixel_density) {
    return kUiMinScale * (std::isfinite(pixel_density) && pixel_density > 0.0f ? pixel_density : 1.0f);
}
} // namespace

namespace {
// `grow` when the caller gave one, else the window's own.
float growth(float width_px, float height_px, float px_per_mm, float grow) {
    return std::isfinite(grow) && grow > 0.0f ? std::clamp(grow, 1.0f, kUiMaxGrow)
                                              : ui_window_grow(width_px, height_px, px_per_mm);
}
} // namespace

float ui_scale(float width_px, float height_px, float px_per_mm, float pixel_density, float grow) {
    const float density = std::isfinite(px_per_mm) && px_per_mm > 0.0f
                              ? std::min(px_per_mm, kMaxPxPerMm) / kDefaultPxPerMm
                              : 1.0f;
    return std::max(min_scale_px(pixel_density),
                    density * growth(width_px, height_px, px_per_mm, grow));
}

float bar_under_row_pt(bool has_song, bool slim) {
    if (!has_song) {
        return 0.0f;
    }
    return slim ? kSlimSectionBandPt : kSectionBandPt;
}

namespace {

// The top row's parts: the gear, the library's and Learn's buttons (one menu button on a narrow
// row), and the status text (with its font).
void place_top_row_parts(ScreenLayout& out, float top_h, float W, float unit) {
    // The gear: a square as tall as the row at its right end, the library's at its left end and
    // Learn's beside it.
    // The status text runs from a margin on the left to a gap before the gear.
    const float side = std::min(top_h, W);
    out.gear = RectF{W - side, 0.0f, side, top_h};
    const bool menu_row = W < kMenuBelowRowPt * unit;
    if (out.gear.x >= side) {
        (menu_row ? out.menu : out.library) = RectF{0.0f, 0.0f, side, top_h};
    }
    if (!menu_row && out.gear.x >= 2.0f * side) {
        out.learn = RectF{side, 0.0f, side, top_h};
    }
    const float margin = std::round(top_h * kTopRowMarginShare);
    const float text_w = out.gear.x - 2.0f * margin;
    if (text_w > 0.0f) {
        out.status_text = RectF{margin, 0.0f, text_w, top_h};
    }
    // The UI's body size, on the same scale as every control.
    const float font = std::min(std::max(kStatusFontPt * unit, kMinLabelFontPx),
                                std::floor(top_h / kTopRowMinHeightEm));
    if (font >= kMinLabelFontPx && !out.status_text.empty()) {
        out.status_visible = true;
        out.status_font_px = font;
    }
}

} // namespace

ScreenLayout compute_layout(float width_px, float height_px, float px_per_mm,
                            const LookSettings& look_in, const PinnedBar& bar,
                            float pixel_density, float grow_in) {
    const LookSettings look = look_in.clamped();
    const float W = whole_pixels(width_px);
    const float H = whole_pixels(height_px);
    const float ppmm = std::isfinite(px_per_mm) && px_per_mm > 0.0f
                           ? std::min(px_per_mm, kMaxPxPerMm)
                           : kDefaultPxPerMm;

    ScreenLayout out;
    if (W < 1.0f || H < 1.0f) {
        return out; // nothing to lay out in
    }

    // The notes always keep their share; everything else shares the rest, most important first.
    float budget = std::floor(H * (1.0f - kMinNoteAreaShare));
    // The UI's scale: design points to pixels, and how much the window grows the UI.
    const float unit = ui_scale(W, H, ppmm, pixel_density, grow_in);
    const float grow = growth(W, H, ppmm, grow_in);

    // 15 mm grown with the window, but never less than 15 mm at the scale floor, so the row keeps
    // the controls the kit draws at that floor.
    const float top_mm_px = std::max(kMinTouchTargetMm * ppmm * grow,
                                     kMinTouchTargetMm * min_scale_px(pixel_density) * kDefaultPxPerMm);
    const float top_h = std::min({std::ceil(top_mm_px), std::floor(H * kMaxTopRowShare), budget});
    budget -= top_h;

    float under_h = 0.0f;
    float lane_h = 0.0f;
    if (bar.pinned) {
        out.bar_fit = BarFit::Full;
    }
    if (bar.has_song && (bar.pinned || bar.lane)) {
        const auto band = [&](bool slim) { return std::ceil(bar_under_row_pt(true, slim) * unit); };
        // The keyboard it would have without the band; it may shrink to this share, no more.
        const float keyboard_now = std::min(std::floor(H * look.keyboard_height), budget);
        const float keyboard_min = std::floor(kMinPinnedKeyboardShare * keyboard_now);
        if (bar.lane) {
            const float lane_full = std::ceil(kLanePt * unit);
            const float lane_min = std::ceil(kLaneMinPt * unit);
            const float left = budget - band(false) - keyboard_min;
            if (lane_full <= left) {
                lane_h = lane_full;
                out.lane_fit = LaneFit::Full;
            } else if (lane_min <= left) {
                lane_h = std::floor(left);
                out.lane_fit = LaneFit::Short;
            } else {
                out.lane_fit = LaneFit::NoRoom;
            }
        }
        if (lane_h > 0.0f) {
            under_h = band(false); // the lane always has the full band over it
        } else if (bar.pinned) {
            if (band(false) + keyboard_min <= budget) {
                under_h = band(false);
            } else if (band(true) + keyboard_min <= budget) {
                under_h = band(true);
                out.bar_fit = BarFit::Slim;
            } else {
                out.bar_fit = BarFit::Fallback;
            }
        }
        budget -= under_h + lane_h;
    }

    const float keyboard_h = std::min(std::floor(H * look.keyboard_height), budget);
    budget -= keyboard_h;

    float chord_h = 0.0f;
    if (look.chord_label) {
        // Readable from the piano bench without being a banner: the larger of the physical size
        // and a share of the window height, a share of that (kChordLabelShare), scaled by the
        // setting.
        const float preferred = std::max(std::max(kChordFontMm * ppmm, kChordFontHeightShare * H) *
                                             kChordLabelShare * look.chord_label_size,
                                         kMinChordFontPx);
        const float strip_max = std::min(std::floor(H * kMaxChordLabelShare), budget);
        const float widest = W / (static_cast<float>(kChordLabelMaxChars) * kLabelCharWidthEm);
        const float font = std::floor(std::min({preferred, strip_max / kLineHeightEm, widest}));
        if (font >= kMinChordFontPx) {
            out.chord_label_visible = true;
            out.chord_label_font_px = font;
            chord_h = std::ceil(font * kLineHeightEm);
        }
    }

    const float note_h = H - top_h - under_h - lane_h - chord_h - keyboard_h;
    float y = 0.0f;
    out.top_row = row(y, top_h, W);
    y += top_h;
    out.bar_under = row(y, under_h, W);
    y += under_h;
    out.lane = row(y, lane_h, W);
    y += lane_h;
    out.chord_label = row(y, chord_h, W);
    y += chord_h;
    out.note_area = row(y, note_h, W);
    y += note_h;
    out.keyboard = row(y, keyboard_h, W);

    if (top_h > 0.0f) {
        place_top_row_parts(out, top_h, W, unit);
    }

    if (keyboard_h > 0.0f) {
        // The bed takes the top of the keyboard rect and the keys start exactly where it ends.
        const float bed_h = std::min(std::max(std::round(keyboard_h * kKeyBedShare), kMinKeyBedPx),
                                     std::floor(keyboard_h * kKeyBedMaxShare));
        out.key_bed = row(out.keyboard.y, bed_h, W);
        out.key_area = row(out.keyboard.y + bed_h, keyboard_h - bed_h, W);
        if (bed_h > 0.0f) {
            out.felt_line_h = std::max(1.0f, std::round(bed_h * kFeltLineShareOfBed));
        }
        out.key_shadow_h =
            std::min(std::max(2.0f, std::round(out.key_area.h * kKeyShadowShare)), out.key_area.h);
    }

    {
        const float preferred = std::max(kNoteLabelFontMm * ppmm, kNoteLabelFontFloorPx);
        const float block_font =
            std::floor(std::min(preferred, keyboard_h * kBlockLabelKeyboardShare));
        const float key_font = std::floor(std::min(preferred, keyboard_h * kKeyLabelKeyboardShare));
        if (look.shows_block_names() && block_font >= kMinLabelFontPx) {
            out.block_labels_visible = true;
            out.note_label_font_px = block_font;
        }
        if (look.shows_key_names() && key_font >= kMinLabelFontPx) {
            out.key_labels_visible = true;
            out.key_label_font_px = key_font;
        }
        if (look.c_markers && key_font >= kMinLabelFontPx) {
            out.c_markers_visible = true;
            out.c_marker_font_px = key_font;
        }
    }
    return out;
}

TopRowSplit split_top_row(const ScreenLayout& layout, float bar_w, float keep_status_w,
                          bool centred) {
    const RectF& area = layout.status_text;
    TopRowSplit out;
    out.status_text = area;
    if (area.empty() || !std::isfinite(bar_w) || bar_w <= 0.0f) {
        return out;
    }
    const float keep = std::isfinite(keep_status_w) ? std::max(0.0f, keep_status_w) : 0.0f;
    const float gap = std::round(layout.top_row.h * kTopRowMarginShare);
    const float w = std::min(std::floor(bar_w), area.w);
    const float window_w = layout.top_row.w;
    float x = centred ? std::floor((window_w - w) * 0.5f) : area.right() - w;
    if (keep > 0.0f) {
        x = std::max(x, area.x + keep + gap);
    }
    x = std::max(area.x, std::min(x, area.right() - w));
    out.bar = RectF{x, area.y, w, area.h};
    const float text_w = x - gap - area.x;
    out.status_text = text_w > 0.0f ? RectF{area.x, area.y, text_w, area.h} : RectF{};
    return out;
}

float bar_room(const ScreenLayout& layout, float keep_status_w) {
    const RectF& area = layout.status_text;
    if (area.empty()) {
        return 0.0f;
    }
    const float keep = std::isfinite(keep_status_w) ? std::max(0.0f, keep_status_w) : 0.0f;
    if (keep <= 0.0f) {
        return area.w;
    }
    const float gap = std::round(layout.top_row.h * kTopRowMarginShare);
    return std::max(0.0f, area.w - keep - gap);
}

HitBand scrubber_hit_band(float track_cy, float height, float row_bottom, float limit_bottom) {
    if (!std::isfinite(track_cy) || !std::isfinite(height) || !std::isfinite(row_bottom) ||
        !std::isfinite(limit_bottom) || !(height > 0.0f)) {
        return {std::isfinite(row_bottom) ? row_bottom : 0.0f,
                std::isfinite(row_bottom) ? row_bottom : 0.0f};
    }
    const float limit = std::max(limit_bottom, row_bottom);
    float top = track_cy - height * 0.5f;
    top = std::min(top, limit - height); // room below is short: move up first
    top = std::max(top, row_bottom);     // but never into the row
    return {top, std::min(top + height, limit)};
}

BarForm fit_bar_form(float comfy_room_w, float room_w, const BarFormWidths& widths) {
    if (!std::isfinite(room_w) || room_w < 0.0f) {
        return BarForm::Minimal;
    }
    const auto fits = [](float w, float room) {
        return std::isfinite(w) && w > 0.0f && std::isfinite(room) && w <= room;
    };
    const auto offered = [](float w) { return std::isfinite(w) && w > 0.0f; };
    if (fits(widths.full, comfy_room_w)) {
        return BarForm::Full;
    }
    if (fits(widths.compact, room_w)) {
        return BarForm::Compact;
    }
    if (fits(widths.tight, room_w)) {
        return BarForm::Tight;
    }
    if (fits(widths.minimal, room_w) || fits(widths.minimal_no_pin, room_w) ||
        !offered(widths.lean)) {
        return BarForm::Minimal;
    }
    if (fits(widths.lean, room_w) || !offered(widths.bare)) {
        return BarForm::Lean;
    }
    if (fits(widths.bare, room_w) || !offered(widths.least)) {
        return BarForm::Bare;
    }
    return BarForm::Least;
}

bool bar_keeps_pin(BarForm form, const BarFormWidths& widths, float room_w) {
    switch (form) {
    case BarForm::Full:
    case BarForm::Compact:
    case BarForm::Tight:
        return true;
    case BarForm::Minimal:
        break;
    case BarForm::Lean:
    case BarForm::Bare:
    case BarForm::Least:
        return false;
    }
    return std::isfinite(room_w) && std::isfinite(widths.minimal) && widths.minimal <= room_w;
}

} // namespace etude
