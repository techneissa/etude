// SPDX-License-Identifier: GPL-3.0-or-later
// The crash screen's state and buttons (pure): what it says after a crash and when an update's data was
// not put back, what each button does, what it reads from the update's status and what it leaves for the
// app it starts. The drawing is checked by the touch rules (crash_screen_layout_test.cpp) and the audit.
#include "crash_notice.hpp"
#include "crash_screen.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <set>

using namespace etude::app;
using namespace upload_test;
using etude::app::ui::CrashButton;

TEST_CASE("crash screen: after a crash it says so and offers Restart and Restart and send report") {
    const ui::CrashScreenView v = crash_screen_view({});
    CHECK(v.title == "Etude closed unexpectedly");
    CHECK(v.detail.find("Nothing is sent unless you send it") != std::string::npos);
    REQUIRE(v.buttons.size() == 2);
    CHECK(v.buttons[0] == CrashButton::Restart);
    CHECK(v.buttons[1] == CrashButton::RestartAndSend);
    CHECK(std::string(ui::crash_button_words(v.buttons[0])) == "Restart");
    CHECK(std::string(ui::crash_button_words(v.buttons[1])) == "Restart and send report");
}

TEST_CASE("crash screen: data that was not put back is told with its reason, and the app is not offered") {
    const ui::CrashScreenView v = crash_screen_view({"the backup is not complete"});
    CHECK(v.title == "Etude did not start");
    CHECK(v.detail.find("Couldn't put your data back: the backup is not complete") != std::string::npos);
    CHECK(v.detail.find("does not start on data that was not put back") != std::string::npos);
    REQUIRE(v.buttons.size() == 2);
    CHECK(v.buttons[0] == CrashButton::TryAgain);
    CHECK(v.buttons[1] == CrashButton::SendReport);
    for (const CrashButton b : v.buttons) {
        CHECK(b != CrashButton::Restart); // never a plain start on data that was not restored
        CHECK(b != CrashButton::RestartAndSend);
    }
}

TEST_CASE("crash screen: what each button does") {
    CHECK(crash_leave_for(CrashButton::Restart) == CrashLeave::Restart);
    CHECK(crash_leave_for(CrashButton::TryAgain) == CrashLeave::Restart); // the app unit's count tries again
    CHECK(crash_leave_for(CrashButton::RestartAndSend) == CrashLeave::RestartAndSend);
    CHECK(crash_leave_for(CrashButton::SendReport) == CrashLeave::ShowReport);
    CHECK(leaves_screen(CrashLeave::Restart));
    CHECK(leaves_screen(CrashLeave::RestartAndSend));
    CHECK_FALSE(leaves_screen(CrashLeave::ShowReport)); // the sheet opens on this screen
    CHECK_FALSE(leaves_screen(CrashLeave::Stay));
}

TEST_CASE("crash screen: every button has its own words and its own id") {
    std::set<std::string> words;
    std::set<std::string> ids;
    for (const CrashButton b : {CrashButton::Restart, CrashButton::RestartAndSend, CrashButton::TryAgain,
                                CrashButton::SendReport}) {
        words.insert(ui::crash_button_words(b));
        ids.insert(ui::crash_button_id(b));
        CHECK(std::string(ui::crash_button_id(b)).rfind("##crash_screen", 0) == 0);
    }
    CHECK(words.size() == 4);
    CHECK(ids.size() == 4);
}

TEST_CASE("crash screen: the reason comes from the status's data line, and nothing else is a reason") {
    CHECK(crash_screen_facts_from_status("state=rolled-back\nversion=1.2.0\nfrom=1.1.0\nreason=x\n"
                                         "data=the backup is not complete\ntime=5\n")
              .data_error == "the backup is not complete");
    CHECK(crash_screen_facts_from_status("state=rolled-back\nreason=went back to 1.1.0\ntime=5\n").data_error.empty());
    CHECK(crash_screen_facts_from_status("").data_error.empty());
    CHECK(crash_screen_facts_from_status("junk without lines").data_error.empty());
    // A line break inside the value becomes a blank, and a long value is cut.
    CHECK(crash_screen_facts_from_status("data=a\tb\n").data_error == "a b");
    CHECK(crash_screen_facts_from_status("data=" + std::string(1000, 'x') + "\n").data_error.size() <= 300);
}

TEST_CASE("crash screen: the status is read as a regular file only, from the update folder") {
    TempDir t;
    CHECK(read_crash_screen_facts({}).data_error.empty());                     // no install
    CHECK(read_crash_screen_facts(t.path / "nowhere").data_error.empty());     // no folder
    CHECK(read_crash_screen_facts(t.path).data_error.empty());                 // no status
    write_file(t.path / "status", "state=failed\ndata=the disk is full\n");
    CHECK(read_crash_screen_facts(t.path).data_error == "the disk is full");
    // A link is never followed, and a file that is too big is not read.
    fs::remove(t.path / "status");
    write_file(t.path / "elsewhere", "data=from a link\n");
    fs::create_symlink(t.path / "elsewhere", t.path / "status");
    CHECK(read_crash_screen_facts(t.path).data_error.empty());
    fs::remove(t.path / "status");
    write_file(t.path / "status", "data=big\n" + std::string(5000, '\n'));
    CHECK(read_crash_screen_facts(t.path).data_error.empty());
}

TEST_CASE("crash screen: Restart notes the report as told, Restart and send report leaves its marker, the rest nothing") {
    TempDir t;
    const std::string name = "20261020-153012.txt";
    write_file(t.path / kCrashFolder / name, "etude crash report\n");
    leave_for_next_start(t.path, CrashLeave::ShowReport);
    leave_for_next_start(t.path, CrashLeave::Stay);
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / kCrashSeenFile));
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / kCrashSendMarker));
    leave_for_next_start(t.path, CrashLeave::RestartAndSend);
    CHECK(fs::exists(t.path / kCrashFolder / kCrashSendMarker));
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / kCrashSeenFile)); // the app tells it, with the sheet open
    CHECK(crash_notice_pending(t.path));
    leave_for_next_start(t.path, CrashLeave::Restart);
    CHECK(read_file(t.path / kCrashFolder / kCrashSeenFile) == name + "\n");
    CHECK_FALSE(crash_notice_pending(t.path));
}

TEST_CASE("crash screen: the old --safe-mode, which units from before still pass, asks for it too") {
    CHECK(is_crash_screen_option("--crash-screen"));
    CHECK(is_crash_screen_option("--safe-mode"));
    CHECK_FALSE(is_crash_screen_option("--safe"));
    CHECK_FALSE(is_crash_screen_option("--crash-screen-reason"));
    CHECK_FALSE(is_crash_screen_option(""));
}

TEST_CASE("crash screen: the developer flag names a button by its words, and nothing else") {
    CHECK(crash_button_named("restart") == ui::CrashButton::Restart);
    CHECK(crash_button_named("restart-send") == ui::CrashButton::RestartAndSend);
    CHECK(crash_button_named("try-again") == ui::CrashButton::TryAgain);
    CHECK(crash_button_named("send-report") == ui::CrashButton::SendReport);
    CHECK_FALSE(crash_button_named("").has_value());
    CHECK_FALSE(crash_button_named("Restart").has_value());
    CHECK_FALSE(crash_button_named("restart ").has_value());
}
