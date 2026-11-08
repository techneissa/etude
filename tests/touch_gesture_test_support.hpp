// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the touch_gesture_*test.cpp files (split from touch_gesture_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/touch_gesture.hpp"
#include "etude/finger_claims.hpp"
#include "etude/finger_count.hpp"
#include "etude/handle_grab.hpp"
#include "etude/touch_kind.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace touch_gesture_test_support {

using namespace etude;
using Kind = GestureRecognizer::Event::Kind;

inline std::vector<GestureRecognizer::Event> drain(GestureRecognizer& g) {
    std::vector<GestureRecognizer::Event> out(g.events().begin(), g.events().end());
    g.clear_events();
    return out;
}

inline std::vector<Kind> kinds(const std::vector<GestureRecognizer::Event>& events) {
    std::vector<Kind> out;
    for (const auto& e : events) {
        out.push_back(e.kind);
    }
    return out;
}

inline int count_of(const std::vector<GestureRecognizer::Event>& events, Kind k) {
    int n = 0;
    for (const auto& e : events) {
        n += e.kind == k ? 1 : 0;
    }
    return n;
}

} // namespace touch_gesture_test_support
