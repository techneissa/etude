// SPDX-License-Identifier: GPL-3.0-or-later
// The window's side of the device check: feeds it the ports, the keyboards' notes and the
// developer's pretend keyboards, and takes the card's answers.
#include "window_state.hpp"

namespace etude::app {

static_assert(kMaxInputSlots == MidiInput::kMaxPorts);

bool developer_run(const WindowOptions& opt) {
    return opt.script.active() || opt.audit.active() || opt.headless;
}

int icon_title_for(const WindowOptions& opt) {
    if (opt.icon_title >= 0) {
        return opt.icon_title;
    }
    return opt.about_demo.empty() ? -1 : 4;
}

// A developer run keeps the check from opening by itself, so every other screen is the same as
// without it.
bool WindowState::quiet_device_check() const { return developer_run(opt); }

// Once a frame after the ports were read: the check sees the inputs that came or went, and the
// first scan. The list is read again only when an input came or went.
void WindowState::update_device_check(const PortChanges& changes) {
    const bool ready = midi->ready();
    const bool changed = changes.opened > 0 || changes.closed > 0 || ready != check_ready;
    if (changed) {
        check_ready = ready;
        check_inputs = midi->inputs();
    }
    // An offer waits while a song plays and comes when it stops.
    // The microphone is the player then: a keyboard's notes are dropped, so no card is offered.
    const bool free_to_offer = !songs.playing() && !songs.counting_down() &&
                      model.app.input_mode != etude::InputMode::Microphone;
    device_check.update(check_inputs, ready, changed, free_to_offer);
}

// The developer's pretend keyboards (--fake-keyboard and friends): a
// note they play is a MIDI note of its own to the keys, the trail and the song.
void WindowState::take_dev_keyboards() {
    const std::vector<DevKeyboardEvent> due = opt.script.take_keyboard_events();
    for (const DevKeyboardEvent& ev : due) {
        const auto played = device_check.dev_event(ev);
        if (!played) {
            continue;
        }
        MidiEvent note;
        note.time_ns = midi_clock_ns();
        note.type = played->down ? etude::MidiEventType::NoteOn : etude::MidiEventType::NoteOff;
        note.data1 = static_cast<std::uint8_t>(played->pitch);
        note.data2 = played->down ? 100 : 0;
        virtual_events.push_back(note);
    }
}

void WindowState::answer_device_check(etude::app::ui::DeviceCheckChoice choice) {
    if (choice == etude::app::ui::DeviceCheckChoice::None) {
        return;
    }
    device_check.answer(choice);
    device_check.write_back(model.app);
    if (choice == etude::app::ui::DeviceCheckChoice::Run) {
        ui.close_panel(); // the card is over the notes, which the settings sheet covers
    }
}

} // namespace etude::app
