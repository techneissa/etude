// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_name.hpp"

#include "font_ink.hpp"

#include "etude/accidental_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace etude::app::ui {

namespace {

// The gap before a sign, in ems of the type (the chord label's). The signs are drawn in both of the
// audit's frames, so the difference between them holds the letter alone.
constexpr float kBeforeSignEm = 0.05f;
constexpr std::size_t kMostSigns = 3;
// On a name set large on its own (a key, a title) the sign is a little taller than the capital, on
// the capital's middle, so it reads at a glance: this many times the share the chord label gives
// it.
constexpr float kSignBoost = 1.45f;

struct Signs {
    AccidentalKind kinds[kMostSigns]{};
    std::size_t count = 0;
};

// The signs after the letter of `plain`; false when it is not a letter and signs.
bool parse_signs(const char* plain, Signs* out) {
    if (plain[0] < 'A' || plain[0] > 'G') {
        return false;
    }
    for (const char* p = plain + 1; *p != '\0'; ++p) {
        if ((*p != '#' && *p != 'b') || out->count == kMostSigns) {
            return false;
        }
        out->kinds[out->count++] = *p == '#' ? AccidentalKind::Sharp : AccidentalKind::Flat;
    }
    return true;
}

void fill_shape(ImDrawList* dl, const AccidentalShape& shape, ImVec2 at, ImU32 colour) {
    for (std::size_t q = 0; q < shape.count; ++q) {
        const GlyphQuad& quad = shape.quads[q];
        const ImVec2 pts[4] = {{at.x + quad.x[0], at.y + quad.y[0]},
                               {at.x + quad.x[1], at.y + quad.y[1]},
                               {at.x + quad.x[2], at.y + quad.y[2]},
                               {at.x + quad.x[3], at.y + quad.y[3]}};
        dl->AddConvexPolyFilled(pts, 4, colour);
    }
}

// The name's ink across: the letter's, and each sign with the gap before it.
float group_width(float size, const LabelMetrics& m, const Signs& signs, float sign_h) {
    float group = m.ink_right - m.ink_left;
    for (std::size_t i = 0; i < signs.count; ++i) {
        group +=
            std::round(kBeforeSignEm * size) + accidental_glyph_width_px(signs.kinds[i], sign_h);
    }
    return group;
}

float sign_height(ImFont* font, float size) {
    const CapBand cap = cap_band(font, size);
    return accidental_glyph_height_px((cap.bottom - cap.top) * kSignBoost);
}

} // namespace

float note_name_width(ImFont* font, float size, const char* plain) {
    Signs signs;
    if (plain == nullptr || !parse_signs(plain, &signs)) {
        return plain == nullptr ? 0.0f : text_size(font, size, plain).x;
    }
    const char letter[2] = {plain[0], '\0'};
    return group_width(size, label_metrics(font, size, letter), signs, sign_height(font, size));
}

void note_name_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
                  ImVec4 colour, const char* plain) {
    Signs signs;
    if (plain == nullptr || !parse_signs(plain, &signs) || signs.count == 0) {
        label_at(k, dl, font, size, slot, LabelAlign::Centre, colour, plain);
        return;
    }
    const char letter[2] = {plain[0], '\0'};
    const LabelMetrics m = label_metrics(font, size, letter);
    const float sign_h = sign_height(font, size);
    const float group = group_width(size, m, signs, sign_h);
    const float left = std::round(slot.cx() - group * 0.5f);
    // The letter's ink starts at `left`: its pen is `ink_left` before it. Left aligned, so the
    // audit holds it vertically only.
    label_at(k, dl, font, size, {left - m.ink_left, slot.y0, slot.x1, slot.y1}, LabelAlign::Left,
             colour, letter);
    float x = left + (m.ink_right - m.ink_left);
    const float top = std::round(slot.cy() - sign_h * 0.5f);
    for (std::size_t i = 0; i < signs.count; ++i) {
        x += std::round(kBeforeSignEm * size);
        fill_shape(dl, accidental_shape(signs.kinds[i], sign_h), {x, top}, u32(colour));
        x += accidental_glyph_width_px(signs.kinds[i], sign_h);
    }
}

