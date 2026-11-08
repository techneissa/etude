// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/device_check_offer.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("device check offer: the first start opens it, later starts do not") {
    DeviceCheckMemory memory;
    CHECK(should_offer_device_check(memory, DeviceCheckTrigger::Start, ""));
    remember_device_check(memory, "");
    CHECK(memory.first_run_done);
    CHECK_FALSE(should_offer_device_check(memory, DeviceCheckTrigger::Start, ""));
}

TEST_CASE("device check offer: skipping with no keyboard ends the first start") {
    DeviceCheckMemory memory;
    remember_device_check(memory, "");
    CHECK(memory.seen.empty());
    CHECK_FALSE(should_offer_device_check(memory, DeviceCheckTrigger::Start, ""));
}

TEST_CASE("device check offer: a keyboard never seen offers it once") {
    DeviceCheckMemory memory;
    remember_device_check(memory, "usb:a");
    CHECK(should_offer_device_check(memory, DeviceCheckTrigger::KeyboardPlugged, "usb:b"));
    remember_device_check(memory, "usb:b");
    CHECK_FALSE(should_offer_device_check(memory, DeviceCheckTrigger::KeyboardPlugged, "usb:b"));
    CHECK_FALSE(should_offer_device_check(memory, DeviceCheckTrigger::KeyboardPlugged, "usb:a"));
}

TEST_CASE("device check offer: a keyboard with no id is never offered") {
    DeviceCheckMemory memory;
    remember_device_check(memory, "usb:a");
    CHECK_FALSE(should_offer_device_check(memory, DeviceCheckTrigger::KeyboardPlugged, ""));
}

TEST_CASE("device check offer: Manual always opens it") {
    DeviceCheckMemory memory;
    remember_device_check(memory, "usb:a");
    CHECK(should_offer_device_check(memory, DeviceCheckTrigger::Manual, "usb:a"));
    CHECK(should_offer_device_check(memory, DeviceCheckTrigger::Manual, ""));
}

TEST_CASE("device check offer: remembering twice keeps one entry") {
    DeviceCheckMemory memory;
    remember_device_check(memory, "usb:a");
    remember_device_check(memory, "usb:a");
    CHECK(memory.seen.size() == 1);
}

TEST_CASE("device check offer: the seen keyboards go to the settings file and back") {
    DeviceCheckMemory memory;
    remember_device_check(memory, "Yamaha PSR-E383");
    remember_device_check(memory, "port:Odd|Name");
    const std::string text = device_check_seen_text(memory, 400);
    CHECK(text == "Yamaha PSR-E383|port:Odd Name");
    const DeviceCheckMemory back = device_check_memory(true, text);
    CHECK(back.first_run_done);
    REQUIRE(back.seen.size() == 2);
    CHECK(back.seen[1] == "port:Odd Name");
    CHECK(device_check_memory(false, "").seen.empty());
    CHECK(device_check_memory(false, "a||b|").seen.size() == 2);
}

TEST_CASE("device check offer: a seen list too long for the file loses its oldest ids") {
    DeviceCheckMemory memory;
    remember_device_check(memory, "aaaa");
    remember_device_check(memory, "bbbb");
    remember_device_check(memory, "cccc");
    CHECK(device_check_seen_text(memory, 9) == "bbbb|cccc");
    CHECK(device_check_seen_text(memory, 3).empty());
}

TEST_CASE("device check offer: more ids than the cap in a file keep the first ones") {
    std::string text;
    for (std::size_t i = 0; i < kMaxSeenKeyboards + 4; ++i) {
        text += (i == 0 ? "" : "|") + std::to_string(i);
    }
    CHECK(device_check_memory(true, text).seen.size() == kMaxSeenKeyboards);
}

TEST_CASE("device check offer: the list of seen keyboards is capped, oldest first out") {
    DeviceCheckMemory memory;
    for (std::size_t i = 0; i < kMaxSeenKeyboards + 3; ++i) {
        remember_device_check(memory, "usb:" + std::to_string(i));
    }
    CHECK(memory.seen.size() == kMaxSeenKeyboards);
    CHECK(memory.seen.front() == "usb:3");
    CHECK(memory.seen.back() == "usb:" + std::to_string(kMaxSeenKeyboards + 2));
    CHECK(should_offer_device_check(memory, DeviceCheckTrigger::KeyboardPlugged, "usb:0"));
}
