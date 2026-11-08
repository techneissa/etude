// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/device_check_text.hpp"

#include "etude/pitch.hpp"

#include <cstdlib>

namespace etude {

namespace {

std::string keys_text(const DeviceCheckKeyboard& kb, int octave_offset) {
    if (!kb.known()) {
        return "Unknown model · " + std::to_string(kb.range.key_count()) + " keys";
    }
    return "Recognised · " + note_name(kb.range.lowest, false, octave_offset) + " to " +
           note_name(kb.range.highest, false, octave_offset);
}

std::string shift_text(int octaves) {
    const int n = std::abs(octaves);
    return std::string("shifted ") + (n == 1 ? "an octave" : std::to_string(n) + " octaves") +
           (octaves > 0 ? " up?" : " down?");
}

DeviceCheckWords note_words(const DeviceCheckNote& note, int octave_offset) {
    DeviceCheckWords w;
    const std::string name = note_name(note.pitch, false, octave_offset);
    if (!note.outside()) {
        w.tone = DeviceCheckTone::Good;
        w.detail = name + " · looks right";
        return w;
    }
    w.tone = DeviceCheckTone::Warn;
    w.detail = name + " is off the keys: " +
               (note.shifted_octaves != 0 ? shift_text(note.shifted_octaves)
                                          : std::string("check Settings > Input"));
    return w;
}

} // namespace

DeviceCheckWords device_check_words(const DeviceCheck& check, int octave_offset) {
    const DeviceCheckKeyboard* kb = check.keyboard();
    if (kb == nullptr) {
        return {"Plug in your keyboard", "The on-screen keys work meanwhile.",
                DeviceCheckTone::Waiting};
    }
    DeviceCheckWords w;
    if (check.note().has_value() && check.step() != DeviceCheckStep::WaitingForNote) {
        w = note_words(*check.note(), octave_offset);
    } else {
        w.tone = DeviceCheckTone::Found;
        w.detail = check.others() > 0
                       ? std::to_string(check.others() + 1) + " keyboards · play the one to check"
                       : keys_text(*kb, octave_offset) + " · play a key";
    }
    w.title = kb->known() ? kb->profile : kb->name;
    return w;
}

} // namespace etude
