// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/toast.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude {
namespace {

float finite_or_zero(float v) { return std::isfinite(v) ? v : 0.0f; }

} // namespace

float toast_alpha(double age_s, bool warning, bool reduced_effects, ToastAction action) {
    if (!std::isfinite(age_s)) {
        return 0.0f;
    }
    const double show = toast_show_s(warning, action);
    const double age = std::max(age_s, 0.0);
    if (age >= show) {
        return 0.0f;
    }
    if (reduced_effects) {
        return 1.0f;
    }
    const double fade_from = show - kToastFadeS;
    if (age <= fade_from) {
        return 1.0f;
    }
    return static_cast<float>(1.0 - (age - fade_from) / kToastFadeS);
}

void ToastState::put(std::string_view text, bool warning, TextSpan name, SongLines song) {
    constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    std::size_t n = text.size();
    bool cut = false;
    if (n > kMaxBytes) {
        // Too long: keep the start and end with an ellipsis, never inside a UTF-8 character.
        n = kMaxBytes - kEllipsis.size();
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) {
            --n;
        }
        cut = true;
    }
    std::copy_n(text.data(), n, text_);
    if (cut) {
        std::copy(kEllipsis.begin(), kEllipsis.end(), text_ + n);
        n += kEllipsis.size();
    }
    text_[n] = '\0';
    warning_ = warning;
    action_ = ToastAction::None; // a new toast offers nothing until it is asked to
    // The name as far as the kept text reaches (a cut keeps the start, then the ellipsis).
    const std::size_t kept = cut ? n - kEllipsis.size() : n;
    const std::size_t begin = std::min(name.begin, kept);
    name_ = {begin, std::min(name.size, kept - begin)};
    song_ = song.on;
    composer_ = song.on && song.composer && !song.detail.empty();
    std::size_t d = song.on ? song.detail.size() : 0;
    const bool detail_cut = d > kMaxDetailBytes;
    if (detail_cut) {
        d = kMaxDetailBytes - kEllipsis.size();
        while (d > 0 && (static_cast<unsigned char>(song.detail[d]) & 0xC0) == 0x80) {
            --d;
        }
    }
    std::copy_n(song.detail.data(), d, detail_);
    if (detail_cut) {
        std::copy(kEllipsis.begin(), kEllipsis.end(), detail_ + d);
        d += kEllipsis.size();
    }
    detail_[d] = '\0';
}

void ToastState::show(std::string_view text, double now_s, bool warning, TextSpan name,
                      SongLines song) {
    if (text.empty()) {
        return;
    }
    put(text, warning, name, song);
    start_s_ = now_s;
    queued_ = false;
}

void ToastState::queue(std::string_view text, bool warning, TextSpan name, SongLines song) {
    if (text.empty()) {
        return;
    }
    put(text, warning, name, song);
    queued_ = true;
}

void ToastState::begin_frame(double now_s) {
    if (queued_) {
        queued_ = false;
        start_s_ = now_s;
    }
}

float ToastState::alpha(double now_s, bool reduced_effects) {
    if (!active() || queued_) {
        return 0.0f;
    }
    const float a = toast_alpha(now_s - start_s_, warning_, reduced_effects, action_);
    if (a <= 0.0f) {
        text_[0] = '\0';
        action_ = ToastAction::None;
    }
    return a;
}

namespace {

// Touching is clear; a sliver this thin is rounding, not an overlap.
constexpr float kClearEps = 1e-3f;

bool usable(const RectF& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) &&
           !r.empty();
}

bool overlaps(const RectF& a, const RectF& b) {
    if (!usable(a) || !usable(b)) {
        return false;
    }
    return a.x < b.right() - kClearEps && b.x < a.right() - kClearEps &&
           a.y < b.bottom() - kClearEps && b.y < a.bottom() - kClearEps;
}

bool contains(const RectF& outer, const RectF& r) {
    return usable(outer) && r.x >= outer.x - kClearEps && r.y >= outer.y - kClearEps &&
           r.right() <= outer.right() + kClearEps && r.bottom() <= outer.bottom() + kClearEps;
}

struct Placer {
    float display_w;
    float display_h;
    const ToastRegions& regions;
    const RectF& anchor;
    bool anchored;

