// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/device_check_offer.hpp"

#include <algorithm>

namespace etude {

namespace {
bool has_seen(const DeviceCheckMemory& memory, std::string_view id) {
    return std::ranges::find(memory.seen, id) != memory.seen.end();
}
} // namespace

bool should_offer_device_check(const DeviceCheckMemory& memory, DeviceCheckTrigger trigger,
                               std::string_view keyboard_id) {
    switch (trigger) {
    case DeviceCheckTrigger::Manual:
        return true;
    case DeviceCheckTrigger::Start:
        return !memory.first_run_done;
    case DeviceCheckTrigger::KeyboardPlugged:
        return !keyboard_id.empty() && !has_seen(memory, keyboard_id);
    }
    return false;
}

void remember_device_check(DeviceCheckMemory& memory, std::string_view keyboard_id) {
    memory.first_run_done = true;
    if (keyboard_id.empty() || has_seen(memory, keyboard_id)) {
        return;
    }
    if (memory.seen.size() >= kMaxSeenKeyboards) {
        memory.seen.erase(memory.seen.begin());
    }
    memory.seen.emplace_back(keyboard_id);
}

std::string device_check_seen_text(const DeviceCheckMemory& memory, std::size_t max_bytes) {
    std::vector<std::string> ids;
    for (const std::string& id : memory.seen) {
        std::string clean = id;
        std::ranges::replace(clean, '|', ' ');
        ids.push_back(std::move(clean));
    }
    const auto length = [&] {
        std::size_t n = 0;
        for (const std::string& id : ids) {
            n += id.size() + 1;
        }
        return n > 0 ? n - 1 : 0;
    };
    while (!ids.empty() && length() > max_bytes) {
        ids.erase(ids.begin());
    }
    std::string text;
    for (const std::string& id : ids) {
        text += text.empty() ? "" : "|";
        text += id;
    }
    return text;
}

DeviceCheckMemory device_check_memory(bool first_run_done, std::string_view seen_text) {
    DeviceCheckMemory memory;
    memory.first_run_done = first_run_done;
    while (!seen_text.empty() && memory.seen.size() < kMaxSeenKeyboards) {
        const std::size_t bar = seen_text.find('|');
        const std::string_view id = seen_text.substr(0, bar);
        if (!id.empty()) {
            memory.seen.emplace_back(id);
        }
        seen_text = bar == std::string_view::npos ? std::string_view{} : seen_text.substr(bar + 1);
    }
    return memory;
}

} // namespace etude
