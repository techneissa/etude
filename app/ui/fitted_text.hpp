// SPDX-License-Identifier: GPL-3.0-or-later
// A string measured and fitted to a room, kept in a slot until what it was made from changes, so a
// page's frame costs a few comparisons and not a measuring of its text.
#pragma once

#include "kit.hpp"

#include <string>

namespace etude::app::ui {

// A string measured, and fitted to a room, kept until what it was made from changes (the string, its
// font, its size or the room): a frame costs a few comparisons, not a measuring of the text.
struct Fitted {
    std::string from;
    ImFont* font = nullptr;
    float size = 0.0f;
    float room = -1.0f;
    float natural = 0.0f; // the width of `from` on one line
    std::string out;      // `from` fitted to `room`
};

inline const Fitted& measured(Fitted& slot, ImFont* font, float size, const std::string& text,
                       float room) {
    if (slot.font != font || slot.size != size || slot.room != room || slot.from != text) {
        slot.font = font;
        slot.size = size;
        slot.room = room;
        slot.from = text;
        slot.natural = label_width(font, size, text.c_str());
        slot.out = fit(font, size, text, room);
    }
    return slot;
}

} // namespace etude::app::ui
