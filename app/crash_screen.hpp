// SPDX-License-Identifier: GPL-3.0-or-later
// The crash screen's words, buttons and what each button does (`etude --crash-screen`, started by
// systemd after the app crashed or was ended by its watchdog). Pure, apart from the two functions
// that read the update's status and leave a note for the app that starts next. No SDL.
//
// Two cases: the app crashed (title "Etude closed unexpectedly"; Restart, and Restart and send
// report), and the app did not start because an update's going back could not put the data back
// (the reason, `Couldn't put your data back: ...`; Try again, and Send report). In the second the app
// never starts on data that was not restored: Try again starts the app unit, whose first step tries
// the restore again.
#pragma once

#include "ui/crash_screen_view.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace etude::app {

// Whether a command-line argument asks for the crash screen. `--safe-mode` is the old name: the units
// of an install that was not run through deploy/pi/setup.sh again still start `etude --safe-mode` after
// a crash, and an unknown option there would end with the usage text and a black screen.
bool is_crash_screen_option(std::string_view argument);

// What the screen knows when it opens.
struct CrashScreenFacts {
    std::string data_error; // why the update's data was not put back ("" when it was, or there was no update)
};

ui::CrashScreenView crash_screen_view(const CrashScreenFacts& facts);

// What a tap does next.
enum class CrashLeave : std::uint8_t {
    Stay,           // nothing leaves the screen
    ShowReport,     // the report sheet opens on this screen
    Restart,        // the screen exits 0: systemd starts the app
    RestartAndSend, // the same, and the app opens the report sheet after its start
};
CrashLeave crash_leave_for(ui::CrashButton button);
bool leaves_screen(CrashLeave leave);

// The developer flag `--crash-screen-press NAME` taps a button by name once the screen is up, so a
// container test with no one at the screen can press it: restart, restart-send, try-again or
// send-report. Null for any other word.
std::optional<ui::CrashButton> crash_button_named(std::string_view name);

// The helper's status file (`<prefix>/update/status`) read for the reason in its `data` line. Anything
// that is not a status leaves no reason.
CrashScreenFacts crash_screen_facts_from_status(std::string_view status_text);

// The facts of this run: the status in the update folder next to the running program ("" for a
// program that does not run from an install: a Mac, a build folder). Reads a regular file only.
CrashScreenFacts read_crash_screen_facts(const std::filesystem::path& update_dir);

// What the screen leaves for the app it starts, in `data_dir`: Restart notes the newest report as told
// (no notice for a crash the player was just told about); Restart and send report leaves the marker the
// next start opens the report sheet by. Nothing for the other results.
void leave_for_next_start(const std::filesystem::path& data_dir, CrashLeave leave);

} // namespace etude::app
