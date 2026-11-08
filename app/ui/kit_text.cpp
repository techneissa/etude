// SPDX-License-Identifier: GPL-3.0-or-later
#include "kit.hpp"

#include "kit_internal.hpp"

#include "clef_icon.hpp"
#include "exact_shot.hpp"
#include "font_ink.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "pressed_fade.hpp"
#include "row_layout.hpp"
#include "sections_port.hpp"
#include "etude/accidental_text.hpp"
#include "etude/note_style.hpp"
#include "transport.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace etude::app::ui {

namespace {

FitCache& fit_cache() {
    static FitCache cache;
    return cache;
}

// The metrics of the labels drawn lately (ink_measure.hpp has the store and its key), so a screen of
// labels that did not change measures none of them again. Every answer is kept: measuring a label
// loads its glyphs, so the sides read below are final, and a letter the font lacks has none for
// good (`sides_known` says so).
LabelMetricsStore& metrics_store() {
    static LabelMetricsStore store;
    return store;
}

} // namespace

std::string_view screen_text(std::string_view text) {
    if (!has_accidental_spelling(text)) {
        return text;
    }
    static std::array<std::string, 8> ring;
    static std::size_t next = 0;
    std::string& out = ring[next++ % ring.size()];
    accidental_display_text_into(text, out);
    return out;
}

const char* screen_text(const char* text) {
    if (text == nullptr) {
        return text;
    }
    const std::string_view whole(text);
    const std::string_view out = screen_text(whole);
    return out.data() == whole.data() ? text : out.data();
}

ImVec2 text_size(ImFont* font, float size, const char* text) {
    if (!(size > 0.0f)) {
        return {0.0f, 0.0f}; // ImGui asserts on a size of 0 or NaN
    }
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, screen_text(text));
}

void text_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImVec4 colour,
             const char* text) {
    (void)k;
    dl->AddText(font, size, {std::round(pos.x), std::round(pos.y)}, u32(colour), screen_text(text));
}

LabelMetrics label_metrics(ImFont* font, float size, const char* text) {
    LabelMetrics m;
    if (!(size > 0.0f) || text == nullptr) {
        return m;
    }
    text = screen_text(text);
    const std::uint64_t key = label_metrics_key(font, size, text);
    if (const LabelMetrics* kept = metrics_store().find(key)) {
        return *kept;
    }
    const CapBand band = cap_band(font, size);
    m.cap_top = band.top;
    m.baseline = band.bottom;
    const float advance = text_size(font, size, text).x;
    m.ink_right = advance;
    // The first and last letters: their ink, not their advance.
    ImWchar first = 0;
    ImWchar last = 0;
    for (const char* p = text; *p != '\0';) {
        unsigned int cp = 0;
        const int n = ImTextCharFromUtf8(&cp, p, nullptr);
        if (n <= 0) {
            break;
        }
        if (p == text) {
            first = static_cast<ImWchar>(cp);
        }
        last = static_cast<ImWchar>(cp);
        p += n;
    }
    // Their sides are the glyph bitmaps' (rendered no wider than the letter, within a pixel), read
    // from the glyph table. In this ImGui a glyph loads on its first use, and `text_size` above has
    // used every letter of the label, so `FindGlyphNoFallback` answers null only for a letter the
    // font lacks (and always will): that side stays the advance's edge, and the answer is kept all
    // the same, flagged, so such a label is measured once, not each frame.
    ImFontBaked* baked = font->GetFontBaked(size);
    const float scale = size / baked->Size;
    if (const ImFontGlyph* g = first != 0 ? baked->FindGlyphNoFallback(first) : nullptr) {
        m.ink_left = g->X0 * scale;
    } else {
        m.sides_known = first == 0;
    }
    if (const ImFontGlyph* g = last != 0 ? baked->FindGlyphNoFallback(last) : nullptr) {
        m.ink_right = advance - g->AdvanceX * scale + g->X1 * scale;
    } else {
        m.sides_known = m.sides_known && last == 0;
    }
    metrics_store().put(key, m);
    return m;
}

