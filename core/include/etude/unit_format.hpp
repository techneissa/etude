// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// The units the UI shows after a number. One format for all of them: the number, then the unit with
// no space ("60%", "80ms", "4.0s", "120BPM", "3×"), so a value reads the same on a slider, a
// stepper, a chip and a toast. `Count` is a plain number: how many of something the label names.
enum class Unit : std::uint8_t { Percent, Milliseconds, Seconds, Bpm, Times, Count };

// A formatted value, kept on the stack: the UI draws every frame and never allocates for a label.
struct UnitText {
    std::array<char, 24> text{};
    std::size_t length = 0;

    const char* c_str() const { return text.data(); }
    std::string_view view() const { return {text.data(), length}; }
};

// `value` rounded to `decimals` places (0 to 3), then the unit. A negative value starts with the
// true minus sign (U+2212), never "-0" for a value that rounds to zero; `plus` puts "+" before a
// positive one (the skip buttons). A value that is not finite, or a billion or more, reads "-" with
// no unit.
UnitText format_unit(double value, Unit unit, int decimals = 0, bool plus = false);

// The same as a string, for text built out of pieces (a status line, a hint, a toast).
inline std::string format_unit_string(double value, Unit unit, int decimals = 0,
                                      bool plus = false) {
    return std::string(format_unit(value, unit, decimals, plus).view());
}

} // namespace etude
