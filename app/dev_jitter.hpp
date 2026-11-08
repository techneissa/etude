// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <random>

namespace etude::app {

// A random sleep of 0..max_ms, for --dev-frame-jitter-ms: it makes the frame time vary so the
// song's timing can be seen to hold. The same sequence every run.
inline int dev_jitter_ms(int max_ms) {
    static std::mt19937 rng(20261012);
    return std::uniform_int_distribution<int>(0, max_ms)(rng);
}

} // namespace etude::app
