// SPDX-License-Identifier: GPL-3.0-or-later
// The hit window's quick choices. The slider on the practice page sets any value from
// kMinHitWindowMs to kMaxHitWindowMs; Relaxed, Normal and Strict are three values on it. A window
// that equals one of them shows that choice selected; any other value shows none.
#pragma once

#include <array>
#include <cstddef>

namespace etude {

inline constexpr int kHitWindowRelaxedMs = 150;
inline constexpr int kHitWindowNormalMs = 80; // also AppSettings' default
inline constexpr int kHitWindowStrictMs = 40;

// Strictest first, the order the segmented control shows them.
enum class HitWindowPreset { Strict, Normal, Relaxed };
inline constexpr int kHitWindowPresetCount = 3;

inline constexpr std::array<int, kHitWindowPresetCount> kHitWindowPresetMs = {
    kHitWindowStrictMs, kHitWindowNormalMs, kHitWindowRelaxedMs};

inline constexpr int hit_window_ms(HitWindowPreset p) {
    return kHitWindowPresetMs[static_cast<std::size_t>(p)];
}

// The index (HitWindowPreset) of the preset whose window is exactly `ms`, or -1 for none.
inline constexpr int hit_window_preset_index(int ms) {
    for (int i = 0; i < kHitWindowPresetCount; ++i) {
        if (kHitWindowPresetMs[static_cast<std::size_t>(i)] == ms) return i;
    }
    return -1;
}

} // namespace etude