ImVec2 label_pen(ImFont* font, float size, const Box& slot, LabelAlign align, const char* text) {
    const LabelPen pen =
        place_label(label_metrics(font, size, text), slot.x0, slot.x1, slot.cy(), align);
    // Whole points, like every text (`text_at`).
    return {std::round(pen.x), std::round(pen.y)};
}

namespace {
// The size `label_at` draws a label of `size` at: a whole size, so the baked glyphs are drawn as
// they are (not scaled) and the baseline falls on a pixel row: the label's middle is then within
// the half pixel the rounding of its pen leaves. Display sizes are left as they are (a half pixel
// of a size is lost in them, and the round letters' overshoot grows with the size).
float label_size(float size) {
    constexpr float kRoundBelow = 40.0f;
    return size < kRoundBelow ? std::max(1.0f, std::round(size)) : size;
}
} // namespace

// `text_size` measures `screen_text(text)`, the words `label_at` draws (F# is F♯): the width here is
// the drawn label's, accidentals included.
float label_width(ImFont* font, float size, const char* text) {
    return text_size(font, label_size(size), text).x;
}

void label_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
              LabelAlign align, ImVec4 colour, const char* text) {
    (void)k;
    if (text == nullptr || text[0] == '\0' || !(size > 0.0f)) {
        return;
    }
    size = label_size(size);
    text = screen_text(text);
    const ImVec2 at = label_pen(font, size, slot, align, text);
    if (audit::on()) {
        const ImVec2 ts = text_size(font, size, text);
        const LabelMetrics m = label_metrics(font, size, text);
        // Letters that hang below the baseline: the audit then reads the baseline from the rows.
        bool descenders = false;
        bool standing = false; // a letter or a digit, which stands on the baseline
        for (const char* p = text; *p != '\0'; ++p) {
            descenders = descenders || std::strchr("gjpqy,;()[]{}|Q_@$", *p) != nullptr;
            standing = standing || std::isalnum(static_cast<unsigned char>(*p)) != 0;
        }
        // The flat sign's bowl hangs a little below the baseline in the font (the sharp's does not).
        descenders = descenders || std::string_view(text).find(kFlatSign) != std::string_view::npos;
        audit::label(dl, text, {slot.x0, slot.y0}, {slot.x1, slot.y1}, {at.x - 1.0f, at.y},
                     {at.x + ts.x + 1.0f, at.y + ts.y}, {at.x + m.ink_left, at.y + m.cap_top},
                     {at.x + m.ink_right, at.y + m.baseline}, m.baseline - m.cap_top, align,
                     descenders, standing);
    }
    if (audit::labels_hidden()) {
        return;
    }
    dl->AddText(font, size, at, u32(colour), text);
}

void label_stack(const Kit& k, ImDrawList* dl, const Box& slot, const StackLine& top,
                 const StackLine* bottom, StackBands* bands, float extra_gap) {
    const float top_h = text_size(top.font, top.size, top.text).y;
    const LabelMetrics tm = label_metrics(top.font, top.size, top.text);
    const float top_cap = tm.baseline - tm.cap_top;
    if (bottom == nullptr) {
        if (bands != nullptr) {
            *bands = {slot.cy(), slot.cy()};
        }
        label_at(k, dl, top.font, top.size, {slot.x0, slot.cy() - top_h * 0.5f, slot.x1,
                                            slot.cy() + top_h * 0.5f},
                 LabelAlign::Left, top.colour, top.text);
        return;
    }
    // The two lines keep their line spacing (each label_at centres its cap band on its own slot's
    // middle, so the bands' middles are half a line box of each apart); the block, from the top
    // line's capitals to the bottom line's baseline, is centred on the slot.
    const float bottom_h = text_size(bottom->font, bottom->size, bottom->text).y;
    const LabelMetrics bm = label_metrics(bottom->font, bottom->size, bottom->text);
    const float bottom_cap = bm.baseline - bm.cap_top;
    const float apart = (top_h + bottom_h) * 0.5f + extra_gap;
    const float t = slot.cy() - (apart + (bottom_cap - top_cap) * 0.5f) * 0.5f; // top band's middle
    const float b = t + apart;
    if (bands != nullptr) {
        *bands = {t, b};
    }
    label_at(k, dl, top.font, top.size, {slot.x0, t - top_h * 0.5f, slot.x1, t + top_h * 0.5f},
             LabelAlign::Left, top.colour, top.text);
    label_at(k, dl, bottom->font, bottom->size,
             {slot.x0, b - bottom_h * 0.5f, slot.x1, b + bottom_h * 0.5f}, LabelAlign::Left,
             bottom->colour, bottom->text);
}

