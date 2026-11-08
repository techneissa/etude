// SPDX-License-Identifier: GPL-3.0-or-later
// What the status text in the top row is made of: the notes after the keyboard's name, each a part
// with a drop rank, so a narrow row gives up the least important parts whole, in a fixed order,
// instead of cutting the line mid-word (app/ui/status_fit.hpp does the fitting). Plain C++, no UI.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace etude::app {

// The order the notes go in as the room shrinks, first to go first. The keyboard's name outranks
// all of them: it is the last thing the row gives up.
//
//   the effects line (developer), the song's sound, the combo, the hands, the mode, the speed, the
//   state (paused, ready, waiting, end), the sheet's bar number, the loop, the transposition, the
//   bar count, the song's title, the practice plan's words, the dropped-MIDI warning, the
//   out-of-range warning.
//
// What the bar's own buttons show (sound, hands, mode, speed, state) goes before what only the line
// says (the bar, the loop, a transposition). The title is cut with an ellipsis before it goes, and
// is the last of the song's line to go.
enum class StatusRank : std::uint8_t {
    Effects,
    Sound,
    Combo,
    Hands,
    Mode,
    Speed,
    State,
    SheetBar,
    Loop,
    Transpose,
    Bar,
    Title,
    Plan,
    Dropped,
    OutOfRange,
};

// One note of the status text. Parts with the same `group` in a row read as one line, joined with a
// middle dot ("Title · Flow · Both hands"); another group starts after a wider gap.
struct StatusPart {
    std::string_view text;
    StatusRank rank = StatusRank::Title;
    std::uint8_t group = 0;
};

inline constexpr int kMaxStatusParts = 24;

// The parts of a frame's status text, in the order they read left to right.
struct StatusParts {
    std::array<StatusPart, kMaxStatusParts> part{};
    int count = 0;

    // Adds a part unless its text is empty or the list is full.
    void add(std::string_view text, StatusRank rank, std::uint8_t group) {
        if (!text.empty() && count < kMaxStatusParts) {
            part[static_cast<std::size_t>(count++)] = {text, rank, group};
        }
    }
};

} // namespace etude::app
