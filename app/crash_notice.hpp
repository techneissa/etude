// SPDX-License-Identifier: GPL-3.0-or-later
// The reports in `<data folder>/crashes/` as the next start sees them: the newest ones kept, the
// report the notice is about (once per report: the folder remembers the newest told), its text with
// every path scrubbed, and the addresses that file it as an issue. No SDL.
#pragma once

#include "etude/crash_report.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace etude::app {

inline constexpr const char* kCrashFolder = "crashes";
inline constexpr const char* kCrashSeenFile = "seen";

struct CrashNotice {
    std::string name;            // the report's file
    std::string text;            // the report, scrubbed: what the player reads and what is kept
    etude::CrashSummary summary;
    std::string url;             // the issue address for a browser (kCrashUrlMax)
    std::string qr_url;          // the short one for a QR code (kCrashQrUrlMax)
};

// Keeps the newest `kCrashReportsKept` reports and returns the notice for the newest one not told
// yet, writing the folder's note that it was.
std::optional<CrashNotice> start_crash_notice(const std::filesystem::path& data_dir,
                                              const etude::ScrubRules& rules);

// The crash screen's own report: the newest one whether it was told or not, scrubbed as the notice
// is, and nothing noted. Null when there is none.
std::optional<CrashNotice> latest_crash_notice(const std::filesystem::path& data_dir,
                                               const etude::ScrubRules& rules);

// A report for a start that did not happen because the update's data was not put back (there may be
// no crash report to send): the version, the system and the reason, with the addresses that file it.
CrashNotice restore_failure_notice(const std::string& reason, const etude::ScrubRules& rules);

// What the crash screen leaves for the app it starts (both in the reports folder, so the app finds
// them with the reports): `mark_crash_reports_seen` notes the newest report as told, so the app shows
// no notice for a crash the player was just told about; `leave_send_marker` asks the next start to open
// the report sheet at once ("Restart and send report"), and `take_send_marker` reads it once.
void mark_crash_reports_seen(const std::filesystem::path& data_dir);
bool leave_send_marker(const std::filesystem::path& data_dir);
bool take_send_marker(const std::filesystem::path& data_dir);
inline constexpr const char* kCrashSendMarker = "send-report";

// The rules of this machine: the home folder, and the app's folders given.
etude::ScrubRules scrub_rules_for(const std::filesystem::path& data_dir,
                                    const std::filesystem::path& install_dir);

// True when a report not told yet is in `data_dir`/crashes (nothing is changed).
bool crash_notice_pending(const std::filesystem::path& data_dir);

// The folder the program lives in, from SDL's base path (the folder of the executable, ending in a
// slash): `<prefix>/versions/X` for `<prefix>/versions/X/bin/`, the .app for a Mac bundle's
// Resources folder.
std::filesystem::path install_dir_of(std::string_view base_path);

// The notice made when the profiles' names are known, so the report is scrubbed of them too: the
// profiles load on the history's own thread after the start. `defer` once at the start, then `poll`
// each frame until it gives the notice (or nothing, once, when there is none to tell).
class CrashNoticeStart {
  public:
    // `send`: "Restart and send report" asked for the sheet at this start, so the newest report is the
    // notice even when it was told already.
    void defer(const std::filesystem::path& data_dir, bool send = false);
    bool waiting() const { return waiting_; }
    // Nothing while the names are not known (`names_known` false) or after the one answer.
    std::optional<CrashNotice> poll(bool names_known, const etude::ScrubRules& rules);

  private:
    std::filesystem::path data_dir_;
    bool send_ = false;
    bool waiting_ = false;
};

// A report made up for the developer's shots (`--crash-demo`): a Mac's trace or a Pi's.
CrashNotice demo_crash_notice(bool mac);

} // namespace etude::app
