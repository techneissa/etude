// SPDX-License-Identifier: GPL-3.0-or-later
// The window's display, in real millimetres: what the UI's sizes are measured against. `resolve`
// reads the panel's physical size once at start (the --panel-mm flag, else the system's report,
// else nothing); `scale.px_per_mm` turns it into output pixels per millimetre every frame without
// touching the system again. The Settings override beats all of it, for a panel that reports the
// wrong size.
#pragma once

#include "panel_scale.hpp"

#include "etude/panel_size.hpp"

#include <SDL3/SDL_video.h>

#include <optional>
#include <string>

namespace etude::app {

class DisplayPanel {
  public:
    // At start. `flag` is --panel-mm; `headless` draws into an offscreen target the size of the
    // window, so the window stands for the whole panel.
    void resolve(SDL_Window* window, std::optional<PanelMm> flag, bool headless);
    // The text of the one start-up line: what the size is and where it came from.
    std::string describe() const;

    float px_per_mm(float out_w, float out_h, float win_density, int setting_width_mm,
                    float taste) const {
        return scale_.px_per_mm(out_w, out_h, win_density, setting_width_mm, taste);
    }
    PanelMm mm() const { return scale_.mm; }
    PanelSource source() const { return source_; }

  private:
    PanelScale scale_;
    PanelSource source_ = PanelSource::None;
    std::string detail_;
};

} // namespace etude::app
