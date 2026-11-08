// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/melody_shape.hpp"

#include <algorithm>

namespace etude {

namespace {

void widen(MelodyHandRange& range, int pitch) {
    range.lowest = range.used() ? std::min(range.lowest, pitch) : pitch;
    range.highest = std::max(range.highest, pitch);
}

} // namespace

MelodyShape melody_shape(const Song& song) {
    MelodyShape shape;
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment) {
            continue;
        }
        widen(n.hand == Hand::Left ? shape.left : shape.right, n.pitch);
    }
    return shape;
}

} // namespace etude
