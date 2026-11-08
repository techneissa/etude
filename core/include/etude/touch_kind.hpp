// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace etude {

// What kind of touch device a finger came from (SDL names the same four). A touch screen is
// Direct; a trackpad is Indirect and its fingers are not on the screen (its pointer events cover
// it); a device whose driver says nothing, as a Pi's panel may, is Unknown.
enum class TouchKind : std::uint8_t {
    Unknown,
    Direct,
    IndirectAbsolute,
    IndirectRelative,
};

// Whether a finger from a device of this kind is a finger on the screen: every kind but a
// trackpad's, unknown included.
constexpr bool is_screen_touch(TouchKind kind) {
    return kind != TouchKind::IndirectAbsolute && kind != TouchKind::IndirectRelative;
}

// Logs each touch device once. The memory is capped, and so is the log: a ninth device is said
// once to be too many and the rest stay quiet (never one line per finger).
class TouchDeviceLog {
  public:
    static constexpr std::size_t kMaxDevices = 8;
    enum class Say : std::uint8_t {
        Nothing,  // seen before
        Device,   // new: say its type
        TooMany,  // the list is full: say so, once
    };
    Say observe(std::uint64_t id) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (seen_[i] == id) {
                return Say::Nothing;
            }
        }
        if (count_ < kMaxDevices) {
            seen_[count_++] = id;
            return Say::Device;
        }
        if (told_too_many_) {
            return Say::Nothing;
        }
        told_too_many_ = true;
        return Say::TooMany;
    }

  private:
    std::array<std::uint64_t, kMaxDevices> seen_{};
    std::size_t count_ = 0;
    bool told_too_many_ = false;
};

} // namespace etude