// The Ear card's modes: rounded blocks of a third of the icon, one (a note) or two (the second
// higher, lower, or beside the first), the pair centred as one. False for another icon.
static bool draw_ear_glyph(ImDrawList* dl, const char* icon, ImVec2 centre, float size, ImVec4 colour) {
    const bool one = std::strcmp(icon, icon::kEarNote) == 0;
    const bool up = std::strcmp(icon, icon::kEarUp) == 0;
    const bool down = std::strcmp(icon, icon::kEarDown) == 0;
    const bool together = std::strcmp(icon, icon::kEarTogether) == 0;
    if (!one && !up && !down && !together) {
        return false;
    }
    const float side = size * 0.3f;
    const float step = size * 0.22f; // each block's centre from the middle, along each axis
    const auto block = [&](float cx, float cy) {
        dl->AddRectFilled({cx - side * 0.5f, cy - side * 0.5f}, {cx + side * 0.5f, cy + side * 0.5f},
                          u32(colour), side * 0.25f);
    };
    if (one) {
        block(centre.x, centre.y);
    } else if (together) {
        block(centre.x - step, centre.y);
        block(centre.x + step, centre.y);
    } else {
        block(centre.x - step, centre.y + (up ? step : -step));
        block(centre.x + step, centre.y + (up ? -step : step));
    }
    return true;
}

// The metronome: the tapering case, its swinging arm and the weight on it. False for another icon.
static bool draw_metronome_glyph(ImDrawList* dl, const char* icon, ImVec2 centre, float size,
                                ImVec4 colour) {
    if (std::strcmp(icon, icon::kMetronome) != 0) {
        return false;
    }
    const float w = std::max(1.5f, size * 0.09f);
    const ImVec2 bl{centre.x - size * 0.36f, centre.y + size * 0.44f};
    const ImVec2 br{centre.x + size * 0.36f, centre.y + size * 0.44f};
    const ImVec2 tr{centre.x + size * 0.15f, centre.y - size * 0.44f};
    const ImVec2 tl{centre.x - size * 0.15f, centre.y - size * 0.44f};
    const ImVec2 body[4] = {bl, br, tr, tl};
    dl->AddPolyline(body, 4, u32(colour), ImDrawFlags_Closed, w);
    const ImVec2 base{centre.x, centre.y + size * 0.28f};
    const ImVec2 tip{centre.x + size * 0.2f, centre.y - size * 0.3f};
    dl->AddLine(base, tip, u32(colour), w);
    dl->AddCircleFilled({base.x + (tip.x - base.x) * 0.55f, base.y + (tip.y - base.y) * 0.55f},
                        std::max(1.8f, size * 0.09f), u32(colour), 12);
    return true;
}

