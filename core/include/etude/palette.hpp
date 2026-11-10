// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "etude/color.hpp"
#include "etude/look.hpp"

namespace etude {

// The colour schemes' swatches and the curated palettes that fill them, the colour picker's
// arithmetic and the list of recent colours. Plain data: the Effects page draws it.
//
// A scheme has a fixed list of swatches, each one colour of the look:
// - ByHand: left hand, right hand, other notes (a note with no hand);
// - ByPitchClass: C, C#, D ... B;
// - ByOctave: octaves 0 to 8 (scientific numbering);
// - ByVelocity: soft, medium, hard (the ramp's stops);
// - Single: the one colour.
// A palette gives every scheme a full set of swatches. Applying one sets the current scheme's
// swatches and remembers the palette for that scheme (`LookSettings::palettes`), so a reset can go
// back to it after the player changed a swatch. The colours shown as a palette are the ones that
// match it exactly; colours that match no palette are "Custom".

inline constexpr std::array<Palette, 6> kAllPalettes{Palette::Classic, Palette::Vivid,
                                                     Palette::Pastel,  Palette::Ocean,
                                                     Palette::Sunset,  Palette::Mono};
inline constexpr std::array<ColorScheme, kColorSchemeCount> kAllColorSchemes{
    ColorScheme::ByHand, ColorScheme::ByPitchClass, ColorScheme::ByOctave, ColorScheme::ByVelocity,
    ColorScheme::Single};

// The name shown on its row, and the word it is saved as.
std::string_view palette_name(Palette p);

inline constexpr int kMaxSwatches = 12;

// A scheme's swatch colours, in swatch order.
struct Swatches {
    std::array<RGBA, kMaxSwatches> colors{};
    int count = 0;
    std::span<const RGBA> view() const {
        return {colors.data(), static_cast<std::size_t>(count)};
    }
    friend bool operator==(const Swatches&, const Swatches&) = default;
};

int swatch_count(ColorScheme scheme);
// The colour of swatch `index` in `look`; black for an index the scheme does not have.
RGBA swatch_color(const LookSettings& look, ColorScheme scheme, int index);
// Sets swatch `index` (opaque; the alpha is dropped). False, and nothing changed, for an index the
// scheme does not have.
bool set_swatch_color(LookSettings& look, ColorScheme scheme, int index, RGBA c);
Swatches scheme_swatches(const LookSettings& look, ColorScheme scheme);
// What palette `p` gives `scheme`. Classic is the default look's colours.
Swatches palette_swatches(Palette p, ColorScheme scheme);

// Sets the current scheme's swatches from `p` and remembers `p` for that scheme. The other
// schemes' colours stay.
void apply_palette(LookSettings& look, Palette p);
// The palette `scheme`'s colours in `look` are exactly, or none (Custom). Where two palettes give
// the same colours the one remembered for the scheme wins, then the first in `kAllPalettes`.
std::optional<Palette> matching_palette(const LookSettings& look, ColorScheme scheme);
// The current scheme's swatches back to the palette remembered for it.
void reset_to_palette(LookSettings& look);

std::span<const int> strip_swatches(ColorScheme scheme);

// --- Colour-blind check ------------------------------------------------------------------------

// --- Picker ------------------------------------------------------------------------------------

// The inline colour control: a square of saturation (left to right) and brightness (top to bottom,
// the top brightest) beside a hue bar, both drawn at full saturation and brightness, so every
// colour of the screen can be picked. HSV, kept as the control holds it: a grey or a black keeps
// the hue it had, so the hue bar does not jump when the square goes to a corner.
struct ColourPick {
    double hue = 0.0;        // degrees 0..360
    double saturation = 0.0; // 0..1
    double value = 1.0;      // 0..1
    friend bool operator==(const ColourPick&, const ColourPick&) = default;
};
// A colour as the control holds it (the hue of a grey or a black is 0), and back to the byte.
ColourPick pick_of(RGBA c);
// The pick for `c` when the control last held `previous`: a grey or a black has no hue of its own and
// keeps the one `previous` had (so the hue bar stays where it was); any other colour takes its own.
ColourPick pick_keeping_hue(RGBA c, const ColourPick& previous);
RGBA pick_color(const ColourPick& p);
// Positions on the hue bar (0..1, a full turn of hue) and in the square (x: saturation, y: one
// minus the value, so the top edge is the brightest).
double hue_position(const ColourPick& p);
double square_x(const ColourPick& p);
double square_y(const ColourPick& p);
// The pick with the hue at bar position `t`, or saturation and value at the square position
// (`x`, `y`); positions are clamped to 0..1, the other axes stay.
ColourPick with_hue_at(ColourPick p, double t);
ColourPick with_square_at(ColourPick p, double x, double y);

// --- Quick picks -------------------------------------------------------------------------------

// A grid of vivid, high-contrast colours, one tap to apply, laid out like a colour chart: eight hues
// across in colour-wheel order (red, orange, yellow, green, cyan, blue, purple, pink; yellow, green,
// cyan, blue are the pure ones), and down each column the shades from light to bright to deep, then
// a row of greys from white to black. Every colour appears once, and a neighbour in a row or a
// column is the next step of a smooth progression.
inline constexpr std::size_t kQuickColumns = 8;
inline constexpr std::size_t kQuickShades = 3;              // light, bright, deep
inline constexpr std::size_t kQuickRows = kQuickShades + 1; // and the greys
inline constexpr std::array<double, kQuickColumns> kQuickHues{0.0,   30.0,  60.0,  120.0,
                                                              180.0, 240.0, 280.0, 320.0};
inline constexpr double kQuickLightSaturation = 0.45; // the light row: the hue, half way to white
inline constexpr double kQuickDeepValue = 0.6;        // the deep row: the hue, darkened
using QuickPickGrid = std::array<std::array<RGBA, kQuickColumns>, kQuickRows>;
// `quick_picks()[row][column]`: rows 0 to 2 the shades, row 3 the greys.
const QuickPickGrid& quick_picks();

// --- Recent colours ----------------------------------------------------------------------------

// The last colours the player picked, newest first, at most `kRecentColours`. Adding one already
// in the list moves it to the front; past the limit the oldest goes. Opaque colours only.
inline constexpr std::size_t kRecentColours = 2 * kQuickColumns; // two rows of the quick picks' width
class RecentColours {
  public:
    void add(RGBA c);
    std::span<const RGBA> items() const { return {items_.data(), count_}; }
    std::size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    friend bool operator==(const RecentColours& a, const RecentColours& b) {
        return std::ranges::equal(a.items(), b.items());
    }

  private:
    std::array<RGBA, kRecentColours> items_{};
    std::size_t count_ = 0;
};

} // namespace etude
