// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in for the app's start in the update helper's container test (scripts/update-helper-test.sh):
// what a freshly installed etude does when its first frame is on screen, with the app's own code for
// it (core ReadyGate decides, app/update_files.cpp writes <update dir>/ready), and the history's state
// given on the command line instead of a database. Then it runs until it is killed, as the app does.
//
//   update-ready-probe --update-dir DIR --version X.Y.Z --history ok|failed|pending [--pings-for SECONDS]
//
// --pings-for: answers the unit's watchdog the way the frame loop does (the app's own code, app/watchdog.cpp,
// a ping every quarter of a second of this loop's turns) for that many seconds, or for ever when it is -1,
// and then hangs: no ping, no exit. Without it nothing is sent.
#include "update_files.hpp"
#include "watchdog.hpp"

#include "etude/update_handoff.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    std::string dir;
    std::string version;
    std::string history = "ok";
    double pings_for = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--update-dir") == 0) {
            dir = argv[i + 1];
        } else if (std::strcmp(argv[i], "--version") == 0) {
            version = argv[i + 1];
        } else if (std::strcmp(argv[i], "--history") == 0) {
            history = argv[i + 1];
        } else if (std::strcmp(argv[i], "--pings-for") == 0) {
            pings_for = std::atof(argv[i + 1]);
        }
    }
    const etude::HistoryOpen state = history == "ok"       ? etude::HistoryOpen::Opened
                                       : history == "failed" ? etude::HistoryOpen::Failed
                                                             : etude::HistoryOpen::Pending;
    etude::ReadyGate gate;
    // Frames, as the window draws them: each one says how the start stands, until it is settled.
    for (int frame = 0; frame < 3; ++frame) {
        if (!gate.settled() && gate.update(true, state)) {
            if (!etude::app::write_update_ready(dir, version)) {
                std::fprintf(stderr, "probe: could not write ready\n");
            }
        }
        ::usleep(100'000);
    }
    if (pings_for != 0) {
        etude::app::Watchdog watchdog;
        const auto began = std::chrono::steady_clock::now();
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            if (pings_for > 0 && std::chrono::duration<double>(now - began).count() > pings_for) {
                break;
            }
            watchdog.ping(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
            ::usleep(250'000);
        }
    }
    for (;;) {
        ::pause();
    }
}