namespace {

// An icon of the icon font (a private-use code point): its ink centred on `centre`, not its line box
// (which sits it too high) and not the glyph's bounds (the bitmap has room on one side that the
// drawing does not use). False when `icon` is not one.
bool draw_font_icon(const Kit& k, ImDrawList* dl, const char* icon, ImVec2 centre, float size,
                    ImVec4 colour) {
    unsigned int cp = 0;
    const int len = ImTextCharFromUtf8(&cp, icon, nullptr);
    if (len > 0 && icon[len] == '\0' && cp >= 0xE000 && cp <= 0xF8FF) {
        // An icon: centre its ink, not its line box (which sits it too high) and not the glyph's
        // bounds (the bitmap has room on one side that the drawing does not use).
        // The pen position, drawn at `size` from the baked size's metrics. A whole pixel: the text
        // is placed on pixels, at a whole size (the baked glyph drawn as it is, not scaled). Rounding
        // to the nearest keeps the ink within half a pixel of the centre on each axis; where that
        // leaves it a hair from the half (the pixels read back cannot tell), the icon is drawn a
        // point larger or smaller, whichever lands it nearest.
        const bool play = std::strcmp(icon, icon::kPlay) == 0;
        struct Placed {
            ImVec2 at;
            float miss = 0.0f; // the larger of the two axes' distance from the centre
            float size = 0.0f;
            bool found = false;
        };
        const auto place = [&](float at_size) {
            Placed out;
            ImFontBaked* baked = k.regular->GetFontBaked(at_size);
            const ImFontGlyph* g = baked->FindGlyphNoFallback(static_cast<ImWchar>(cp));
            if (g == nullptr) {
                return out;
            }
            const GlyphInk ink = glyph_ink(k.regular, baked, g, static_cast<ImWchar>(cp));
            const float scale = at_size / baked->Size;
            // A play triangle sits right of centre by its optical share of its width: the one
            // icon centred off its ink.
            const float optical = play ? kPlayOpticalShare * (ink.r - ink.l) * scale : 0.0f;
            const float want_x = centre.x + optical - (ink.l + ink.r) * 0.5f * scale;
            const float want_y = centre.y - (ink.t + ink.b) * 0.5f * scale;
            out.at = {std::round(want_x), std::round(want_y)};
            out.miss = std::max(std::fabs(out.at.x - want_x), std::fabs(out.at.y - want_y));
            out.size = at_size;
            out.found = true;
            return out;
        };
        // A size the atlas has not baked costs a bake on the frame thread, and which sizes it
        // holds changes with its history (a lost device empties it), so a running app draws the
        // whole size only; a shot and the audit try the neighbours too.
        const bool nudge = exact_shot() || audit::on();
        const float whole = std::max(1.0f, std::round(size));
        Placed best = place(whole);
        if (best.found) {
            constexpr float kNear = 0.42f; // inside the half pixel the audit reads, with room
            for (const float change : {-1.0f, 1.0f}) {
                if (best.miss <= kNear) {
                    break;
                }
                if (!nudge || whole + change < 1.0f) {
                    continue;
                }
                const Placed other = place(whole + change);
                if (other.found && other.miss < best.miss) {
                    best = other;
                }
            }
            dl->AddText(k.regular, best.size, best.at, u32(colour), icon);
            return true;
        }
    }
    return false;
}

} // namespace

