// SPDX-License-Identifier: GPL-3.0-or-later
// The reset button of a settings slider or stepper, with no ImGui in it so tests/reset_to_test.cpp
// checks the rules.
//
// Rules:
// - A slider or a stepper row that is given its default always has a small reset button at its
//   trailing end. While the value is the default it is greyed and does nothing: it is not a
//   pressable item (a tap on it changes nothing and the touch audit does not count it), but it keeps
//   its place and its 12 mm and 2 mm, so the row looks the same in both states and nothing moves
//   when it lights up. Once the value differs it is active, and a tap puts the default back. Switches
//   and segmented rows have none.
// - The default is the setting's own: the row is given it from the one place the setting defines it
//   (the settings struct's member initialiser), never a second copy of the number.
// - A slider's value is a float that a drag leaves a hair off its default, so "differs" is more than
//   `kResetSlack` of the slider's range; a whole-number value differs when it is not equal.
// - A reset changes only the setting it belongs to (`ResetTo::target`): the Opacity row's reset puts
//   the strength back and leaves Grade words as it is.
// - Putting the default back is one edit: the toast says so and its Undo (the toast's one Undo path,
//   `Kit::say_undoable`) puts the exact earlier value back, in the setting itself, so it works
//   whether or not the row is still drawn (the sheet may have closed). The Undo holds the setting's
//   address: the settings live in the app's one `UiModel` for as long as the app runs.
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>

namespace etude::app::ui {

// How far from its default a float value may sit, as a share of its range, and still count as at
// the default (a drag that stops on the default lands a rounding off it).
inline constexpr float kResetSlack = 1e-3f;

// The button is active (not greyed) while the value differs from the default.
constexpr bool reset_active(int value, int def) { return value != def; }

inline bool reset_active(float value, float def, float range) {
    return std::fabs(value - def) > kResetSlack * std::fabs(range);
}

// What a row is given to reset: the setting's default, and the setting itself when the row shows it
// another way (the Opacity row shows 0 while the words are off, but its reset is the strength). A
// null `target` is the row's own value.
template <class T>
struct ResetTo {
    T def{};
    T* target = nullptr;
    ResetTo(T d) : def(d) {} // NOLINT: a row is given its default as it is
    ResetTo(T d, T* t) : def(d), target(t) {}
};

// A tap on the reset button of a whole-number setting: the default goes in. Nothing happens while
// the value is the default. Returns the earlier value, for the Undo; empty when nothing changed.
inline std::optional<int> tap_reset(int* target, int def) {
    if (!reset_active(*target, def)) {
        return std::nullopt;
    }
    const int prior = *target;
    *target = def;
    return prior;
}

// The same for a float setting with its slider's `range`.
inline std::optional<float> tap_reset(float* target, float def, float range) {
    if (!reset_active(*target, def, range)) {
        return std::nullopt;
    }
    const float prior = *target;
    *target = def;
    return prior;
}

// The toast's Undo of a reset (`Kit::say_undoable`): the exact earlier value goes back into the
// setting itself, so it works whether or not the row is still drawn. It holds the setting's address
// and the value, nothing of a row.
template <class T>
std::function<void()> undo_of(T* target, T prior) {
    return [target, prior] { *target = prior; };
}

} // namespace etude::app::ui
