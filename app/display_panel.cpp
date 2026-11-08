// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_panel.hpp"
#include "log_ring.hpp"

#include "panel_probe.hpp"
#include "ui.hpp"

#include "etude/screen_layout.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

namespace etude::app {
namespace {

DisplayPixels display_pixels(SDL_Window* window, SDL_DisplayID* id) {
    DisplayPixels out;
    *id = window != nullptr ? SDL_GetDisplayForWindow(window) : 0;
    if (*id == 0) {
        return out;
    }
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(*id);
    if (mode == nullptr || mode->w <= 0 || mode->h <= 0) {
        return out;
    }
    const float density = mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
    out.w = static_cast<float>(mode->w) * density;
    out.h = static_cast<float>(mode->h) * density;
    out.points_w = static_cast<float>(mode->w);
    return out;
}

} // namespace

void DisplayPanel::resolve(SDL_Window* window, std::optional<PanelMm> flag, bool headless) {
    SDL_DisplayID id = 0;
    scale_ = {};
    scale_.headless = headless;
    scale_.display = headless ? DisplayPixels{} : display_pixels(window, &id);
    source_ = PanelSource::None;
    detail_.clear();
    if (flag) {
        // The flag gets the same dpi bound as the system's report and the EDID (`plausible_panel_mm`):
        // a size in the wrong unit would make every control huge or tiny.
        float px_w = scale_.display.w;
        float px_h = scale_.display.h;
        if (window != nullptr && (headless || !(px_w > 0.0f))) {
            int pw = 0;
            int ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            px_w = static_cast<float>(pw);
            px_h = static_cast<float>(ph);
        }
        const std::optional<PanelMm> checked = plausible_panel_mm(*flag, px_w, px_h);
        if (checked || !(px_w > 0.0f) || !(px_h > 0.0f)) {
            scale_.mm = checked ? *checked : *flag; // no pixel size to judge it by: take it
            source_ = PanelSource::Flag;
            return;
        }
        log_printf(
                     "--panel-mm %.0fx%.0f rejected: not a believable size for a %.0fx%.0f pixel "
                     "display (%.0f to %.0f dpi); using the display's own report\n",
                     static_cast<double>(flag->w), static_cast<double>(flag->h),
                     static_cast<double>(px_w), static_cast<double>(px_h),
                     static_cast<double>(kLeastPanelDpi), static_cast<double>(kMostPanelDpi));
    }
    if (headless || !(scale_.display.w > 0.0f)) {
        return;
    }
    const std::optional<ProbedPanel> found = probe_system_panel(
        id, static_cast<int>(scale_.display.w), static_cast<int>(scale_.display.h));
    if (found) {
        scale_.mm = found->mm;
        source_ = found->source;
        detail_ = found->detail;
    }
}

std::string DisplayPanel::describe() const {
    char buf[240];
    if (!scale_.mm.known()) {
        std::snprintf(buf, sizeof buf,
                      "display size: not known, assuming 96 dpi (set --panel-mm=WxH, or the screen "
                      "size in Settings > View)");
        return buf;
    }
    std::snprintf(buf, sizeof buf, "display size: %.0f x %.0f mm from %s%s%s",
                  static_cast<double>(scale_.mm.w), static_cast<double>(scale_.mm.h),
                  panel_source_name(source_), detail_.empty() ? "" : " ", detail_.c_str());
    return buf;
}

namespace {
float g_frame_px_per_mm = 0.0f; // set_display_px_per_mm
} // namespace

void set_display_px_per_mm(float px_per_mm) { g_frame_px_per_mm = px_per_mm; }

float display_px_per_mm(SDL_Window* window) {
    if (g_frame_px_per_mm > 0.0f) {
        return g_frame_px_per_mm;
    }
    float px_per_mm = kDefaultPxPerMm;
    if (window != nullptr) {
        const float density = SDL_GetWindowPixelDensity(window);
        if (density > 0.0f) {
            px_per_mm = density * kDefaultPxPerMm;
        }
    }
    return px_per_mm;
}

} // namespace etude::app
