// SPDX-License-Identifier: GPL-3.0-or-later
// Settings > Input's MIDI port rows: the choice is saved by name, a port that is gone means Auto
// and says so once, and the pick reaches the watcher thread and the device check.
#include "device_check_host.hpp"
#include "midi_ports.hpp"
#include "port_lists.hpp"

#include "etude/session_state.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

PortLists listed(std::vector<std::string> in, std::vector<std::string> out = {}) {
    PortLists lists;
    lists.inputs = std::move(in);
    lists.outputs = std::move(out);
    lists.inputs_known = true;
    lists.outputs_known = true;
    return lists;
}

} // namespace

TEST_CASE("midi ports: the choices round trip through the settings file, by name") {
    AppSettings a;
    a.midi_in_port = "Mystery Keys 20:0";
    a.midi_out_port = "USB MIDI Interface";
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    std::vector<SettingsIssue> issues;
    const AppSettings back = read_app_settings(SettingsDocument::parse(doc.serialize()), &issues);
    CHECK(back.midi_in_port == "Mystery Keys 20:0");
    CHECK(back.midi_out_port == "USB MIDI Interface");
    CHECK(issues.empty());
    CHECK(AppSettings{}.midi_in_port.empty()); // Auto is the default
}

TEST_CASE("midi ports: the steps are Auto, then the ports, then a saved port that is gone") {
    const PortChoices none = port_choices("", {"A", "B"});
    CHECK(none.names == std::vector<std::string>{"", "A", "B"});
    CHECK(none.current == 0);
    const PortChoices b = port_choices("B", {"A", "B"});
    CHECK(b.current == 2);
    const PortChoices gone = port_choices("Z", {"A", "B"});
    CHECK(gone.names == std::vector<std::string>{"", "A", "B", "Z"});
    CHECK(gone.current == 3);
}

TEST_CASE("midi ports: a chosen port that is missing means Auto") {
    const PortLists lists = listed({"Keys A", "Keys B"});
    CHECK_FALSE(port_missing("", lists, true));
    CHECK_FALSE(port_missing("Keys B", lists, true));
    CHECK(port_missing("Keys C", lists, true));
    // Before the first listing nothing is known to be missing.
    CHECK_FALSE(port_missing("Keys C", PortLists{}, true));
    // A named input that is listed is the only one held open, the first port of that name;
    // otherwise (Auto, or the name gone) all of them are.
    const std::vector<std::string> keys{"Keys A", "Keys B", "Keys B"};
    CHECK(pinned_index("Keys B", keys) == 1);
    CHECK(pinned_index("Keys A", keys) == 0);
    CHECK(pinned_index("Keys C", keys) == -1);
    CHECK(pinned_index("", keys) == -1);
}

TEST_CASE("midi ports: names lose the client and port numbers, and a name is listed once") {
    CHECK(port_key("PSR-E383 MIDI 1 24:0") == "PSR-E383 MIDI 1");
    CHECK(port_key("PSR-E383 MIDI 1") == "PSR-E383 MIDI 1");
    CHECK(port_key("Port 1: Keys") == "Port 1: Keys");
    CHECK(port_keys({"Keys 20:0", "Keys 28:0", "Other"}) == std::vector<std::string>{"Keys", "Other"});
}

TEST_CASE("midi ports: the inputs and the outputs are known one at a time") {
    MidiPorts ports;
    ports.publish_inputs({"Keys A"});
    const PortLists& lists = ports.lists();
    CHECK(lists.inputs_known);
    CHECK_FALSE(lists.outputs_known);
    // A saved output is not missing before the outputs were listed, whatever the inputs say.
    CHECK_FALSE(port_missing("Synth", lists, false));
    PortFallback fallback;
    CHECK_FALSE(fallback.update("Synth", lists, false));
    ports.publish_outputs({"Other"});
    CHECK(fallback.update("Synth", ports.lists(), false));
}

TEST_CASE("midi ports: an output that would not open is told once") {
    OpenFailure failure;
    CHECK_FALSE(failure.update(""));
    CHECK(failure.update("Synth"));
    CHECK_FALSE(failure.update("Synth"));
    CHECK_FALSE(failure.update(""));
    CHECK(failure.update("Synth"));
}

