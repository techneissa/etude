// SPDX-License-Identifier: GPL-3.0-or-later
// The device check as the app runs it: which keyboards are plugged in (the MIDI ports and the
// developer's pretend ones), when the check opens by itself, what it remembers, and the answers of
// its card. The steps are core's `DeviceCheck`; the rules for when it appears are core's
// `should_offer_device_check`.
#pragma once

#include "dev_keyboard_event.hpp"
#include "midi_input_info.hpp"
#include "port_lists.hpp"
#include "ui/device_check_choice.hpp"

#include "etude/device_check.hpp"
#include "etude/device_check_offer.hpp"
#include "etude/keyboard.hpp"
#include "etude/midi_event.hpp"
#include "etude/session_state.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace etude::app {

using ui::DeviceCheckChoice;

inline constexpr std::size_t kMaxInputSlots = 8; // as MidiInput::kMaxPorts

class DeviceCheckHost {
  public:
    // The player's memory, from the settings file. `quiet` is a developer run (a script, the
    // audit, a headless run): the check never opens by itself for a real port or
    // for the first start, so the pictures and the measurements of every other screen stay as
    // they were. A pretend keyboard and `--device-check` still open it.
    void load(const AppSettings& app, bool quiet);
    // Writes the memory back when it changed since the last call.
    void write_back(AppSettings& app);

    // Once a frame, after the ports were read. `inputs` is the open ports (`changed`: since the last
    // call, so the work of a quiet frame is nothing); `ready`: the first scan of the ports is in;
    // `idle`: no song is playing, an offer waits for it, never forgotten.
    void update(std::span<const MidiInputInfo> inputs, bool ready, bool changed, bool idle);
    // A note-on from the MIDI input in `slot` (velocity above zero).
    void note_on(std::size_t slot, int pitch);
    // A pretend keyboard's doing (--fake-keyboard and friends). Returns the MIDI note-on or
    // note-off it stands for, for the window to play like a real one.
    struct Played {
        int pitch = 0;
        bool down = false;
    };
    std::optional<Played> dev_event(const DevKeyboardEvent& event);

    // The input the player chose on Settings > Input (empty: Auto). When it is one of the
    // keyboards plugged in, the check sees that one alone, like the app itself; a name nothing
    // answers to changes nothing. The MIDI inputs are already narrowed to it; this does the same
    // for the pretend ones.
    void set_input_pick(const std::string& name);
    const std::vector<std::string>& pretend_names() const { return pretend_; }

    // The card's answer. Run opens the check again, at once.
    void answer(DeviceCheckChoice choice);

    // While the card is up (`card_up`: drawn, not hidden by play, a panel or a card) a note only
    // lights its key: its note-ons are taken out of `events` before the song's evaluators, the
    // octave-shift detector and a start on the first note see them. Note-offs stay, so nothing is
    // left held. A check that is open but hidden never holds a note back from the song.
    void drop_note_ons(std::vector<MidiEvent>& events, bool card_up) const;

    bool open() const { return open_; }
    const DeviceCheck& check() const { return check_; }

  private:
    struct Keyboard {
        DeviceCheckKeyboard info;
        bool pretend = false;
        std::string port; // the name the port is listed by
    };
    std::vector<Keyboard> connected(std::span<const MidiInputInfo> inputs) const;
    void follow();
    void begin_run();
    void end_run();

    DeviceCheck check_;
    DeviceCheckMemory memory_;
    DeviceCheckMemory saved_;
    std::vector<std::string> followed_; // ids the card followed in this run, the ones it remembers
    bool quiet_ = false;
    bool open_ = false;
    bool want_ = false;      // an offer is waiting for the screen to be free
    bool scanned_ = false;   // the first scan of the ports was seen
    bool pretend_changed_ = false; // a pretend keyboard came or went since the last update
    std::vector<Keyboard> now_;               // plugged in at the last update
    std::vector<std::string> pretend_;        // names of the pretend keyboards plugged in
    std::string pick_;                        // the chosen input, or empty
    std::array<std::string, kMaxInputSlots> slot_id_;
};

// The id the app knows a keyboard by across runs: its profile's name when it has one, else its
// port name without the trailing "client:port" numbers a Linux port adds.
std::string keyboard_id(std::string_view port_name, const KeyboardProfile* profile);

} // namespace etude::app
