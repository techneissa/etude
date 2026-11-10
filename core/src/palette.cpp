// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/palette.hpp"

#include "etude/saved_palettes.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

constexpr std::size_t scheme_index(ColorScheme s) { return static_cast<std::size_t>(s); }

Swatches of(std::span<const RGBA> colors) {
    Swatches out;
    out.count = static_cast<int>(std::min<std::size_t>(colors.size(), kMaxSwatches));
    std::copy_n(colors.begin(), out.count, out.colors.begin());
    return out;
}

// Twelve note colours: hues `step` degrees apart from `hue0`, white keys' and black keys' pitch
// classes alternating between two lightnesses so neighbours differ in brightness too.
Swatches note_wheel(double light_even, double light_odd, double chroma, double hue0, double step) {
    Swatches out;
    out.count = 12;
    for (int i = 0; i < 12; ++i) {
        out.colors[static_cast<std::size_t>(i)] = from_oklch(
            i % 2 == 0 ? light_even : light_odd, chroma, hue0 + step * static_cast<double>(i));
    }
    return out;
}

// One colour per octave from `light0` and `hue0`, both moving by a step per octave.
Swatches octave_ramp(double light0, double light_step, double chroma, double hue0,
                     double hue_step) {
    Swatches out;
    out.count = static_cast<int>(kOctaveColorCount);
    for (int i = 0; i < out.count; ++i) {
        const auto d = static_cast<double>(i);
        out.colors[static_cast<std::size_t>(i)] =
            from_oklch(light0 + light_step * d, chroma, hue0 + hue_step * d);
    }
    return out;
}

Swatches three(RGBA a, RGBA b, RGBA c) { return of(std::array<RGBA, 3>{a, b, c}); }

Swatches hands(Palette p) {
    switch (p) {
    case Palette::Classic:
        break;
    case Palette::Vivid:
        return three(from_oklch(0.74, 0.2, 50.0), from_oklch(0.68, 0.2, 255.0),
                     from_oklch(0.78, 0.02, 255.0));
    case Palette::Pastel:
        return three(from_oklch(0.87, 0.08, 60.0), from_oklch(0.85, 0.07, 245.0),
                     from_oklch(0.86, 0.015, 255.0));
    case Palette::Ocean:
        return three(from_oklch(0.82, 0.12, 180.0), from_oklch(0.66, 0.14, 255.0),
                     from_oklch(0.74, 0.03, 220.0));
    case Palette::Sunset:
        return three(from_oklch(0.78, 0.15, 60.0), from_oklch(0.68, 0.18, 350.0),
                     from_oklch(0.74, 0.03, 30.0));
    case Palette::Mono:
        return three(from_oklch(0.92, 0.01, 250.0), from_oklch(0.68, 0.03, 250.0),
                     from_oklch(0.8, 0.0, 0.0));
    }
    const LookSettings d;
    return three(d.left_color, d.right_color, d.unknown_color);
}

Swatches notes(Palette p) {
    switch (p) {
    case Palette::Classic:
        break;
    case Palette::Vivid:
        return note_wheel(0.68, 0.82, 0.2, 25.0, 30.0);
    case Palette::Pastel:
        return note_wheel(0.86, 0.86, 0.07, 25.0, 30.0);
    case Palette::Ocean:
        return note_wheel(0.68, 0.84, 0.12, 165.0, 10.0);
    case Palette::Sunset:
        return note_wheel(0.68, 0.84, 0.15, 300.0, 14.0);
    case Palette::Mono: {
        Swatches out;
        out.count = 12;
        for (int i = 0; i < 12; ++i) {
            out.colors[static_cast<std::size_t>(i)] =
                from_oklch(0.62 + 0.3 * static_cast<double>(i) / 11.0, 0.03, 250.0);
        }
        return out;
    }
    }
    return of(default_pitch_colors());
}

Swatches octaves(Palette p) {
    switch (p) {
    case Palette::Classic:
        break;
    case Palette::Vivid:
        return octave_ramp(0.72, 0.0, 0.19, 260.0, 40.0);
    case Palette::Pastel:
        return octave_ramp(0.86, 0.0, 0.07, 282.0, 32.0);
    case Palette::Ocean:
        return octave_ramp(0.55, 0.04, 0.12, 260.0, -10.0);
    case Palette::Sunset:
        return octave_ramp(0.58, 0.04, 0.16, 300.0, 20.0);
    case Palette::Mono:
        return octave_ramp(0.55, 0.045, 0.03, 250.0, 0.0);
    }
    return of(default_octave_colors());
}

