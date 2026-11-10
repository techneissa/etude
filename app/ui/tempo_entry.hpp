// SPDX-License-Identifier: GPL-3.0-or-later
// The typed tempo: which whole numbers of beats a minute the song's speed range allows, and what
// to say of a number typed. No ImGui here.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace etude::app::ui {

struct TempoRange {
    int low = 1;
    int high = 1;
};

// The whole tempos whose speed is inside the range `low`..`high` (beats a minute): the first at
// or above `low`, the last at or below `high`.
inline TempoRange tempo_range(double low, double high) {
    TempoRange r;
    r.low = std::max(1, static_cast<int>(std::ceil(low - 1e-9)));
    r.high = std::max(r.low, static_cast<int>(std::floor(high + 1e-9)));
    return r;
}

// How many digits the largest tempo has: the field takes no more.
inline int tempo_digits(const TempoRange& r) {
    int n = 1;
    for (int v = r.high; v >= 10; v /= 10) {
        ++n;
    }
    return n;
}

enum class TempoEntry : unsigned char {
    Empty,   // nothing typed
    TooLow,  // below the range
    TooHigh, // above it
    Ok,
};

inline TempoEntry check_tempo_entry(std::string_view digits, const TempoRange& r) {
    if (digits.empty()) {
        return TempoEntry::Empty;
    }
    long value = 0;
    for (const char c : digits) {
        value = value * 10 + (c - '0');
        if (value > 1'000'000) {
            break;
        }
    }
    if (value < r.low) {
        return TempoEntry::TooLow;
    }
    return value > r.high ? TempoEntry::TooHigh : TempoEntry::Ok;
}

} // namespace etude::app::ui