namespace {

// A chord's name cut into the runs it is drawn in: a run of text, or a sign.
constexpr std::size_t kMostRuns = 14;
constexpr std::size_t kRunText = 12;
struct Run {
    char text[kRunText]{};
    AccidentalKind sign = AccidentalKind::Flat;
    bool is_sign = false;
};
struct Runs {
    Run run[kMostRuns]{};
    std::size_t count = 0;
};

void add_text(Runs& out, const char* text, std::size_t length) {
    if (length == 0 || out.count == kMostRuns) {
        return;
    }
    // Text next to text is one run, so the letters keep their own spacing.
    if (out.count > 0 && !out.run[out.count - 1].is_sign) {
        Run& last = out.run[out.count - 1];
        const std::size_t have = std::strlen(last.text);
        const std::size_t room = kRunText - 1 - have;
        std::memcpy(last.text + have, text, std::min(length, room));
        return;
    }
    Run& r = out.run[out.count++];
    std::memcpy(r.text, text, std::min(length, kRunText - 1));
}

void add_sign(Runs& out, AccidentalKind kind) {
    if (out.count == kMostRuns) {
        return;
    }
    Run& r = out.run[out.count++];
    r.is_sign = true;
    r.sign = kind;
}

// A name with signs written '#' and 'b' ("F#", "m7b5"): `letters_first` is a root or bass, whose
// first character is a letter, the signs after it; a quality's every 'b' is a sign.
void add_name(Runs& out, const char* name) {
    const char* start = name;
    for (const char* p = name; *p != '\0'; ++p) {
        if (*p == '#' || *p == 'b') {
            add_text(out, start, static_cast<std::size_t>(p - start));
            add_sign(out, *p == '#' ? AccidentalKind::Sharp : AccidentalKind::Flat);
            start = p + 1;
        }
    }
    add_text(out, start, std::strlen(start));
}

Runs chord_runs(const char* root, const char* quality, const char* bass) {
    Runs out;
    add_name(out, root != nullptr ? root : "");
    add_name(out, quality != nullptr ? quality : "");
    if (bass != nullptr && bass[0] != '\0') {
        add_text(out, "/", 1);
        add_name(out, bass);
    }
    return out;
}

float runs_width(ImFont* font, float size, const Runs& runs, float sign_h) {
    float width = 0.0f;
    for (std::size_t i = 0; i < runs.count; ++i) {
        const Run& r = runs.run[i];
        if (r.is_sign) {
            width += std::round(kBeforeSignEm * size) + accidental_glyph_width_px(r.sign, sign_h);
        } else {
            width += text_size(font, size, r.text).x;
        }
    }
    return width;
}

} // namespace

float chord_name_width(ImFont* font, float size, const char* root, const char* quality,
                       const char* bass) {
    return runs_width(font, size, chord_runs(root, quality, bass), sign_height(font, size));
}

void chord_name_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
                   ImVec4 colour, const char* root, const char* quality, const char* bass) {
    const Runs runs = chord_runs(root, quality, bass);
    float shown = size;
    float sign_h = sign_height(font, shown);
    const float width = runs_width(font, shown, runs, sign_h);
    if (width > slot.w() && width > 0.0f) {
        shown = std::floor(size * slot.w() / width);
        sign_h = sign_height(font, shown);
    }
    float x = slot.x0;
    const float top = std::round(slot.cy() - sign_h * 0.5f);
    for (std::size_t i = 0; i < runs.count; ++i) {
        const Run& r = runs.run[i];
        if (r.is_sign) {
            x += std::round(kBeforeSignEm * shown);
            fill_shape(dl, accidental_shape(r.sign, sign_h), {x, top}, u32(colour));
            x += accidental_glyph_width_px(r.sign, sign_h);
            continue;
        }
        label_at(k, dl, font, shown, {x, slot.y0, slot.x1, slot.y1}, LabelAlign::Left, colour,
                 r.text);
        x += text_size(font, shown, r.text).x;
    }
}

} // namespace etude::app::ui
