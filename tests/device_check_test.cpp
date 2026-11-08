// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/device_check.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

DeviceCheckKeyboard psr() {
    return {"usb:0499:1a0e", "PSR-E383", "Yamaha PSR-E383", kRange61};
}

DeviceCheckKeyboard stranger() {
    return {"port:Mystery", "Mystery MIDI", "", kRange61};
}

DeviceCheckKeyboard stage() {
    return {"usb:0000:0001", "Stage 88", "Stage 88", kRange88};
}

} // namespace

TEST_CASE("device check starts waiting for a keyboard with nothing followed") {
    DeviceCheck check;
    CHECK(check.step() == DeviceCheckStep::WaitingForKeyboard);
    CHECK(check.keyboard() == nullptr);
    CHECK(check.others() == 0);
    CHECK_FALSE(check.note().has_value());
}

TEST_CASE("device check: a keyboard found shows its name, profile and range") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    REQUIRE(check.step() == DeviceCheckStep::WaitingForNote);
    REQUIRE(check.keyboard() != nullptr);
    CHECK(check.keyboard()->name == "PSR-E383");
    CHECK(check.keyboard()->profile == "Yamaha PSR-E383");
    CHECK(check.keyboard()->range.lowest == 36);
    CHECK(check.keyboard()->range.highest == 96);
    CHECK(check.keyboard()->known());
}

TEST_CASE("device check: an unknown keyboard is found without a profile") {
    DeviceCheck check;
    check.keyboard_connected(stranger());
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
    CHECK_FALSE(check.keyboard()->known());
}

TEST_CASE("device check: a note inside the range lights that key") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.note_on("usb:0499:1a0e", 60);
    REQUIRE(check.step() == DeviceCheckStep::NoteSeen);
    REQUIRE(check.note().has_value());
    CHECK(check.note()->key == 60);
    CHECK_FALSE(check.note()->outside());
    CHECK(check.note()->shifted_octaves == 0);
}

TEST_CASE("device check: range ends count as inside") {
    CHECK(judge_check_note(kRange61, 36).key == 36);
    CHECK(judge_check_note(kRange61, 96).key == 96);
}

TEST_CASE("device check: a note outside the range lights no key and hints the octave") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.note_on("usb:0499:1a0e", 108); // 61 keys 36..96: one octave up is 48..108
    REQUIRE(check.note().has_value());
    CHECK_FALSE(check.note()->key.has_value());
    CHECK(check.note()->outside());
    CHECK(check.note()->shifted_octaves == 1);
}

TEST_CASE("device check: a note below the range hints a downward shift") {
    const auto note = judge_check_note(kRange61, 27); // 36..96 shifted down one octave is 24..84
    CHECK(note.outside());
    CHECK(note.shifted_octaves == -1);
}

TEST_CASE("device check: the smaller shift wins and two octaves are found") {
    CHECK(judge_check_note(kRange61, 118).shifted_octaves == 2);  // 60..120
    CHECK(judge_check_note(kRange61, 12).shifted_octaves == -2);  // 12..72
}

TEST_CASE("device check: a note no octave shift explains gives no hint") {
    const auto note = judge_check_note(kRange49, 127); // 49 keys 36..84; three up is 72..120
    CHECK(note.outside());
    CHECK(note.shifted_octaves == 0);
}

TEST_CASE("device check: an 88-key piano never hints a shift it cannot have") {
    // 21..108 down one octave is 9..96.
    const auto note = judge_check_note(kRange88, 10);
    CHECK(note.outside());
    CHECK(note.shifted_octaves == -1);
}

TEST_CASE("device check: Done only follows a note") {
    DeviceCheck check;
    check.finish();
    CHECK(check.step() == DeviceCheckStep::WaitingForKeyboard);
    check.keyboard_connected(psr());
    check.finish();
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
    check.note_on(psr().id, 60);
    check.finish();
    CHECK(check.step() == DeviceCheckStep::Done);
    CHECK(check.finished());
}

TEST_CASE("device check: another note replaces the one shown") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.note_on(psr().id, 60);
    check.note_on(psr().id, 108);
    CHECK(check.step() == DeviceCheckStep::NoteSeen);
    CHECK(check.note()->pitch == 108);
}

