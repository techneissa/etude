// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sections.hpp"

#include <algorithm>

namespace etude {

namespace {

// The largest step of an auto speed-up policy.
constexpr int kMaxSpeedStepPercent = 25;

// ---- Text

struct Decoded {
    std::uint32_t cp = 0;
    std::size_t len = 0; // 0 when the bytes at this position are not a valid character
};

Decoded decode_utf8(std::string_view s, std::size_t i) {
    auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        return {b0, 1};
    }
    std::size_t extra = 0;
    std::uint32_t cp = 0;
    std::uint32_t min = 0;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        extra = 1;
        cp = b0 & 0x1F;
        min = 0x80;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        extra = 2;
        cp = b0 & 0x0F;
        min = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        extra = 3;
        cp = b0 & 0x07;
        min = 0x10000;
    } else {
        return {};
    }
    if (i + extra >= s.size()) {
        return {};
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) {
            return {};
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return {};
    }
    return {cp, extra + 1};
}

bool is_space_like(std::uint32_t cp) {
    return cp <= 0x20 || (cp >= 0x7F && cp <= 0xA0) || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
           cp == 0x205F || cp == 0x3000;
}

bool is_invisible(std::uint32_t cp) {
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
           (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF || cp == 0xFFFE || cp == 0xFFFF;
}

} // namespace

std::string sanitize_section_name(std::string_view text) {
    std::string out;
    bool pending_space = false;
    for (std::size_t i = 0; i < text.size();) {
        Decoded d = decode_utf8(text, i);
        if (d.len == 0) {
            ++i; // an invalid byte is dropped
            continue;
        }
        std::string_view bytes = text.substr(i, d.len);
        i += d.len;
        if (is_space_like(d.cp)) {
            pending_space = !out.empty();
            continue;
        }
        if (is_invisible(d.cp)) {
            continue;
        }
        std::size_t needed = bytes.size() + (pending_space ? 1 : 0);
        if (out.size() + needed > kMaxSectionNameBytes) {
            break;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.append(bytes);
    }
    return out;
}

PlanItem sanitized(const PlanItem& item) {
    PlanItem out = item;
    out.repeats = std::clamp(out.repeats, 0, kMaxPlanRepeats);
    out.speed_up.start_percent = snap_speed_percent(out.speed_up.start_percent);
    // Clamp before rounding: adding to a huge value would overflow.
    const int step = std::clamp(out.speed_up.step_percent, kSpeedStepPercent, kMaxSpeedStepPercent);
    out.speed_up.step_percent =
        (step + kSpeedStepPercent / 2) / kSpeedStepPercent * kSpeedStepPercent;
    out.speed_up.max_percent =
        std::max(snap_speed_percent(out.speed_up.max_percent), out.speed_up.start_percent);
    return out;
}

} // namespace etude
