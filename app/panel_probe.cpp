// SPDX-License-Identifier: GPL-3.0-or-later
#include "panel_probe.hpp"

#include "panel_drm.hpp"

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace etude::app {

std::optional<ProbedPanel> probe_system_panel(std::uint32_t display, int px_w, int px_h) {
#ifdef __APPLE__
    // SDL's display ids on macOS are the CGDirectDisplayIDs.
    const CGSize size = CGDisplayScreenSize(static_cast<CGDirectDisplayID>(display));
    const std::optional<PanelMm> mm = plausible_panel_mm(
        {static_cast<float>(size.width), static_cast<float>(size.height)},
        static_cast<float>(px_w), static_cast<float>(px_h));
    if (!mm) {
        return std::nullopt;
    }
    return ProbedPanel{*mm, PanelSource::System, "CGDisplayScreenSize"};
#else
    (void)display;
    return probe_drm_panel("/sys/class/drm", px_w, px_h);
#endif
}

} // namespace etude::app
