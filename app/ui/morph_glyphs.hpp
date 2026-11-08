// SPDX-License-Identifier: GPL-3.0-or-later
// The drawn icons (Lucide's path data, ISC licence, and the app's own hands), ready to morph. No
// ImGui here, so the tests can check every one.
#pragma once

#include "etude/icon_morph.hpp"

#include <cstddef>
#include <cstdint>

namespace etude::app::ui {

enum class MorphGlyph : std::uint8_t {
    Play,
    Pause,
    Mute,       // volume-x
    SoundOther, // volume-1
    SoundBoth,  // volume-2
    Settings,
    Close,
    LeftHand,  // open, seen from the back as a player sees it: the thumb on its right, inward
    RightHand, // the same, mirrored: the thumb on its left
    Chevron,   // chevron-right; a group's open state turns it a quarter, never morphs it
    BarStrip,  // the bar styles: the screen with the bar's layout drawn in its top part
    BarGroups,
    BarDock,
    Pinned,   // pin: the bar is kept shown
    Unpinned, // pin-off: it hides after a while; morphs to and from Pinned
    Replay,   // play it again: Lucide's rotate-ccw with a play head in the ring
    // The accompaniment button: two music notes (the song's other instruments) or, when every
    // accompaniment part is a voice, audio lines; each sounding, with a half slash (some parts
    // muted) and with the full slash (all muted). A microphone would read as the input source.
    Ensemble,
    EnsemblePartial,
    EnsembleOff,
    Voice,
    VoicePartial,
    VoiceOff,
    ChevronDown, // the accompaniment button's menu: closed (down), and open (up)
    ChevronUp,
    // The side sheet-music button, the note area drawn small: falling notes only (Off), a staff
    // above falling notes (On), and a staff over the whole area (Full). They morph into each other.
    ViewOff,
    ViewOn,
    ViewFull,
    Count,    // no icon
};

struct GlyphStyle {
    float stroke = 2.0f; // in grid units (Lucide's 2 on a 24-unit grid)
    float scale = 1.0f;  // drawn this much larger than the size asked for
};

// The icon's curves and samples, built on first use. Count gives an empty icon. A shipped glyph
// that fails to parse asserts in a debug build.
const MorphIconData& morph_data(MorphGlyph glyph);
// The skip buttons' step as drawn digits, `seconds` clamped to kSkipMin..kSkipMax. In the 24-unit
// grid, centred on (12, 12); the number inside a skip arrow rolls from one step to the next.
inline constexpr int kSkipMin = 1;
inline constexpr int kSkipMax = 30;
inline constexpr float kNumberStroke = 1.8f; // the digits' line, in grid units
// A rolling number moves this many grid units for one unit of its offset: a short slide, so the
// numbers stay whole (a digit is 10 tall) and the window only trims the far end of one that is
// nearly gone. The window sits within the ring: half a width and half a height, in grid units.
inline constexpr float kNumberTravel = 6.0f;
struct NumberWindow {
    float x;
    float y;
};
inline constexpr NumberWindow kNumberWindow{7.4f, 7.2f};
const MorphIconData& number_data(int seconds);
// A real glyph that parsed: only these are drawn.
bool glyph_ok(MorphGlyph glyph);
GlyphStyle glyph_style(MorphGlyph glyph);
// Half the ink's width and height of a glyph at rest, in grid units, its stroke included (before
// `GlyphStyle::scale`). The ink is centred on (12, 12), so these are its reach on each side.
struct GlyphExtent {
    float half_w = 0.0f;
    float half_h = 0.0f;
};
GlyphExtent glyph_extent(MorphGlyph glyph);

// Drawing flattens each cubic in at most kMaxCurveSteps steps into a buffer of kMaxCurvePoints
// points per subpath.
inline constexpr int kMaxCurveSteps = 24;
inline constexpr int kMaxCurvePoints = 1024;

} // namespace etude::app::ui