    bool clear(const RectF& box, bool over_notes = false) const {
        if (!contains(RectF{0.0f, 0.0f, display_w, display_h}, box)) {
            return false;
        }
        if (regions.invalid) {
            return false;
        }
        for (std::size_t i = 0; i < regions.avoid_count && i < kMaxToastAvoid; ++i) {
            const RectF& r = regions.avoid[i];
            if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) ||
                !std::isfinite(r.h) || overlaps(box, r)) {
                return false; // a rect that cannot be read keeps everything out
            }
        }
        if (anchored && overlaps(box, anchor)) {
            return false;
        }
        if (over_notes) {
            return true;
        }
        const RectF notes =
            usable(regions.notes) ? regions.notes : RectF{0.0f, 0.0f, display_w, display_h};
        return !overlaps(box, notes) || contains(regions.surface, box) || contains(regions.slot, box);
    }
};

// The last resort for a toast whose action (Undo) is the player's way back (`must_show`): it stands
// where nothing else is, over the falling notes if it must, still never over a header, the bar, a
// popover or the control it is for. The first clear place from the top of the notes down, centred on
// the anchor, else centred in the window, whole, else cut to `min_w`.
ToastPlacement place_over_notes(const Placer& placer, const RectF& anchor, bool anchored, float w,
                                float full_w, float h, float min_w, float gap) {
    const ToastRegions& regions = placer.regions;
    const float display_w = placer.display_w;
    const float display_h = placer.display_h;
    const auto side = [&](float width, float cx) {
        return std::clamp(cx - width * 0.5f, gap, std::max(std::max(display_w - gap, gap) - width, gap));
    };
    const float top = usable(regions.notes) ? regions.notes.y : 0.0f;
    const float step = std::max(gap, 1.0f);
    const float widths[2] = {full_w, min_w};
    for (int wi = 0; wi < 2; ++wi) {
        const float width = widths[wi];
        if (!(width > 0.0f) || (wi == 1 && min_w >= full_w)) {
            continue; // nothing to place, or the cut one is the whole one
        }
        const ToastForm form = width < finite_or_zero(w) ? ToastForm::Shortened : ToastForm::Full;
        const float centres[2] = {anchored ? anchor.x + anchor.w * 0.5f : display_w * 0.5f,
                                  display_w * 0.5f};
        for (float y = top; y + h <= display_h + kClearEps; y += step) {
            // Centred first, then anywhere along the row.
            for (const float cx : centres) {
                const RectF box{side(width, cx), y, width, h};
                if (placer.clear(box, true)) {
                    return ToastPlacement{box, ToastSpot::Over, form};
                }
            }
            for (float x = gap; x + width <= display_w - gap + kClearEps; x += step) {
                const RectF box{x, y, width, h};
                if (placer.clear(box, true)) {
                    return ToastPlacement{box, ToastSpot::Over, form};
                }
            }
        }
    }
    return ToastPlacement{};
}

} // namespace