TEST_CASE("device check: unplugging at any step goes back to waiting") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.keyboard_removed(psr().id);
    CHECK(check.step() == DeviceCheckStep::WaitingForKeyboard);
    CHECK(check.keyboard() == nullptr);

    check.keyboard_connected(psr());
    check.note_on(psr().id, 60);
    check.keyboard_removed(psr().id);
    CHECK(check.step() == DeviceCheckStep::WaitingForKeyboard);
    CHECK_FALSE(check.note().has_value());

    check.keyboard_connected(psr()); // plugged back: found again, no stale note
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
    CHECK_FALSE(check.note().has_value());
}

TEST_CASE("device check: unplugging an unknown keyboard id changes nothing") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.keyboard_removed("usb:nothing");
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
}

TEST_CASE("device check: a second keyboard is counted, not followed") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.keyboard_connected(stage());
    CHECK(check.keyboard()->name == "PSR-E383");
    CHECK(check.others() == 1);
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
}

TEST_CASE("device check: the note judged is the played keyboard's range") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.keyboard_connected(stage());
    check.note_on(stage().id, 100); // outside 61 keys, inside 88
    CHECK(check.keyboard()->name == "Stage 88");
    CHECK(check.note()->key == 100);
    CHECK(check.others() == 1);
}

TEST_CASE("device check: unplugging the followed keyboard moves to the other one") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.keyboard_connected(stage());
    check.note_on(psr().id, 60);
    check.keyboard_removed(psr().id);
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
    CHECK(check.keyboard()->name == "Stage 88");
    CHECK(check.others() == 0);
    CHECK_FALSE(check.note().has_value());
}

TEST_CASE("device check: unplugging the other keyboard keeps the step and the note") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.keyboard_connected(stage());
    check.note_on(psr().id, 60);
    check.keyboard_removed(stage().id);
    CHECK(check.step() == DeviceCheckStep::NoteSeen);
    CHECK(check.keyboard()->name == "PSR-E383");
    CHECK(check.others() == 0);
}

TEST_CASE("device check: seeing the same keyboard again replaces it without a second entry") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    auto again = psr();
    again.profile = "Yamaha PSR-E383 (renamed)";
    check.keyboard_connected(again);
    CHECK(check.others() == 0);
    CHECK(check.keyboard()->profile == "Yamaha PSR-E383 (renamed)");
}

TEST_CASE("device check: notes from an unconnected id or outside MIDI are ignored") {
    DeviceCheck check;
    check.note_on("usb:ghost", 60); // nothing connected
    CHECK(check.step() == DeviceCheckStep::WaitingForKeyboard);
    check.keyboard_connected(psr());
    check.note_on("usb:ghost", 60);
    check.note_on(psr().id, 128);
    check.note_on(psr().id, -1);
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
}

TEST_CASE("device check: Skip leaves from any step and is final") {
    DeviceCheck none;
    none.skip();
    CHECK(none.step() == DeviceCheckStep::Skipped);

    DeviceCheck found;
    found.keyboard_connected(psr());
    found.skip();
    CHECK(found.step() == DeviceCheckStep::Skipped);

    found.keyboard_connected(stage()); // nothing moves a finished check
    found.note_on(psr().id, 60);
    found.keyboard_removed(psr().id);
    found.finish();
    CHECK(found.step() == DeviceCheckStep::Skipped);
    CHECK(found.keyboard()->name == "PSR-E383");
}

TEST_CASE("device check: Done keeps the keyboard and note for the owner to remember") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.note_on(psr().id, 60);
    check.finish();
    CHECK(check.keyboard()->id == psr().id);
    CHECK(check.note()->key == 60);
}

TEST_CASE("device check: begin runs it again from the start") {
    DeviceCheck check;
    check.keyboard_connected(psr());
    check.note_on(psr().id, 60);
    check.finish();
    check.begin();
    CHECK(check.step() == DeviceCheckStep::WaitingForKeyboard);
    CHECK(check.keyboard() == nullptr);
    CHECK_FALSE(check.note().has_value());
    check.keyboard_connected(psr());
    CHECK(check.step() == DeviceCheckStep::WaitingForNote);
}

TEST_CASE("device check: more than eight keyboards are not followed") {
    DeviceCheck check;
    for (int i = 0; i < 12; ++i) {
        check.keyboard_connected({"id" + std::to_string(i), "K", "", kRange61});
    }
    CHECK(check.others() == 7);
}
