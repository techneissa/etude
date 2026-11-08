// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace etude {

inline constexpr float kMmPerInch = 25.4f;
// Pixels per mm of a 96 dpi screen: what the UI assumes when nothing says better. A design point
// is a pixel of such a screen: the UI scales it (`ui_scale`) so it is 1/96 inch on the panel's real
// size too.
inline constexpr float kFallbackPxPerMm = 96.0f / kMmPerInch;

// Millimetres in design points, rounded up to a whole point.
constexpr float pt_from_mm_up(float mm) {
    const float pt = mm * kFallbackPxPerMm;
    const float whole = static_cast<float>(static_cast<int>(pt));
    return whole < pt ? whole + 1.0f : whole;
}

// The touch rules in real millimetres. Every control has a hit area of at least `kMinHitMm` in both
// directions (what is drawn may be smaller); two hit areas are never closer than `kMinHitGapMm`
// and never overlap; a press that stays within `kTapSlopMm` of where it landed is still a tap.
inline constexpr float kMinHitMm = 12.0f;
inline constexpr float kMinHitGapMm = 2.0f;
inline constexpr float kTapSlopMm = 2.5f;
// The same in design points, whole: 46 and 8 (12.2 mm and 2.1 mm on every screen).
inline constexpr float kMinHitPt = pt_from_mm_up(kMinHitMm);
inline constexpr float kMinHitGapPt = pt_from_mm_up(kMinHitGapMm);

// A panel size that makes the screen less dense than this, or denser than the upper bound, is a
// wrong report (a projector's 16:9 placeholder, a size in the wrong unit), not a real panel.
inline constexpr float kLeastPanelDpi = 40.0f;
inline constexpr float kMostPanelDpi = 600.0f;

// A panel's physical size in millimetres. Zero in either direction: not known.
struct PanelMm {
    float w = 0.0f;
    float h = 0.0f;
    bool known() const { return w > 0.0f && h > 0.0f; }
    friend bool operator==(const PanelMm&, const PanelMm&) = default;
};

// Where the size in force came from (logged once at start).
enum class PanelSource : std::uint8_t {
    None,    // nothing known: 96 dpi
    Flag,    // --panel-mm=WxH
    Setting, // the Settings override (a width in mm)
    Edid,    // the display's EDID (the Pi's DRM connector)
    System,  // the operating system (the Mac's CGDisplayScreenSize)
};
const char* panel_source_name(PanelSource source);

// "344x194", "344 x 194.5", "344X194" or "344×194": the width and the height, each 20 to 5000 mm.
std::optional<PanelMm> parse_panel_mm(std::string_view text);

// The size a display's EDID block (at least 128 bytes) reports: the first detailed timing's image
// size when it has one (millimetre exact), else the basic block's centimetres. A block that is not
// an EDID, or reports no size (a projector: 0 or an aspect ratio in the centimetre bytes), gives
// nothing.
std::optional<PanelMm> panel_mm_from_edid(const std::uint8_t* data, std::size_t size);

// Whether `mm` is a believable size for a screen of `px_w` by `px_h` pixels: its density is
// between `kLeastPanelDpi` and `kMostPanelDpi` in both directions and the pixels and millimetres
// have about the same shape (within a fifth). A portrait size on a landscape screen (or the
// reverse) is turned round first, so the result may be `mm` swapped.
std::optional<PanelMm> plausible_panel_mm(PanelMm mm, float px_w, float px_h);

// Pixels per millimetre of a screen `px_w` by `px_h` pixels that is `mm` large: the mean of the two
// directions. 0 for a size not known or a bad pixel count.
float panel_px_per_mm(float px_w, float px_h, PanelMm mm);

// The panel for a width alone (the Settings override): the height follows the pixels' shape.
PanelMm panel_mm_from_width(float width_mm, float px_w, float px_h);

} // namespace etude
