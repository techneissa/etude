// SPDX-License-Identifier: GPL-3.0-or-later
// The drawn icons' path data: Lucide's (ISC licence) and the app's own hands.
#include "morph_glyphs.hpp"

#include "ink_measure.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <memory>

namespace etude::app::ui {

namespace {

struct GlyphDef {
    const char* d;
    float stroke = 2.0f; // in grid units, Lucide's 2
    float scale = 1.0f;  // drawn this much larger than `size`
    bool mirror = false; // flipped left to right
};

// The volume icons share Lucide's speaker body.
#define ETUDE_SPEAKER                                                                            \
    "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 " \
    "0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z"

// Lucide's music: two note heads (circles as paths) and the beam over their stems.
#define ETUDE_NOTES                                                                              \
    "M9 18V5l12-2v13M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0zM21 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z"
// Lucide's audio-lines.
#define ETUDE_LINES "M2 10v3M6 6v11M10 3v18M14 8v7M18 5v13M22 10v3"

// Lucide's rect (3, 3, 18 x 18, rounded by 2) as a path.
#define ETUDE_SCREEN "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"

// A right hand seen from the back, as a player sees it on the keys, in the Lucide stroke style:
// four fingers of different lengths, a rounded palm and the thumb stretching out to the left
// (inward), then the gaps between the fingers. The left hand is its mirror image.
#define ETUDE_HAND                                                                               \
    "M7 18.5A2.5 2.5 0 0 0 9.5 21H16.5A2.5 2.5 0 0 0 19 18.5V7.5A1.5 1.5 0 0 0 16 7.5V4.5A1.5 "    \
    "1.5 "                                                                                         \
    "0 0 0 13 4.5V3.5A1.5 1.5 0 0 0 10 3.5V5A1.5 1.5 0 0 0 7 5V12.5L4.59 9.61A1.4 1.4 0 0 1 2.61 " \
    "11.59L7 16.5Z"                                                                                \
    "M16 7.5V12M13 4.5V12M10 5V12"
constexpr float kViewStroke = 1.8f; // the sheet-music icons' line, a little finer: staff lines sit close
constexpr float kHandScale = 1.15f; // the hands fill their square more than Lucide's icons
constexpr float kHandStroke = 1.9f;

constexpr std::array<GlyphDef, static_cast<std::size_t>(MorphGlyph::Count)> kGlyphs{{
    // play
    {"M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"},
    // pause: Lucide's two rounded rects as paths
    {"M15 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1h-3a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z"
     "M6 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z"},
    // volume-x
    {ETUDE_SPEAKER "M16.5 14.5l5-5M16.5 9.5l5 5"},
    // volume-1
    {ETUDE_SPEAKER "M16 9a5 5 0 0 1 0 6"},
    // volume-2
    {ETUDE_SPEAKER "M16 9a5 5 0 0 1 0 6M19.364 18.364a9 9 0 0 0 0-12.728"},
    // settings: the cog and Lucide's circle of radius 3 as a path
    {"M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 "
     "2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 "
     "1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 "
     "2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"
     "M15 12a3 3 0 0 1-6 0a3 3 0 0 1 6 0z"},
    // x
    {"M18 6 6 18M6 6l12 12"},
    {ETUDE_HAND, kHandStroke, kHandScale, true},  // left: mirrored, thumb on the right
    {ETUDE_HAND, kHandStroke, kHandScale, false}, // right
    // chevron-right
    {"m9 18 6-6-6-6"},
    // The bar styles: Lucide's panel-top frame (a rounded square) with the bar drawn in its top
    // part. Strip: a full-width band. Groups: two pills over a wide scrubber. Dock: one centred
    // pill over a narrower scrubber.
    {ETUDE_SCREEN "M3 9h18"},
    {ETUDE_SCREEN "M6.5 6.2h4M13.5 6.2h4M6.5 11.5h11"},
    {ETUDE_SCREEN "M7 6.2h10M9 11.5h6"},
    // pin
    {"M12 17v5"
     "M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 "
     "1-1v-.76a2 "
     "2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H8a2 2 0 0 0 0 4 "
     "1 "
     "1 0 0 1 1 1z"},
    // pin-off
    {"M12 17v5"
     "M15 9.34V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H7.89"
     "m2 2 20 20"
     "M9 9v1.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h11"},
    // replay: rotate-ccw (the ring open at the upper left, its arrowhead pointing back) with a
    // small play head inside, so it reads as "play it again" and not as the skip arrows' number
    // ring
    {"M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5"
     "M10.4 9.4v5.2L14.6 12z"},
    // Ensemble: Lucide's music (two notes joined by a beam), and with a half slash and a full one
    {ETUDE_NOTES},
    {ETUDE_NOTES "M13 13l8 8"},
    {ETUDE_NOTES "m2 2 20 20"},
    // Voice: Lucide's audio-lines, and with a half slash and a full one
    {ETUDE_LINES},
    {ETUDE_LINES "M13 13l8 8"},
    {ETUDE_LINES "m2 2 20 20"},
    // chevron-down, chevron-up
    {"m6 9 6 6 6-6"},
    {"m18 15-6-6-6 6"},
    // The sheet-music button, drawn as music. Off: a staff crossed out (as the accompaniment
    // button's slash). On: a staff above two falling notes. Full: a staff filling the box, with a
    // note on it (its head a small ring, so it reads as a dot).
    {"M4 7h16M4 12h16M4 17h16M2 2l20 20", kViewStroke},
    {"M3 5h18M3 9h18M3 13h18M8 17.5v3M16 17.5v3", kViewStroke},
    {"M3 4.5h18M3 9.5h18M3 14.5h18M3 19.5h18M10.2 12a1.2 1.2 0 1 1-2.4 0 1.2 1.2 0 0 1 2.4 0zM10.1 12V6",
     kViewStroke},
}};

#undef ETUDE_SPEAKER
#undef ETUDE_NOTES
#undef ETUDE_LINES
#undef ETUDE_HAND
#undef ETUDE_SCREEN

} // namespace

namespace {

// Digits in the stroke style of the glyphs, each in a box `height` 10 tall and `advance` wide.
struct Digit {
    const char* d;
    float advance;
};
constexpr std::array<Digit, 10> kDigits{{
    {"M3 0C1.3 0 0.3 1.9 0.3 5S1.3 10 3 10 5.7 8.1 5.7 5 4.7 0 3 0z", 6.0f},   // 0
    {"M0.6 2.2L2.7 0V10", 4.0f},                                               // 1
    {"M0.6 3C0.6 1.2 1.6 0 3 0S5.4 1.2 5.4 3C5.4 5.5 3 7.2 0.5 10H5.6", 6.0f}, // 2
    {"M0.6 1.2C1.2 0.4 2 0 3 0C4.4 0 5.3 0.9 5.3 2.3C5.3 3.8 4.3 4.6 2.6 4.6C4.5 4.6 5.6 5.6 "
     "5.6 7.4C5.6 9 4.5 10 3 10C1.8 10 0.9 9.5 0.4 8.7",
     6.0f},                          // 3
    {"M4.4 10V0L0.2 6.8H5.8", 6.0f}, // 4
    {"M5.2 0H1.1L0.7 4.6C1.3 4.1 2.1 3.8 3 3.8C4.6 3.8 5.7 4.9 5.7 6.9C5.7 8.8 4.6 10 3 10C1.8 "
     "10 0.9 9.6 0.4 8.8",
     6.0f}, // 5
    {"M5.1 1.2C4.6 0.4 3.9 0 3 0C1.3 0 0.4 2.3 0.4 6.5C0.4 8.8 1.4 10 3 10C4.6 10 5.6 8.9 5.6 "
     "7C5.6 5.2 4.6 4.1 3 4.1C1.9 4.1 1 4.6 0.5 5.6",
     6.0f},                      // 6
    {"M0.4 0H5.6L2.2 10", 6.0f}, // 7
    {"M3 0C1.6 0 0.8 0.9 0.8 2.2C0.8 3.6 1.7 4.5 3 4.6C4.3 4.5 5.2 3.6 5.2 2.2C5.2 0.9 4.4 0 3 "
     "0zM3 4.6C1.4 4.7 0.4 5.7 0.4 7.3C0.4 9 1.5 10 3 10S5.6 9 5.6 7.3C5.6 5.7 4.6 4.7 3 4.6z",
     6.0f}, // 8
    {"M0.9 8.8C1.4 9.6 2.1 10 3 10C4.7 10 5.6 7.7 5.6 3.5C5.6 1.2 4.6 0 3 0C1.4 0 0.4 1.1 0.4 "
     "3C0.4 4.8 1.4 5.9 3 5.9C4.1 5.9 5 5.4 5.5 4.4",
     6.0f}, // 9
}};
constexpr float kDigitGap = 1.2f;
constexpr float kDigitHeight = 10.0f;

// The digits of `n` (1..30) side by side, centred on (12, 12).
MorphIconData make_number(int n) {
    int digits[2];
    int count = 0;
    if (n >= 10) {
        digits[count++] = n / 10;
    }
    digits[count++] = n % 10;
    float width = kDigitGap * static_cast<float>(count - 1);
    for (int i = 0; i < count; ++i) {
        width += kDigits[static_cast<std::size_t>(digits[i])].advance;
    }
    MorphIconData icon;
    float x = 12.0f - width * 0.5f;
    for (int i = 0; i < count; ++i) {
        const Digit& dg = kDigits[static_cast<std::size_t>(digits[i])];
        const int first = icon.path.cubic_count;
        icon.error = parse_svg_path(dg.d, icon.path);
        if (icon.error != PathError::None) {
            return icon;
        }
        for (int j = first; j < icon.path.cubic_count; ++j) {
            Cubic& c = icon.path.cubics[static_cast<std::size_t>(j)];
            for (MorphPoint* p : {&c.p0, &c.c1, &c.c2, &c.p3}) {
                p->x += x;
                p->y += 12.0f - kDigitHeight * 0.5f;
            }
        }
        x += dg.advance + kDigitGap;
    }
    resample(icon.path, icon.shape);
    return icon;
}

// The icon's curves moved so that the centre of their bounding box is the grid's centre (12, 12),
// plus `optical_share` of the ink's width (the stroke included) to the right. Lucide's icons are mostly drawn centred, the app's own
// are not, and a button centres its icon on that point: so the ink of every icon sits where the
// rule wants it. The stroke is the same on every side, so the curves' bounds are the ink's centre.
GlyphExtent centre_on_grid(MorphIconData& icon, float optical_share, float stroke) {
    constexpr int kSteps = 16;
    float x0 = 1e9f;
    float y0 = 1e9f;
    float x1 = -1e9f;
    float y1 = -1e9f;
    const auto bezier = [](float a, float b, float c, float d, float t) {
        const float u = 1.0f - t;
        return u * u * u * a + 3.0f * u * u * t * b + 3.0f * u * t * t * c + t * t * t * d;
    };
    for (int j = 0; j < icon.path.cubic_count; ++j) {
        const Cubic& c = icon.path.cubics[static_cast<std::size_t>(j)];
        for (int i = 0; i <= kSteps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kSteps);
            const float x = bezier(c.p0.x, c.c1.x, c.c2.x, c.p3.x, t);
            const float y = bezier(c.p0.y, c.c1.y, c.c2.y, c.p3.y, t);
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    if (!(x1 >= x0) || !(y1 >= y0)) {
        return {};
    }
    const float dx = 12.0f - (x0 + x1) * 0.5f + optical_share * (x1 - x0 + stroke);
    const float dy = 12.0f - (y0 + y1) * 0.5f;
    for (int j = 0; j < icon.path.cubic_count; ++j) {
        Cubic& c = icon.path.cubics[static_cast<std::size_t>(j)];
        for (MorphPoint* p : {&c.p0, &c.c1, &c.c2, &c.p3}) {
            p->x += dx;
            p->y += dy;
        }
    }
    resample(icon.path, icon.shape);
    return {(x1 - x0 + stroke) * 0.5f, (y1 - y0 + stroke) * 0.5f};
}

std::array<GlyphExtent, static_cast<std::size_t>(MorphGlyph::Count)> g_extent;

} // namespace

const MorphIconData& number_data(int seconds) {
    constexpr int kCount = kSkipMax - kSkipMin + 1;
    static const auto all = [] {
        auto icons = std::make_unique<std::array<MorphIconData, kCount>>();
        for (int i = 0; i < kCount; ++i) {
            (*icons)[static_cast<std::size_t>(i)] = make_number(kSkipMin + i);
            assert((*icons)[static_cast<std::size_t>(i)].error == PathError::None &&
                   "a shipped number failed to parse");
        }
        return icons;
    }();
    return (*all)[static_cast<std::size_t>(std::clamp(seconds, kSkipMin, kSkipMax) - kSkipMin)];
}

const MorphIconData& morph_data(MorphGlyph glyph) {
    static const auto all = [] {
        // One more than the glyphs: Count, no icon, stays empty.
        auto icons = std::make_unique<std::array<MorphIconData, kGlyphs.size() + 1>>();
        for (std::size_t i = 0; i < kGlyphs.size(); ++i) {
            MorphIconData& icon = (*icons)[i];
            icon = make_icon(kGlyphs[i].d);
            assert(icon.error == PathError::None && "a shipped glyph failed to parse");
            if (kGlyphs[i].mirror) {
                for (int j = 0; j < icon.path.cubic_count; ++j) {
                    Cubic& c = icon.path.cubics[static_cast<std::size_t>(j)];
                    for (MorphPoint* p : {&c.p0, &c.c1, &c.c2, &c.p3}) {
                        p->x = 24.0f - p->x;
                    }
                }
            }
            // A play triangle sits right of centre by its named optical share of its width: the
            // one icon the rule does not centre exactly. Everything else is centred on its bounds.
            const bool play = static_cast<MorphGlyph>(i) == MorphGlyph::Play;
            g_extent[i] = centre_on_grid(icon, play ? kPlayOpticalShare : 0.0f, kGlyphs[i].stroke);
        }
        return icons;
    }();
    const auto i = static_cast<std::size_t>(glyph);
    return (*all)[i < kGlyphs.size() ? i : kGlyphs.size()];
}

bool glyph_ok(MorphGlyph glyph) {
    const MorphIconData& d = morph_data(glyph);
    return glyph < MorphGlyph::Count && d.error == PathError::None && d.path.subpath_count > 0;
}

GlyphExtent glyph_extent(MorphGlyph glyph) {
    morph_data(glyph); // built on first use, with the extents
    const auto i = static_cast<std::size_t>(glyph);
    return i < g_extent.size() ? g_extent[i] : GlyphExtent{};
}

GlyphStyle glyph_style(MorphGlyph glyph) {
    const auto i = static_cast<std::size_t>(glyph);
    return i < kGlyphs.size() ? GlyphStyle{kGlyphs[i].stroke, kGlyphs[i].scale} : GlyphStyle{};
}

} // namespace etude::app::ui