TEST_CASE("midi ports: the fallback says so once and the port is taken again when it returns") {
    PortFallback fallback;
    const PortLists both = listed({"Keys A", "Keys B"});
    const PortLists only_a = listed({"Keys A"});
    CHECK_FALSE(fallback.update("Keys B", PortLists{}, true)); // not listed yet
    CHECK_FALSE(fallback.update("Keys B", both, true));
    CHECK(fallback.update("Keys B", only_a, true));
    CHECK_FALSE(fallback.update("Keys B", only_a, true)); // once
    CHECK_FALSE(fallback.update("Keys B", both, true));   // back: the saved name is in force again
    CHECK(fallback.update("Keys B", only_a, true));       // gone again: told again
    CHECK_FALSE(fallback.update("", only_a, true));       // Auto never says it
    // The output has its own list.
    CHECK(fallback.update("Synth", listed({"Keys A"}, {"Other"}), false));
}

TEST_CASE("midi ports: a pick reaches the watcher thread once, and wakes it") {
    MidiPorts ports;
    int woken = 0;
    ports.set_poke([&woken] { ++woken; });
    CHECK(ports.input_choice().empty());
    const std::uint32_t before = ports.choice_serial();
    ports.choose("Keys B", "Synth");
    CHECK(ports.input_choice() == "Keys B");
    CHECK(ports.output_choice() == "Synth");
    CHECK(ports.choice_serial() == before + 1);
    CHECK(woken == 1);
    ports.choose("Keys B", "Synth"); // every frame: nothing changed, nothing is handed over
    CHECK(ports.choice_serial() == before + 1);
    CHECK(woken == 1);
    ports.choose("", "Synth"); // back to Auto
    CHECK(ports.input_choice().empty());
    CHECK(ports.choice_serial() == before + 2);
}

TEST_CASE("midi ports: the lists come back to the frame thread, published only when they change") {
    MidiPorts ports;
    CHECK_FALSE(ports.lists().inputs_known);
    ports.publish_inputs({"Keys A"});
    ports.publish_outputs({"Synth"});
    const PortLists& first = ports.lists();
    CHECK(first.inputs_known);
    CHECK(first.inputs == std::vector<std::string>{"Keys A"});
    CHECK(first.outputs == std::vector<std::string>{"Synth"});
    const std::uint32_t seen = ports.lists_seen();
    ports.publish_inputs({"Keys A"}); // the same again
    ports.lists();
    CHECK(ports.lists_seen() == seen);
    ports.publish_inputs({"Keys A", "Keys B"});
    CHECK(ports.lists().inputs.size() == 2);
    CHECK(ports.lists_seen() == seen + 1);
}

TEST_CASE("midi ports: the device check sees only the chosen keyboard, and Auto sees all") {
    using Kind = DevKeyboardEvent::Kind;
    DeviceCheckHost host;
    AppSettings app;
    app.device_check_done = true;
    host.load(app, true);
    host.dev_event({Kind::Plug, "PSR-E383", 0});
    host.dev_event({Kind::Plug, "Mystery Keys", 0});
    host.update(std::vector<MidiInputInfo>{}, true, true, true);
    CHECK(host.open()); // a pretend keyboard opens it
    host.answer(DeviceCheckChoice::Skip);

    host.set_input_pick("Mystery Keys");
    host.update(std::vector<MidiInputInfo>{}, true, false, true);
    host.answer(DeviceCheckChoice::Run);
    REQUIRE(host.check().keyboard() != nullptr);
    CHECK(host.check().keyboard()->name == "Mystery Keys");
    CHECK(host.check().others() == 0);

    // An empty pick (Auto) sees both.
    host.set_input_pick("");
    host.update(std::vector<MidiInputInfo>{}, true, false, true);
    host.answer(DeviceCheckChoice::Run);
    CHECK(host.check().others() == 1);

    // A pretend name picked never hides a real keyboard.
    host.set_input_pick("Mystery Keys");
    host.update(std::vector<MidiInputInfo>{{0, "Real Keys 20:0", nullptr}}, true, true, true);
    host.answer(DeviceCheckChoice::Run);
    CHECK(host.check().others() == 1); // the real one and the picked pretend one

    // A pick nothing answers to changes nothing: both are there, as in Auto.
    host.set_input_pick("Unplugged Keys");
    host.update(std::vector<MidiInputInfo>{}, true, false, true);
    host.answer(DeviceCheckChoice::Run);
    CHECK(host.check().others() == 1);
}
