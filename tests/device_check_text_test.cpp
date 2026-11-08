// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/device_check_text.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

DeviceCheckKeyboard psr() {
    return {"Yamaha PSR-E383", "PSR-E383 MIDI 1", "Yamaha PSR-E383", kRange61};
}

} // namespace

TEST_CASE("device check words: waiting asks for a keyboard and says the keys stay") {
    DeviceCheck check;
    const DeviceCheckWords w = device_check_words(check);
    CHECK(w.title == "Plug in your keyboard");
    CHECK(w.detail == "The on-screen keys work meanwhile.");
    CHECK(w.tone == DeviceCheckTone::Waiting);
}

TEST_CASE("device check words: a found keyboard shows its profile and range") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    const DeviceCheckWords w = device_check_words(check);
    CHECK(w.title == "Yamaha PSR-E383");
    CHECK(w.detail == "Recognised · C2 to C7 · play a key");
    CHECK(w.tone == DeviceCheckTone::Found);
}

TEST_CASE("device check words: the look's octave numbering applies") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    CHECK(device_check_words(check, -1).detail == "Recognised · C1 to C6 · play a key");
}

TEST_CASE("device check words: an unknown keyboard shows its own name and no range claim") {
    DeviceCheck check;
    check.keyboard_connected({"port:Odd", "Odd Keys", "", kRange88});
    const DeviceCheckWords w = device_check_words(check);
    CHECK(w.title == "Odd Keys");
    CHECK(w.detail == "Unknown model · 88 keys · play a key");
}

TEST_CASE("device check words: two keyboards ask which to play") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.keyboard_connected({"port:Odd", "Odd Keys", "", kRange88});
    CHECK(device_check_words(check).detail == "2 keyboards · play the one to check");
}

TEST_CASE("device check words: a note inside the keys is good") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.note_on(psr().id, 60);
    const DeviceCheckWords w = device_check_words(check);
    CHECK(w.detail == "C4 · looks right");
    CHECK(w.tone == DeviceCheckTone::Good);
}

TEST_CASE("device check words: a note off the keys warns, with the octave when one explains it") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.note_on(psr().id, 108);
    DeviceCheckWords w = device_check_words(check);
    CHECK(w.tone == DeviceCheckTone::Warn);
    CHECK(w.detail == "C8 is off the keys: shifted an octave up?");
    check.note_on(psr().id, 118);
    CHECK(device_check_words(check).detail == "A#8 is off the keys: shifted 2 octaves up?");
    check.note_on(psr().id, 27);
    CHECK(device_check_words(check).detail == "D#1 is off the keys: shifted an octave down?");
}

TEST_CASE("device check words: an off note no octave explains points to the Input settings") {
    DeviceCheck check;
    check.keyboard_connected({"id", "Small", "Small", kRange49});
    check.note_on("id", 127);
    CHECK(device_check_words(check).detail == "G9 is off the keys: check Settings > Input");
}
