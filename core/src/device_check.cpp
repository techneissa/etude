// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/device_check.hpp"

#include "etude/shift_offer.hpp"

#include <algorithm>

namespace etude {

namespace {
constexpr int kMaxCheckKeyboards = 8; // more connected ones are not followed
constexpr int kMaxShiftOctaves = kMaxKeyboardShift / 12;
} // namespace

DeviceCheckNote judge_check_note(KeyRange range, int pitch) {
    DeviceCheckNote note;
    note.pitch = pitch;
    if (range.contains(pitch)) {
        note.key = pitch;
        return note;
    }
    // The smallest octave shift, either way, that puts the note inside what the keyboard would
    // send; the nearer shift first, up before down on a tie.
    for (int octaves = 1; octaves <= kMaxShiftOctaves; ++octaves) {
        for (const int sign : {1, -1}) {
            const auto moved = shifted_range(range, sign * octaves * 12);
            if (moved && moved->contains(pitch)) {
                note.shifted_octaves = sign * octaves;
                return note;
            }
        }
    }
    return note;
}

void DeviceCheck::begin() {
    step_ = DeviceCheckStep::WaitingForKeyboard;
    connected_.clear();
    followed_ = 0;
    note_.reset();
}

void DeviceCheck::settle() {
    if (finished()) {
        return;
    }
    note_.reset();
    step_ = connected_.empty() ? DeviceCheckStep::WaitingForKeyboard
                               : DeviceCheckStep::WaitingForNote;
}

void DeviceCheck::keyboard_connected(const DeviceCheckKeyboard& keyboard) {
    if (finished()) {
        return;
    }
    const auto same = std::ranges::find(connected_, keyboard.id, &DeviceCheckKeyboard::id);
    if (same != connected_.end()) {
        *same = keyboard;
        return;
    }
    if (connected_.size() >= static_cast<std::size_t>(kMaxCheckKeyboards)) {
        return;
    }
    connected_.push_back(keyboard);
    if (connected_.size() == 1) {
        followed_ = 0;
        settle();
    }
}

void DeviceCheck::keyboard_removed(const std::string& id) {
    if (finished()) {
        return;
    }
    const auto at = std::ranges::find(connected_, id, &DeviceCheckKeyboard::id);
    if (at == connected_.end()) {
        return;
    }
    const auto index = static_cast<std::size_t>(at - connected_.begin());
    const bool was_followed = index == followed_;
    connected_.erase(at);
    if (index < followed_) {
        --followed_;
    }
    if (was_followed) {
        followed_ = 0;
        settle(); // unplugging the checked keyboard starts over, with the next one if any
    }
}

void DeviceCheck::note_on(const std::string& id, int pitch) {
    if (finished() || connected_.empty() || pitch < 0 || pitch > 127) {
        return;
    }
    const auto from = std::ranges::find(connected_, id, &DeviceCheckKeyboard::id);
    if (from == connected_.end()) {
        return;
    }
    followed_ = static_cast<std::size_t>(from - connected_.begin());
    note_ = judge_check_note(from->range, pitch);
    step_ = DeviceCheckStep::NoteSeen;
}

void DeviceCheck::finish() {
    if (step_ == DeviceCheckStep::NoteSeen) {
        step_ = DeviceCheckStep::Done;
    }
}

void DeviceCheck::skip() {
    if (!finished()) {
        step_ = DeviceCheckStep::Skipped;
    }
}

const DeviceCheckKeyboard* DeviceCheck::keyboard() const {
    return connected_.empty() ? nullptr : &connected_[followed_];
}

int DeviceCheck::others() const {
    return connected_.empty() ? 0 : static_cast<int>(connected_.size()) - 1;
}

} // namespace etude
