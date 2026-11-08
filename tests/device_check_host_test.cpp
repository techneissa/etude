// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_check_host.hpp"
#include "ui/device_check_choice.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

MidiInputInfo psr(std::size_t slot = 0) {
    return {slot, "PSR-E383 MIDI 1 24:0", find_profile("PSR-E383")};
}

MidiInputInfo stranger(std::size_t slot = 1) {
    return {slot, "Mystery Keys 20:0", nullptr};
}

using Inputs = std::vector<MidiInputInfo>;

AppSettings after_first_run() {
    AppSettings app;
    app.device_check_done = true;
    return app;
}

} // namespace

TEST_CASE("device check host: a keyboard's id is its profile, or its name without port numbers") {
    CHECK(keyboard_id("PSR-E383 MIDI 1 24:0", find_profile("PSR-E383")) == "Yamaha PSR-E383");
    CHECK(keyboard_id("Mystery Keys 20:0", nullptr) == "Mystery Keys");
    CHECK(keyboard_id("Mystery Keys", nullptr) == "Mystery Keys");
    CHECK(keyboard_id("Port 1: Keys", nullptr) == "Port 1: Keys");
}

TEST_CASE("device check host: the first start opens the check, once the ports were scanned") {
    DeviceCheckHost host;
    host.load(AppSettings{}, false);
    host.update(Inputs{}, false, true, true);
    CHECK_FALSE(host.open());
    host.update(Inputs{}, true, true, true);
    REQUIRE(host.open());
    CHECK(host.check().step() == DeviceCheckStep::WaitingForKeyboard);
}

TEST_CASE("device check host: a developer run does not open it for the first start or a port") {
    DeviceCheckHost host;
    host.load(AppSettings{}, true);
    const Inputs plugged{psr()};
    host.update(plugged, true, true, true);
    CHECK_FALSE(host.open());
}

TEST_CASE("device check host: a later start does not open it") {
    DeviceCheckHost host;
    AppSettings app = after_first_run();
    app.device_check_seen = "Yamaha PSR-E383";
    host.load(app, false);
    const Inputs plugged{psr()};
    host.update(plugged, true, true, true);
    CHECK_FALSE(host.open());
}

TEST_CASE("device check host: a keyboard never seen opens it, and finds the keyboard") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    CHECK_FALSE(host.open());
    host.update(Inputs{psr()}, true, true, true);
    REQUIRE(host.open());
    REQUIRE(host.check().keyboard() != nullptr);
    CHECK(host.check().keyboard()->profile == "Yamaha PSR-E383");
    CHECK(host.check().step() == DeviceCheckStep::WaitingForNote);
}

TEST_CASE("device check host: the offer waits while the screen is not free, then comes") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr()}, true, true, false);
    CHECK_FALSE(host.open());
    host.update(Inputs{psr()}, true, true, false);
    CHECK_FALSE(host.open());
    host.update(Inputs{psr()}, true, true, true);
    CHECK(host.open());
}

TEST_CASE("device check host: a note from the input in a slot is seen, and Done remembers it") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr(2)}, true, true, true);
    host.note_on(2, 60);
    REQUIRE(host.check().note().has_value());
    CHECK(host.check().note()->key == 60);
    host.answer(DeviceCheckChoice::Done);
    CHECK_FALSE(host.open());
    AppSettings out;
    host.write_back(out);
    CHECK(out.device_check_done);
    CHECK(out.device_check_seen == "Yamaha PSR-E383");

    // Plugged again later: nothing is offered.
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr()}, true, true, true);
    CHECK_FALSE(host.open());
}

TEST_CASE("device check host: a note from a slot nothing is open in is ignored") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr(0)}, true, true, true);
    host.note_on(5, 60);
    CHECK(host.check().step() == DeviceCheckStep::WaitingForNote);
}

TEST_CASE("device check host: Skip with no keyboard ends the first start and remembers no one") {
    DeviceCheckHost host;
    host.load(AppSettings{}, false);
    host.update(Inputs{}, true, true, true);
    REQUIRE(host.open());
    host.answer(DeviceCheckChoice::Skip);
    CHECK_FALSE(host.open());
    AppSettings out;
    host.write_back(out);
    CHECK(out.device_check_done);
    CHECK(out.device_check_seen.empty());
    DeviceCheckHost next;
    next.load(out, false);
    next.update(Inputs{}, true, true, true);
    CHECK_FALSE(next.open());
}

TEST_CASE("device check host: unplugging goes back to waiting and plugging again finds it") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr()}, true, true, true);
    host.note_on(0, 60);
    host.update(Inputs{}, true, true, true);
    CHECK(host.open());
    CHECK(host.check().step() == DeviceCheckStep::WaitingForKeyboard);
    host.update(Inputs{psr()}, true, true, true);
    CHECK(host.check().step() == DeviceCheckStep::WaitingForNote);
}

TEST_CASE("device check host: a second keyboard is counted while the check is open") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr()}, true, true, true);
    host.update(Inputs{psr(), stranger()}, true, true, true);
    CHECK(host.check().others() == 1);
    host.note_on(1, 100);
    CHECK(host.check().keyboard()->name == "Mystery Keys");
}

