// SPDX-License-Identifier: GPL-3.0-or-later
// Output pixels per millimetre, from the panel's physical size (display_panel.hpp reads it once).
// No SDL, so a test can run the maths.
#pragma once

#include "etude/panel_size.hpp"

namespace etude::app {

// The pixel size of the display a window is on, in the display's own pixels (its mode in points
// times its pixel density); zeros when SDL knows no mode (a hidden window with no display).
struct DisplayPixels {
    float w = 0.0f;
    float h = 0.0f;
    float points_w = 0.0f; // the mode's width in points: what a window's points are cut from
};

struct PanelScale {
    PanelMm mm;                // the panel's size, as read or as the flag says; not known: none
    DisplayPixels display;     // the display the window is on
    bool headless = false;     // drawn into an offscreen target: the window stands for the panel

    // Output pixels per millimetre for a window of `out_w` x `out_h` output pixels, `win_density`
    // output pixels per point. `setting_width_mm` is the Settings override (under
    // `kMinScreenWidthMm`: none; a headless run ignores it). `taste` is the UI scale multiplied on
    // top; it never goes under 1,
    // so no control gets under its size in millimetres. With no size known: 96 dpi times the
    // window's pixel density.
    float px_per_mm(float out_w, float out_h, float win_density, int setting_width_mm,
                    float taste) const;
};

} // namespace etude::app
