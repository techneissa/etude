// SPDX-License-Identifier: GPL-3.0-or-later
// The window's side of Settings > Input's MIDI port rows: sends the player's choice to the watcher
// thread, takes the lists back for the page, and says once when a chosen port is gone.
#include "window_state.hpp"

namespace etude::app {

// Once a frame, after the ports were read. Allocates only when the lists or the pretend keyboards
// changed.
void WindowState::follow_midi_ports() {
    // Saved without the client and port numbers a Linux port ends with: they change with the order
    // the system finds its devices in.
    for (std::string* saved : {&model.app.midi_in_port, &model.app.midi_out_port}) {
        if (const std::string key = port_key(*saved); key != *saved) {
            *saved = key;
        }
    }
    midi_ports->choose(model.app.midi_in_port, model.app.midi_out_port);
    device_check.set_input_pick(model.app.midi_in_port);
    const PortLists& listed = midi_ports->lists();
    const std::uint32_t seen = midi_ports->lists_seen();
    const std::vector<std::string>& pretend = device_check.pretend_names();
    if (seen != shown_ports_seen || pretend != shown_pretend) {
        shown_ports_seen = seen;
        shown_pretend = pretend;
        shown_ports = listed;
        for (const std::string& name : pretend) {
            if (!port_listed(shown_ports.inputs, name)) {
                shown_ports.inputs.push_back(name);
            }
        }
    }
    if (in_fallback.update(model.app.midi_in_port, shown_ports, true)) {
        show_message("MIDI input " + model.app.midi_in_port + " is not connected: using Auto",
                     false);
    }
    if (out_open_failure.update(shown_ports.output_failed)) {
        show_message("MIDI output " + shown_ports.output_failed + " would not open: using Auto",
                     false);
    }
    if (out_fallback.update(model.app.midi_out_port, shown_ports, false)) {
        show_message("MIDI output " + model.app.midi_out_port + " is not connected: using Auto",
                     false);
    }
}

} // namespace etude::app