Swatches velocities(Palette p) {
    switch (p) {
    case Palette::Classic:
        break;
    case Palette::Vivid:
        return three(from_oklch(0.6, 0.22, 265.0), from_oklch(0.88, 0.18, 100.0),
                     from_oklch(0.65, 0.25, 28.0));
    case Palette::Pastel:
        return three(from_oklch(0.82, 0.08, 255.0), from_oklch(0.92, 0.08, 100.0),
                     from_oklch(0.82, 0.09, 20.0));
    case Palette::Ocean:
        return three(from_oklch(0.55, 0.13, 265.0), from_oklch(0.72, 0.12, 215.0),
                     from_oklch(0.88, 0.11, 175.0));
    case Palette::Sunset:
        return three(from_oklch(0.6, 0.15, 300.0), from_oklch(0.78, 0.16, 60.0),
                     from_oklch(0.9, 0.15, 95.0));
    case Palette::Mono:
        return three(from_oklch(0.55, 0.03, 250.0), from_oklch(0.75, 0.03, 250.0),
                     from_oklch(0.95, 0.01, 250.0));
    }
    return of(default_velocity_colors());
}

RGBA single(Palette p) {
    switch (p) {
    case Palette::Classic:
        break;
    case Palette::Vivid:
        return from_oklch(0.72, 0.2, 330.0);
    case Palette::Pastel:
        return from_oklch(0.86, 0.07, 300.0);
    case Palette::Ocean:
        return from_oklch(0.78, 0.12, 200.0);
    case Palette::Sunset:
        return from_oklch(0.76, 0.16, 55.0);
    case Palette::Mono:
        return from_oklch(0.88, 0.01, 250.0);
    }
    return LookSettings{}.single_color;
}

RGBA opaque(RGBA c) {
    c.a = 255;
    return c;
}

double clamp01(double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0; }

} // namespace

std::string_view palette_name(Palette p) {
    switch (p) {
    case Palette::Classic:
        return "Classic";
    case Palette::Vivid:
        return "Vivid";
    case Palette::Pastel:
        return "Pastel";
    case Palette::Ocean:
        return "Ocean";
    case Palette::Sunset:
        return "Sunset";
    case Palette::Mono:
        return "Mono";
    }
    return "";
}

int swatch_count(ColorScheme scheme) {
    switch (scheme) {
    case ColorScheme::ByHand:
        return 3;
    case ColorScheme::ByPitchClass:
        return 12;
    case ColorScheme::ByOctave:
        return static_cast<int>(kOctaveColorCount);
    case ColorScheme::ByVelocity:
        return static_cast<int>(kVelocityColorCount);
    case ColorScheme::Single:
        return 1;
    }
    return 0;
}

namespace {

// The look's colour behind swatch `index`, or null for one the scheme does not have.
template <class Look> auto* swatch_slot(Look& look, ColorScheme scheme, int index) {
    using Ptr = decltype(&look.single_color);
    if (index < 0 || index >= swatch_count(scheme)) {
        return Ptr{nullptr};
    }
    const auto i = static_cast<std::size_t>(index);
    switch (scheme) {
    case ColorScheme::ByHand:
        return index == 0 ? &look.left_color : index == 1 ? &look.right_color : &look.unknown_color;
    case ColorScheme::ByPitchClass:
        return &look.pitch_colors[i];
    case ColorScheme::ByOctave:
        return &look.octave_colors[i];
    case ColorScheme::ByVelocity:
        return &look.velocity_colors[i];
    case ColorScheme::Single:
        return &look.single_color;
    }
    return Ptr{nullptr};
}

} // namespace

RGBA swatch_color(const LookSettings& look, ColorScheme scheme, int index) {
    const RGBA* slot = swatch_slot(look, scheme, index);
    return slot != nullptr ? *slot : RGBA{0, 0, 0, 255};
}

bool set_swatch_color(LookSettings& look, ColorScheme scheme, int index, RGBA c) {
    RGBA* slot = swatch_slot(look, scheme, index);
    if (slot == nullptr) {
        return false;
    }
    *slot = opaque(c);
    return true;
}

Swatches scheme_swatches(const LookSettings& look, ColorScheme scheme) {
    Swatches out;
    out.count = swatch_count(scheme);
    for (int i = 0; i < out.count; ++i) {
        out.colors[static_cast<std::size_t>(i)] = swatch_color(look, scheme, i);
    }
    return out;
}

namespace {

Swatches build_swatches(Palette p, ColorScheme scheme) {
    switch (scheme) {
    case ColorScheme::ByHand:
        return hands(p);
    case ColorScheme::ByPitchClass:
        return notes(p);
    case ColorScheme::ByOctave:
        return octaves(p);
    case ColorScheme::ByVelocity:
        return velocities(p);
    case ColorScheme::Single:
        return of(std::array<RGBA, 1>{single(p)});
    }
    return {};
}

} // namespace

Swatches palette_swatches(Palette p, ColorScheme scheme) {
    // Worked out once: the page asks for every palette's swatches on every frame it is drawn.
    using Table = std::array<std::array<Swatches, kColorSchemeCount>, kAllPalettes.size()>;
    static const Table kTable = [] {
        Table t{};
        for (std::size_t i = 0; i < kAllPalettes.size(); ++i) {
            for (std::size_t j = 0; j < kColorSchemeCount; ++j) {
                t[i][j] = build_swatches(kAllPalettes[i], kAllColorSchemes[j]);
            }
        }
        return t;
    }();
    const auto i = static_cast<std::size_t>(p);
    const auto j = static_cast<std::size_t>(scheme);
    if (i >= kTable.size() || j >= kColorSchemeCount) {
        return {};
    }
    return kTable[i][j];
}

