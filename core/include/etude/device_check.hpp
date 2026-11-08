// SPDX-License-Identifier: GPL-3.0-or-later
// The device check: plug in the keyboard, see it recognised, play a note, see the right key light.
#pragma once

#include "etude/keyboard.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace etude {

// A keyboard the check can follow, as plain values. `id` is the app's stable key for the device
// (USB id, else port name); `profile` is the matched profile's name, empty when the keyboard is
// unknown (then `range` is the app's fallback range).
struct DeviceCheckKeyboard {
    std::string id;
    std::string name;
    std::string profile;
    KeyRange range = kRange61;

    bool known() const { return !profile.empty(); }
};

enum class DeviceCheckStep : std::uint8_t {
    WaitingForKeyboard, // no MIDI keyboard connected: the on-screen keyboard stays, Skip is offered
    WaitingForNote,     // keyboard found: its name, profile and range show, a note is awaited
    NoteSeen,           // a note arrived: the key lights (or the shift hint shows), Done is offered
    Done,               // the player confirmed
    Skipped,            // the player left without a keyboard or a note
};

// What the last note told. `key` is the key to light: the note itself when it is inside the
// keyboard's range, none otherwise (the screen never lights a key it cannot vouch for).
struct DeviceCheckNote {
    int pitch = 0;
    std::optional<int> key;
    // A note outside the range that a whole-octave shift of the keyboard (up to three octaves, the
    // keyboard's own octave button) would bring inside: the shift the keyboard seems to have, in
    // octaves, positive = it sends higher notes than it shows. 0 when no such shift exists.
    int shifted_octaves = 0;

    bool outside() const { return !key.has_value(); }
};

// Pure state machine, no clock and no I/O. The app feeds it what the MIDI layer reports and draws
// `step()`. Events that do not fit the step are ignored; Done and Skipped are final until begin().
class DeviceCheck {
  public:
    // A new run: forgets the last one. Keyboards already connected are reported after it.
    void begin();

    // A keyboard appeared (or was seen again; the same id replaces its entry). With none followed
    // yet the check follows it; a second one is counted in `others()` and followed once played.
    void keyboard_connected(const DeviceCheckKeyboard& keyboard);
    // A keyboard went away at any step: the check returns to the next connected keyboard, or to
    // waiting for one.
    void keyboard_removed(const std::string& id);
    // A note-on from keyboard `id` (velocity zero is a note-off and is not passed here). The first
    // note from another connected keyboard moves the check to it, so it follows the one played.
    void note_on(const std::string& id, int pitch);

    void finish(); // the player confirms the note: Done
    void skip();   // the player leaves: Skipped

    DeviceCheckStep step() const { return step_; }
    bool finished() const { return step_ == DeviceCheckStep::Done || step_ == DeviceCheckStep::Skipped; }
    // The keyboard being checked, none while waiting for one.
    const DeviceCheckKeyboard* keyboard() const;
    // Connected keyboards besides the followed one.
    int others() const;
    // The last note, once one has arrived.
    const std::optional<DeviceCheckNote>& note() const { return note_; }

  private:
    void settle(); // step from the connected list after it changed

    DeviceCheckStep step_ = DeviceCheckStep::WaitingForKeyboard;
    std::vector<DeviceCheckKeyboard> connected_;
    std::size_t followed_ = 0;
    std::optional<DeviceCheckNote> note_;
};

// What `note` says for a key range: the key to light and the octave hint. Exposed for tests.
DeviceCheckNote judge_check_note(KeyRange range, int pitch);

} // namespace etude
