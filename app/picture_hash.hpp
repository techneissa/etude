// SPDX-License-Identifier: GPL-3.0-or-later
// The running hash of a frame's picture (frame_picture.hpp mixes the drawing into it). No SDL here,
// so the idle loop can be tested.
#pragma once

#include <cmath>
#include <cstdint>

namespace etude::app::picture {

inline bool g_on = false;
inline std::uint64_t g_hash = 0;

inline void begin(bool on) {
    g_on = on;
    g_hash = 0xcbf29ce484222325ULL;
}
inline bool on() { return g_on; }
inline std::uint64_t take() { return g_hash; }

inline void mix(std::uint64_t v) {
    g_hash = (g_hash ^ v) * 0x100000001b3ULL;
    g_hash ^= g_hash >> 29;
}
inline std::uint64_t pos(float v) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lrintf(v * 16.0f)));
}
inline std::uint64_t unit(float v) {
    return static_cast<std::uint64_t>(std::lrintf(v * 255.0f));
}
inline std::uint64_t ptr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

} // namespace etude::app::picture
