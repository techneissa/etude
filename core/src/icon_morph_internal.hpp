// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of icon_morph.cpp share. Not part of the app's interface.
#pragma once

// Morphing stroke icons, after the technique of morphicons (https://github.com/guillermolg00/
// morphicons, MIT License, Copyright (c) 2026 Guillermo). A port of the technique, not the code.
#include "etude/icon_morph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace etude {

namespace morph_detail {

inline constexpr double kPi = 3.14159265358979323846;

inline MorphPoint lerp(MorphPoint a, MorphPoint b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

inline MorphPoint rotate(MorphPoint p, float c, float s) { return {c * p.x - s * p.y, s * p.x + c * p.y}; }

} // namespace morph_detail

} // namespace etude
