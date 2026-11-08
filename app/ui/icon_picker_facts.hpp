// SPDX-License-Identifier: GPL-3.0-or-later
// The words beside the About page's icon colours: how many medals the device's best profile holds,
// what the next title asks for and the colour that title unlocks. Pure, so a test holds the wording;
// the page makes them again only when the medals change, never per frame.
#pragma once

#include "about_layout.hpp"

#include "etude/icon_colour.hpp"
#include "etude/medals.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace etude::app::ui {

struct IconPickerFacts {
    int title = 0; // 1 .. 8; 0: not known yet, nothing is shown
    std::string title_name; // "Pianist"
    std::string medals;     // "72 of 192 medals"
    std::string next;       // "Next: Pianist at 96", or "Top title reached"
    std::string unlock;     // the colour the next title unlocks ("Midnight"); empty at the top
    int unlock_index = -1;  // its place in kIconColours; -1 at the top
    // The three lines beside the colours: the medals, the next title, "Unlocks Midnight" (empty at the top).
    std::array<std::string, 3> side;
    // The same in one line, fullest first, for a window too narrow to have them beside the colours.
    std::array<std::string, 4> line;
};

// `points` is the most medals any of the device's profiles holds; the title follows from it.
inline IconPickerFacts icon_picker_facts(int points) {
    IconPickerFacts f;
    f.title = etude::title_for_points(points);
    f.title_name = std::string(etude::tier_title(f.title));
    f.medals = std::to_string(points) + " of " + std::to_string(etude::kMaxMedalPoints) + " medals";
    const std::string dot = " \xC2\xB7 ";
    if (f.title < etude::kTierCount) {
        const int next = f.title + 1;
        f.next = "Next: " + std::string(etude::tier_title(next)) + " at " +
                 std::to_string(etude::title_points(next));
        f.unlock_index = f.title; // the colours run in the titles' order, title 2's at index 1
        f.unlock = std::string(etude::kIconColours[static_cast<std::size_t>(f.unlock_index)].name);
        const std::string unlocks = " \xC2\xB7 unlocks " + f.unlock;
        f.line = {f.medals + dot + f.next + unlocks, f.next + unlocks, f.medals + dot + f.next, f.next};
    } else {
        f.next = "Top title reached";
        f.line = {f.medals + dot + f.next, f.next, f.medals, f.medals};
    }
    f.side = {f.medals, f.next, f.unlock.empty() ? "" : "Unlocks " + f.unlock};
    return f;
}

// What the line under the colours says about a colour that is still locked.
inline std::string icon_unlock_hint(const etude::IconColourInfo& colour) {
    return std::string(colour.name) + " unlocks at " + std::string(etude::tier_title(colour.title));
}

// `texts`, fullest first, said in a room `room` wide by `width(text)`: the first one that fits on
// one line, else the first that wraps into two lines that each fit, else the last one's lines as
// they wrap (the caller cuts what is still wider, which needs a room under a word's width).
template <class Width>
std::vector<std::string> icon_picker_lines(const std::vector<std::string>& texts, float room,
                                           const Width& width) {
    for (const std::string& text : texts) {
        if (width(text) <= room) {
            return {text};
        }
    }
    for (const std::string& text : texts) {
        std::vector<std::string> lines = wrap_words(text, room, 2, width);
        if (std::all_of(lines.begin(), lines.end(),
                        [&](const std::string& line) { return width(line) <= room; })) {
            return lines;
        }
    }
    return wrap_words(texts.back(), room, 2, width);
}

} // namespace etude::app::ui
