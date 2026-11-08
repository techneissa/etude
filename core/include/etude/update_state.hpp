// SPDX-License-Identifier: GPL-3.0-or-later
// What the root update helper (helper/) remembers between runs, in <state dir>/state, so a power cut
// or a reboot in the middle of an update leaves it knowing where it was: the version it is moving
// from and to, the version `previous` pointed at, the service it stopped, and how many times the new
// version has been started without reaching its first frame. Pure text in and out: no file system.
// The file is root's and written whole (temp file, fsync, rename), so it is read strictly: a line
// that does not fit is an error, not a guess.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

enum class UpdatePhase : std::uint8_t {
    None,        // no update is being settled
    Switching,   // `current` is being pointed at the new version (it may or may not be yet)
    Pending,     // the new version is current and has not yet reached its first frame
    RollingBack, // going back: `current`, `previous` and the data are being put as they were
};

struct PendingUpdate {
    UpdatePhase phase = UpdatePhase::None;
    std::string from;     // the version that ran before: X.Y.Z
    std::string to;       // the version being installed
    std::string previous; // the version `previous` pointed at before this update ("" for none)
    std::string unit;     // the service that was running: etude.service, etude-labwc.service, or ""
    int starts = 0;       // starts of the new version counted since the switch
    bool backed_up = false; // the app's data was backed up before the switch, so a rollback restores it
    std::string reason;   // for a rollback: why, one line
    int restores = 0;     // going back: attempts to put the data back that failed so far (for the journal)
    std::string data_error; // going back: why the last attempt failed, one line ("" for none)
};

struct PendingParse {
    PendingUpdate state;
    bool ok = true;
    std::string error; // "line 2: from is not a version", when not ok
};

inline constexpr std::size_t kMaxStateBytes = 2048;

std::string format_pending_update(const PendingUpdate& state);

// A file with no phase line, or an empty one, is phase None. Every other line must be a known key
// with a valid value; a phase other than None needs `from` and `to`.
PendingParse parse_pending_update(std::string_view text);

// The services the helper may stop and start.
bool known_app_unit(std::string_view unit);

// The unit that shows the crash screen after `app_unit` failed (etude-crash-screen.service for
// etude.service, etude-labwc-crash-screen.service for the labwc kiosk); "" for any other name.
std::string crash_screen_unit(std::string_view app_unit);

} // namespace etude
