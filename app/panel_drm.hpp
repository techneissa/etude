// SPDX-License-Identifier: GPL-3.0-or-later
// The display's physical size from the DRM connectors' EDID under /sys/class/drm (the Pi's KMSDRM
// display), read once at start and never per frame. No SDL, so a test can run it on a fixture folder.
#pragma once

#include "etude/panel_size.hpp"

#include <optional>
#include <string>

namespace etude::app {

struct ProbedPanel {
    PanelMm mm;
    PanelSource source = PanelSource::None;
    std::string detail; // the connector's name, for the log
};

// The size the connected connectors under `root` (`/sys/class/drm`) report in their EDID. With
// several connected, the one whose first mode is `px_w` x `px_h` wins, else the first. A connector
// with no EDID (a DSI panel), no size or a size that does not fit the pixels is skipped. Nothing
// when no connector gives a believable size for `px_w` x `px_h` pixels.
std::optional<ProbedPanel> probe_drm_panel(const std::string& root, int px_w, int px_h);

} // namespace etude::app