TEST_CASE("device check host: Run opens it again at once, from any state") {
    DeviceCheckHost host;
    host.load(after_first_run(), true);
    host.update(Inputs{psr()}, true, true, true);
    CHECK_FALSE(host.open());
    host.answer(DeviceCheckChoice::Run);
    REQUIRE(host.open());
    CHECK(host.check().step() == DeviceCheckStep::WaitingForNote);
    host.answer(DeviceCheckChoice::Skip);
    CHECK_FALSE(host.open());
    host.answer(DeviceCheckChoice::Run);
    CHECK(host.open());
}

TEST_CASE("device check host: a pretend keyboard opens it and plays notes a window can play") {
    using Kind = DevKeyboardEvent::Kind;
    DeviceCheckHost host;
    host.load(after_first_run(), true);
    host.update(Inputs{}, true, true, true);
    CHECK_FALSE(host.dev_event({Kind::Plug, "PSR-E383", 0}).has_value());
    host.update(Inputs{}, true, true, true);
    REQUIRE(host.open());
    CHECK(host.check().keyboard()->profile == "Yamaha PSR-E383");
    const auto down = host.dev_event({Kind::NoteOn, "PSR-E383", 64});
    REQUIRE(down.has_value());
    CHECK(down->pitch == 64);
    CHECK(down->down);
    CHECK(host.check().note()->key == 64);
    const auto up = host.dev_event({Kind::NoteOff, "PSR-E383", 64});
    REQUIRE(up.has_value());
    CHECK_FALSE(up->down);
    CHECK_FALSE(host.dev_event({Kind::NoteOn, "Nobody", 60}).has_value());
    host.dev_event({Kind::Unplug, "PSR-E383", 0});
    host.update(Inputs{}, true, true, true);
    CHECK(host.check().step() == DeviceCheckStep::WaitingForKeyboard);
}

TEST_CASE("device check host: --device-check opens it") {
    DeviceCheckHost host;
    host.load(after_first_run(), true);
    host.dev_event({DevKeyboardEvent::Kind::OpenCheck, {}, 0});
    CHECK(host.open());
}

TEST_CASE("device check host: the settings are written back only when the memory changed") {
    DeviceCheckHost host;
    host.load(AppSettings{}, true);
    AppSettings out;
    host.write_back(out);
    CHECK_FALSE(out.device_check_done);
    host.answer(DeviceCheckChoice::Run);
    host.answer(DeviceCheckChoice::Skip);
    host.write_back(out);
    CHECK(out.device_check_done);
    out.device_check_done = false;
    host.write_back(out); // nothing new: the caller's value stays
    CHECK_FALSE(out.device_check_done);
}

TEST_CASE("device check host: a second keyboard the card never followed is still new") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr(), stranger()}, true, true, true);
    REQUIRE(host.open());
    host.note_on(0, 60); // only the PSR is played
    host.answer(DeviceCheckChoice::Done);
    AppSettings out;
    host.write_back(out);
    CHECK(out.device_check_seen == "Yamaha PSR-E383");
    host.update(Inputs{psr()}, true, true, true);
    host.update(Inputs{psr(), stranger()}, true, true, true); // plugged again: offered
    CHECK(host.open());
}

TEST_CASE("device check host: the keyboard played last is the one remembered") {
    DeviceCheckHost host;
    host.load(after_first_run(), false);
    host.update(Inputs{}, true, true, true);
    host.update(Inputs{psr(), stranger()}, true, true, true);
    host.note_on(0, 60);
    host.note_on(1, 60);
    host.answer(DeviceCheckChoice::Done);
    AppSettings out;
    host.write_back(out);
    CHECK(out.device_check_seen == "Yamaha PSR-E383|Mystery Keys");
}

TEST_CASE("device check host: while the card is up a note-on does not reach the song") {
    MidiEvent on;
    on.type = MidiEventType::NoteOn;
    MidiEvent off;
    off.type = MidiEventType::NoteOff;
    std::vector<MidiEvent> events{on, off, on};
    DeviceCheckHost host;
    host.load(after_first_run(), true);
    host.drop_note_ons(events, true); // closed: nothing dropped
    CHECK(events.size() == 3);
    host.answer(DeviceCheckChoice::Run);
    host.drop_note_ons(events, false); // open but hidden (a song plays): the song keeps its notes
    CHECK(events.size() == 3);
    host.drop_note_ons(events, true);
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == MidiEventType::NoteOff);
    host.answer(DeviceCheckChoice::Skip);
    events = {on};
    host.drop_note_ons(events, true); // closed again: notes reach the song
    CHECK(events.size() == 1);
}

TEST_CASE("device check: Escape and notes belong to the card only when it is drawn") {
    using etude::app::ui::device_check_wanted;
    CHECK(device_check_wanted(true, false, false));
    CHECK_FALSE(device_check_wanted(true, false, true)); // a song plays: Escape does what it did
    CHECK_FALSE(device_check_wanted(true, true, false)); // a panel or card covers it
    CHECK_FALSE(device_check_wanted(false, false, false)); // closed, or fading out
}