ToastPlacement place_toast(float w, float h, float min_w, float icon, bool warning,
                           const RectF& anchor, float display_w, float display_h,
                           const ToastRegions& regions, float gap, bool must_show) {
    display_w = std::max(finite_or_zero(display_w), 0.0f);
    display_h = std::max(finite_or_zero(display_h), 0.0f);
    gap = std::max(finite_or_zero(gap), 0.0f);
    const float full_w = std::clamp(finite_or_zero(w), 0.0f, std::max(display_w - 2.0f * gap, 0.0f));
    h = std::max(finite_or_zero(h), 0.0f);
    min_w = std::clamp(finite_or_zero(min_w), 0.0f, full_w);
    icon = std::max(finite_or_zero(icon), h);

    const bool anchored = usable(anchor);
    const Placer placer{display_w, display_h, regions, anchor, anchored};
    ToastPlacement out;
    const auto take = [&](const RectF& box, ToastSpot spot, ToastForm form) {
        if (!placer.clear(box)) {
            return false;
        }
        out = ToastPlacement{box, spot, form};
        return true;
    };
    const float side_hi = std::max(display_w - gap, gap);
    const auto side = [&](float width, float cx) {
        return std::clamp(cx - width * 0.5f, gap, std::max(side_hi - width, gap));
    };
    // Under the anchor, then above it, for a pill `width` wide. A control near the edge of the open
    // sheet or editor, whose card is inset from the window, gets the pill moved in to stand
    // inside that card (it may stand over the notes only there), rather than lose it.
    const auto by_anchor = [&](float width, float height, ToastForm form) {
        if (!anchored) {
            return false;
        }
        const float cx = anchor.x + anchor.w * 0.5f;
        const float x = side(width, cx);
        if (take(RectF{x, anchor.bottom() + gap, width, height}, ToastSpot::Anchor, form) ||
            take(RectF{x, anchor.y - gap - height, width, height}, ToastSpot::Anchor, form)) {
            return true;
        }
        if (!usable(regions.surface) || width > regions.surface.w - 2.0f * gap + kClearEps) {
            return false;
        }
        // A gap of air from the card's edge, as the window's own edge gets.
        const float inner = std::clamp(cx - width * 0.5f, regions.surface.x + gap,
                                       std::max(regions.surface.right() - width - gap,
                                                regions.surface.x + gap));
        return inner != x &&
               (take(RectF{inner, anchor.bottom() + gap, width, height}, ToastSpot::Anchor, form) ||
                take(RectF{inner, anchor.y - gap - height, width, height}, ToastSpot::Anchor, form));
    };
    // In `slot`, `width` wide, centred.
    const auto in_slot = [&](const RectF& slot, float width, float height, ToastSpot spot, ToastForm form) {
        if (!usable(slot) || width > slot.w + kClearEps || height > slot.h + kClearEps) {
            return false;
        }
        const float x = slot.x + (slot.w - width) * 0.5f;
        return take(RectF{x, slot.y, width, height}, spot, form);
    };
    const ToastForm whole = full_w < finite_or_zero(w) ? ToastForm::Shortened : ToastForm::Full;
    // In `slot`, `width` wide, slid along it to the clear place nearest the middle: against the
    // slot's ends, or `gap` beside an avoid rect that blocks the middle. The pill keeps its row.
    const auto slid_in_slot = [&](const RectF& slot, float width, float height, ToastSpot spot,
                                  ToastForm form) {
        if (!usable(slot) || width > slot.w + kClearEps || height > slot.h + kClearEps) {
            return false;
        }
        const float middle = slot.x + (slot.w - width) * 0.5f;
        std::array<float, kMaxToastAvoid * 2> xs{};
        std::size_t n = 0;
        for (std::size_t i = 0; i < regions.avoid_count && i < kMaxToastAvoid; ++i) {
            const RectF& r = regions.avoid[i];
            if (regions.give_way[i] && usable(r) && r.y < slot.y + height && slot.y < r.bottom()) {
                xs[n++] = r.right() + gap;
                xs[n++] = r.x - gap - width;
            }
        }
        std::sort(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(n), [&](float a, float b) {
            return std::fabs(a - middle) < std::fabs(b - middle);
        });
        for (std::size_t i = 0; i < n; ++i) {
            const float x = std::clamp(xs[i], slot.x, slot.right() - width);
            if (take(RectF{x, slot.y, width, height}, spot, form)) {
                return true;
            }
        }
        return false;
    };

    if (by_anchor(full_w, h, whole) ||
        in_slot(regions.slot, full_w, h, ToastSpot::Slot, whole) ||
        in_slot(regions.strip, full_w, h, ToastSpot::Strip, whole) ||
        slid_in_slot(regions.slot, full_w, h, ToastSpot::Slot, whole)) {
        return out;
    }
    const auto cut_to = [&](const RectF& slot, ToastSpot spot) {
        const float width = std::min(full_w, slot.w);
        return width >= min_w && in_slot(slot, width, h, spot, ToastForm::Shortened);
    };
    if (cut_to(regions.slot, ToastSpot::Slot) || cut_to(regions.strip, ToastSpot::Strip)) {
        return out;
    }
    if (warning) {
        if (by_anchor(icon, icon, ToastForm::Icon) ||
            in_slot(regions.slot, icon, icon, ToastSpot::Slot, ToastForm::Icon) ||
            in_slot(regions.strip, icon, icon, ToastSpot::Strip, ToastForm::Icon)) {
            return out;
        }
    }
    if (must_show) {
        return place_over_notes(placer, anchor, anchored, w, full_w, h, min_w, gap);
    }
    return ToastPlacement{};
}

} // namespace etude