void glyph(const Kit& k, ImDrawList* dl, const char* icon, ImVec2 centre, float size,
           ImVec4 colour) {
    if (audit::icons_hidden()) {
        return;
    }
    if (std::strcmp(icon, icon::kLeftHand) == 0 || std::strcmp(icon, icon::kRightHand) == 0) {
        draw_glyph(dl,
                   std::strcmp(icon, icon::kRightHand) == 0 ? MorphGlyph::RightHand
                                                            : MorphGlyph::LeftHand,
                   centre, size, colour);
        return;
    }
    if (std::strcmp(icon, icon::kReplay) == 0) {
        draw_glyph(dl, MorphGlyph::Replay, centre, size, colour);
        return;
    }
    if (std::strcmp(icon, icon::kNote) == 0 || std::strcmp(icon, icon::kNoteDotted) == 0) {
        // A quarter note: a tilted filled head and a stem up its right side; dotted, a dot after the
        // head.
        const ImVec2 head{centre.x - size * 0.12f, centre.y + size * 0.28f};
        const float rx = size * 0.24f;
        const float ry = size * 0.17f;
        const float tilt = -0.42f;
        ImVec2 pts[18];
        for (int i = 0; i < 18; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 18.0f;
            const float ex = std::cos(a) * rx;
            const float ey = std::sin(a) * ry;
            pts[i] = {head.x + ex * std::cos(tilt) - ey * std::sin(tilt),
                      head.y + ex * std::sin(tilt) + ey * std::cos(tilt)};
        }
        dl->AddConvexPolyFilled(pts, 18, u32(colour));
        const float sx = head.x + rx * 0.88f;
        dl->AddLine({sx, head.y - size * 0.04f}, {sx, centre.y - size * 0.46f}, u32(colour),
                    std::max(1.5f, size * 0.1f));
        if (std::strcmp(icon, icon::kNoteDotted) == 0) {
            dl->AddCircleFilled({head.x + rx + size * 0.3f, head.y}, std::max(1.4f, size * 0.09f),
                                u32(colour), 10);
        }
        return;
    }
    if (draw_ear_glyph(dl, icon, centre, size, colour) ||
        draw_metronome_glyph(dl, icon, centre, size, colour)) {
        return;
    }
    // By content, not by pointer: string literals are not always merged across files.
    if (std::strcmp(icon, icon::kCMarker) == 0) {
        // Drawn, not a font icon: the key's own C marker, a bold C over a short accent line.
        // The C's ink and the line under it are centred together, as one icon.
        // A whole-pixel size, so the C's ink read from its bitmap is the ink drawn.
        const float ts = std::round(size * 0.92f);
        const float line_w = std::max(1.5f, size * 0.09f);
        const float line_dy = size * 0.42f;
        ImFontBaked* baked = k.bold->GetFontBaked(ts);
        if (const ImFontGlyph* g = baked->FindGlyphNoFallback('C')) {
            const GlyphInk ink = glyph_ink(k.bold, baked, g, 'C');
            const float scale = ts / baked->Size;
            // The C sits over the line as designed (pen top `pen_dy` above the line's reference); the
            // ink's top and bottom follow, and the pair moves to the centre.
            const float pen_dy = -text_size(k.bold, ts, "C").y * 0.58f;
            const float top = pen_dy + ink.t * scale;
            const float bottom = std::max(pen_dy + ink.b * scale, line_dy + line_w * 0.5f);
            const float shift = -(top + bottom) * 0.5f;
            dl->AddText(k.bold, ts,
                        {std::round(centre.x - (ink.l + ink.r) * 0.5f * scale),
                         std::round(centre.y + shift + pen_dy)},
                        u32(colour), "C");
            // AddLine draws half a pixel right and down of the points it is given: taken back, so
            // the line is centred under the C.
            const float y = centre.y + shift + line_dy - 0.5f;
            dl->AddLine({centre.x - size * 0.26f - 0.5f, y}, {centre.x + size * 0.26f - 0.5f, y},
                        u32(colour), line_w);
        }
        return;
    }
    if (draw_font_icon(k, dl, icon, centre, size, colour)) {
        return;
    }
    const ImVec2 ts = text_size(k.regular, size, icon);
    dl->AddText(k.regular, size,
                {std::round(centre.x - ts.x * 0.5f), std::round(centre.y - ts.y * 0.5f)},
                u32(colour), icon);
}

float hand_pair_width(float size) { return size * 2.0f; }

void hand_pair(const Kit& k, ImDrawList* dl, ImVec2 centre, float size, ImVec4 colour, bool left,
               bool right) {
    const float step = size;
    if (left) {
        glyph(k, dl, icon::kLeftHand, {centre.x - step * 0.5f, centre.y}, size, colour);
    }
    if (right) {
        glyph(k, dl, icon::kRightHand, {centre.x + step * 0.5f, centre.y}, size, colour);
    }
}

void forget_fonts() {
    forget_font_ink();
    fit_cache().clear();
    metrics_store().clear();
}

FitText fit_unreported(ImFont* font, float size, std::string_view text, float max_w) {
    FitCache& cache = fit_cache();
    text = screen_text(text);
    return cache.fit(font, size, text, max_w,
                     [font, size](const char* line) { return text_size(font, size, line).x; });
}

FitText fit(ImFont* font, float size, std::string_view text, float max_w) {
    FitText shown = fit_unreported(font, size, text, max_w);
    audit::cut(screen_text(text), shown.view()); // the audit lists what the settings sheet cut
    return shown;
}

} // namespace etude::app::ui