void apply_palette(LookSettings& look, Palette p) {
    const ColorScheme scheme = look.color_scheme;
    const Swatches s = palette_swatches(p, scheme);
    for (int i = 0; i < s.count; ++i) {
        set_swatch_color(look, scheme, i, s.colors[static_cast<std::size_t>(i)]);
    }
    if (scheme_index(scheme) < look.palettes.size()) {
        look.palettes[scheme_index(scheme)] = p;
    }
    if (scheme == ColorScheme::ByPitchClass) {
        look.note_slot = -1; // the notes now come from a built-in palette
    }
}

std::optional<Palette> matching_palette(const LookSettings& look, ColorScheme scheme) {
    const Swatches mine = scheme_swatches(look, scheme);
    if (scheme_index(scheme) < look.palettes.size()) {
        const Palette remembered = look.palettes[scheme_index(scheme)];
        if (palette_swatches(remembered, scheme) == mine) {
            return remembered;
        }
    }
    for (const Palette p : kAllPalettes) {
        if (palette_swatches(p, scheme) == mine) {
            return p;
        }
    }
    return std::nullopt;
}

void reset_to_palette(LookSettings& look) {
    if (look.color_scheme == ColorScheme::ByPitchClass && apply_saved_palette(look, look.note_slot)) {
        return; // the notes came from a saved palette: back to it
    }
    const std::size_t i = scheme_index(look.color_scheme);
    apply_palette(look, i < look.palettes.size() ? look.palettes[i] : Palette::Classic);
}

std::span<const int> strip_swatches(ColorScheme scheme) {
    static constexpr std::array<int, 3> kThree{0, 1, 2};
    static constexpr std::array<int, 6> kWhiteNotes{0, 2, 4, 5, 7, 9};
    static constexpr std::array<int, 6> kMiddleOctaves{2, 3, 4, 5, 6, 7};
    static constexpr std::array<int, 1> kOne{0};
    switch (scheme) {
    case ColorScheme::ByHand:
    case ColorScheme::ByVelocity:
        return kThree;
    case ColorScheme::ByPitchClass:
        return kWhiteNotes;
    case ColorScheme::ByOctave:
        return kMiddleOctaves;
    case ColorScheme::Single:
        return kOne;
    }
    return {};
}

ColourPick pick_of(RGBA c) {
    const Hsv hsv = to_hsv(c);
    return {hsv.hue, hsv.saturation, hsv.value};
}

ColourPick pick_keeping_hue(RGBA c, const ColourPick& previous) {
    ColourPick p = pick_of(c);
    if (p.saturation == 0.0 || p.value == 0.0) {
        p.hue = previous.hue;
    }
    return p;
}

RGBA pick_color(const ColourPick& p) { return from_hsv(p.hue, p.saturation, p.value); }

double hue_position(const ColourPick& p) { return clamp01(p.hue / 360.0); }

double square_x(const ColourPick& p) { return clamp01(p.saturation); }

double square_y(const ColourPick& p) { return 1.0 - clamp01(p.value); }

ColourPick with_hue_at(ColourPick p, double t) {
    p.hue = clamp01(t) * 360.0;
    return p;
}

ColourPick with_square_at(ColourPick p, double x, double y) {
    p.saturation = clamp01(x);
    p.value = 1.0 - clamp01(y);
    return p;
}

const QuickPickGrid& quick_picks() {
    static const QuickPickGrid grid = [] {
        QuickPickGrid g{};
        for (std::size_t c = 0; c < kQuickColumns; ++c) {
            g[0][c] = from_hsv(kQuickHues[c], kQuickLightSaturation, 1.0);
            g[1][c] = from_hsv(kQuickHues[c], 1.0, 1.0);
            g[2][c] = from_hsv(kQuickHues[c], 1.0, kQuickDeepValue);
            const double level = 1.0 - static_cast<double>(c) / static_cast<double>(kQuickColumns - 1);
            g[3][c] = from_hsv(0.0, 0.0, level);
        }
        return g;
    }();
    return grid;
}

void RecentColours::add(RGBA c) {
    c = opaque(c);
    const auto end = items_.begin() + static_cast<std::ptrdiff_t>(count_);
    auto at = std::find(items_.begin(), end, c);
    if (at == end) {
        // Not in the list: make room at the front, dropping the oldest when full.
        if (count_ < items_.size()) {
            ++count_;
        }
        at = items_.begin() + static_cast<std::ptrdiff_t>(count_ - 1);
    }
    std::move_backward(items_.begin(), at, at + 1);
    items_[0] = c;
}

} // namespace etude
