// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/panel_size.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace etude {
namespace {

constexpr float kLeastSideMm = 20.0f;
constexpr float kMostSideMm = 5000.0f;
constexpr float kShapeTolerance = 0.2f; // pixels' and millimetres' aspect ratios may differ so much
constexpr std::size_t kEdidBlock = 128;
constexpr std::size_t kFirstTiming = 54; // the first detailed timing descriptor

bool side_ok(float mm) { return std::isfinite(mm) && mm >= kLeastSideMm && mm <= kMostSideMm; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' ||
                          s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

bool read_number(std::string_view s, float* out) {
    s = trim(s);
    if (s.empty()) {
        return false;
    }
    const std::string copy(s); // strtof wants a terminated string
    char* end = nullptr;
    const float v = std::strtof(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

} // namespace

const char* panel_source_name(PanelSource source) {
    switch (source) {
    case PanelSource::Flag:
        return "--panel-mm";
    case PanelSource::Setting:
        return "the screen size setting";
    case PanelSource::Edid:
        return "the display's EDID";
    case PanelSource::System:
        return "the system";
    case PanelSource::None:
        break;
    }
    return "nothing (96 dpi)";
}

std::optional<PanelMm> parse_panel_mm(std::string_view text) {
    text = trim(text);
    std::size_t at = text.find_first_of("xX");
    std::size_t skip = 1;
    if (at == std::string_view::npos) {
        at = text.find("\xC3\x97"); // ×
        skip = 2;
    }
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    PanelMm mm;
    if (!read_number(text.substr(0, at), &mm.w) || !read_number(text.substr(at + skip), &mm.h) ||
        !side_ok(mm.w) || !side_ok(mm.h)) {
        return std::nullopt;
    }
    return mm;
}

std::optional<PanelMm> panel_mm_from_edid(const std::uint8_t* data, std::size_t size) {
    static constexpr std::uint8_t kHeader[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (data == nullptr || size < kEdidBlock) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < sizeof kHeader; ++i) {
        if (data[i] != kHeader[i]) {
            return std::nullopt;
        }
    }
    // The first detailed timing: a pixel clock, then the image size in mm (bytes 12 and 13 hold the
    // low eight bits, byte 14 the high four of each).
    const std::uint8_t* t = data + kFirstTiming;
    if (t[0] != 0 || t[1] != 0) {
        PanelMm mm;
        mm.w = static_cast<float>(t[12] | ((t[14] >> 4) << 8));
        mm.h = static_cast<float>(t[13] | ((t[14] & 0x0F) << 8));
        if (mm.known()) {
            return mm;
        }
    }
    // The basic block's size in centimetres; a zero in either is a projector or an aspect ratio.
    const std::uint8_t cm_w = data[21];
    const std::uint8_t cm_h = data[22];
    if (cm_w > 0 && cm_h > 0) {
        return PanelMm{static_cast<float>(cm_w) * 10.0f, static_cast<float>(cm_h) * 10.0f};
    }
    return std::nullopt;
}

std::optional<PanelMm> plausible_panel_mm(PanelMm mm, float px_w, float px_h) {
    if (!mm.known() || !(px_w > 0.0f) || !(px_h > 0.0f) || !std::isfinite(mm.w) ||
        !std::isfinite(mm.h)) {
        return std::nullopt;
    }
    if ((px_w >= px_h) != (mm.w >= mm.h)) {
        mm = {mm.h, mm.w};
    }
    const float dpi_w = px_w / (mm.w / kMmPerInch);
    const float dpi_h = px_h / (mm.h / kMmPerInch);
    if (dpi_w < kLeastPanelDpi || dpi_w > kMostPanelDpi || dpi_h < kLeastPanelDpi ||
        dpi_h > kMostPanelDpi) {
        return std::nullopt;
    }
    const float shape = (px_w / px_h) / (mm.w / mm.h);
    if (shape < 1.0f - kShapeTolerance || shape > 1.0f + kShapeTolerance) {
        return std::nullopt;
    }
    return mm;
}

float panel_px_per_mm(float px_w, float px_h, PanelMm mm) {
    if (!mm.known() || !(px_w > 0.0f) || !(px_h > 0.0f)) {
        return 0.0f;
    }
    return 0.5f * (px_w / mm.w + px_h / mm.h);
}

PanelMm panel_mm_from_width(float width_mm, float px_w, float px_h) {
    if (!(width_mm > 0.0f) || !(px_w > 0.0f) || !(px_h > 0.0f)) {
        return {};
    }
    return {width_mm, width_mm * px_h / px_w};
}

} // namespace etude
