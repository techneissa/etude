// SPDX-License-Identifier: GPL-3.0-or-later
// Where the display's physical size comes from, read once at start (never per frame): CoreGraphics
// on the Mac, the DRM connectors' EDID elsewhere (panel_drm.hpp). SDL reports no millimetres.
#pragma once

#include "panel_drm.hpp"

#include <cstdint>
#include <optional>

namespace etude::app {

// The size the system reports for display `display` (SDL's DisplayID, a CGDirectDisplayID on the
// Mac), `px_w` x `px_h` pixels. Nothing when it reports none or a size that does not fit the pixels.
std::optional<ProbedPanel> probe_system_panel(std::uint32_t display, int px_w, int px_h);

} // namespace etude::app
