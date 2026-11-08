// SPDX-License-Identifier: GPL-3.0-or-later
// When the device check appears by itself, kept from nagging.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// What the app remembers between runs (saved by its owner): whether the check ever ended, and the
// keyboards it has seen.
struct DeviceCheckMemory {
    bool first_run_done = false;
    std::vector<std::string> seen; // keyboard ids, oldest first, capped at kMaxSeenKeyboards
};

inline constexpr std::size_t kMaxSeenKeyboards = 8;

enum class DeviceCheckTrigger : std::uint8_t {
    Start,           // the app started
    KeyboardPlugged, // a keyboard appeared while the app runs (not one found at start)
    Manual,          // Settings > Input > "Check keyboard"
};

// The check opens itself only:
//   - on the very first start (Start while `first_run_done` is false), and
//   - when a keyboard this app has never seen is plugged in (KeyboardPlugged with an id not in
//     `seen`), once per keyboard.
// Never on a later start, never for a known keyboard, never for one with no id (it cannot be told
// apart, so it cannot be remembered). The player starts it again from Settings: Manual always opens.
// The app also holds it back while a song is playing or a panel is open.
bool should_offer_device_check(const DeviceCheckMemory& memory, DeviceCheckTrigger trigger,
                               std::string_view keyboard_id);

// The check ended (Done or Skipped) with `keyboard_id` followed, empty when none: nothing offers
// itself again for that keyboard, and the first start is over. A keyboard that was only passed
// by (the check was not offered) is remembered the same way.
void remember_device_check(DeviceCheckMemory& memory, std::string_view keyboard_id);

// The memory as the settings file keeps it: ids joined by `|` (an id never holds one: it is
// replaced by a space), and back. A list that is too long for `max_bytes` loses its oldest ids.
std::string device_check_seen_text(const DeviceCheckMemory& memory, std::size_t max_bytes);
DeviceCheckMemory device_check_memory(bool first_run_done, std::string_view seen_text);

} // namespace etude
