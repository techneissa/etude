// SPDX-License-Identifier: GPL-3.0-or-later
// What the app and the root update helper (helper/) say to each other through small files in
// <prefix>/update/, and the rules for when an update may start and when a new version is ready. Pure
// text in, plain structs out: no file system.
//
//   release.tar.gz, release.tar.gz.minisig
//             the downloaded tarball and its signature, as the app checked them; the helper copies
//             them and checks them again itself
//   request   the app writes `version=X.Y.Z` (one line) last, once both files are in place
//   ready     the new version writes `version=X.Y.Z` when it has reached its first frame and its
//             history has opened and migrated (ReadyGate)
//   status    the helper writes how a run ended: state, version, from, reason, time (key=value lines)
//
// The folder is the app's, so the helper reads all of it as untrusted; the app reads the status the
// same way.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// The names of the files in <prefix>/update/ (the list above).
inline constexpr std::string_view kUpdateTarballFile = "release.tar.gz";
inline constexpr std::string_view kUpdateSignatureFile = "release.tar.gz.minisig";
inline constexpr std::string_view kUpdateRequestFile = "request";
inline constexpr std::string_view kUpdateReadyFile = "ready";
inline constexpr std::string_view kUpdateStatusFile = "status";

// "version=1.2.3\n", or empty when `version` is not a plain X.Y.Z version.
std::string format_update_request(std::string_view version);
// The same line: what the new version writes when its first frame is on screen.
std::string format_update_ready(std::string_view version);

enum class UpdateState : std::uint8_t {
    None,       // no status, or one this build does not know
    Applying,   // the helper is working
    Ok,         // the new version runs
    RolledBack, // the new version did not start; the old one is back
    Failed,     // refused before anything was switched
};

struct UpdateStatus {
    UpdateState state = UpdateState::None;
    std::string version; // the version the update was for
    std::string from;    // the version that ran before it
    std::string reason;  // one line, for the player
    // Set only while a going back could not put the data back: why, one line. The crash screen shows
    // it and does not start the app; any later status (the restore done) has none.
    std::string data;
    std::int64_t time_s = 0;
};

// Reads the helper's status file; unknown keys and bad lines are skipped, a value over 300 bytes is
// cut, and a version that is not X.Y.Z is dropped.
UpdateStatus parse_update_status(std::string_view text);


// How the history database stands at a start: its file opened and every migration applied.
enum class HistoryOpen : std::uint8_t {
    Pending, // the history thread is still opening it (or it is busy and the open is retried)
    Opened,  // opened and migrated: the runs are in
    Failed,  // it could not be opened or migrated this session
};

// The rule for the `ready` file: written once, when the first frame is on screen and the history
// has opened and migrated. A history that failed to open or migrate makes it never written, so the
// start counts as failed and the helper goes back to the old version.
class ReadyGate {
  public:
    // True exactly once: on the first call that has both the frame and an opened history.
    bool update(bool frame_shown, HistoryOpen history) {
        if (history == HistoryOpen::Failed) {
            failed_ = true;
        }
        if (written_ || failed_ || !frame_shown || history != HistoryOpen::Opened) {
            return false;
        }
        written_ = true;
        return true;
    }
    bool failed() const { return failed_; }
    // Nothing more will come of calling update().
    bool settled() const { return written_ || failed_; }

  private:
    bool written_ = false;
    bool failed_ = false;
};

// What is going on in the app, for the rule below.
struct UpdateActivity {
    bool playing = false;      // a song is playing
    bool counting_in = false;  // the count-in before it
    bool run_in_progress = false; // a run of trouble bars is on (a bar looping, or the check-in)
    bool recording = false;    // notes are being recorded
};

// An update starts, and the app is restarted for it, only from the idle screen: never while a song
// plays, counts in, a run is on or a recording is going.
inline bool update_may_start(const UpdateActivity& a) {
    return !(a.playing || a.counting_in || a.run_in_progress || a.recording);
}

} // namespace etude
