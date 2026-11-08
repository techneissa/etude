// SPDX-License-Identifier: GPL-3.0-or-later
// The pure side of crash reports: the name of a report file, what a log line may keep of a path,
// what a report says in short, the address that opens a prefilled issue, which reports to keep, and
// which one the next start tells the player about. No file system, no clock: the app writes the
// reports (app/crash_handler.*) and reads them back (app/crash_notice.*).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// The newest reports kept in the data folder's `crashes/`.
inline constexpr std::size_t kCrashReportsKept = 10;

// `YYYYMMDD-HHMMSS.txt` (UTC) for a time in seconds since 1970, written into `out` (at least
// `kCrashNameChars` long) with a closing zero. Plain arithmetic and no allocation, so a signal
// handler may call it. The names sort in the order the crashes came.
inline constexpr std::size_t kCrashNameChars = 24;
void crash_report_name(std::int64_t epoch_seconds, char* out);
bool is_crash_report_name(std::string_view name);

// What a line may say about where it is. A path inside one of the app's folders stays (its folder
// written from the home folder as `~`), any other path becomes `<path>`, and a profile's name
// becomes `<profile>`.
struct ScrubRules {
    std::string home;                  // the home folder, "" when unknown
    std::vector<std::string> app_dirs; // the data folder, the install folder, ...
    std::vector<std::string> names;    // the profiles' names
};
std::string scrub_line(std::string_view line, const ScrubRules& rules);
// Every line of `text`, scrubbed.
std::string scrub_text(std::string_view text, const ScrubRules& rules);

// What the issue's address and the notice say of a report, read back from its text.
struct CrashSummary {
    std::string version;
    std::string os;
    std::string signal; // "SIGSEGV (11)", "uncaught C++ exception"
    std::string screen;
    std::vector<std::string> frames; // the top frames, the handler's own left out
};
CrashSummary summarize_crash(std::string_view report);

// Where an issue is filed, and the limits of the address: the browser's (a Mac opens it) and what a
// phone's camera reads comfortably from a QR code on the 7 inch panel (the Pi shows it).
inline constexpr std::size_t kCrashUrlMax = 6000;
inline constexpr std::size_t kCrashQrUrlMax = 250; // a QR code of version 11 at medium correction, 65 modules with its quiet zone
// `https://github.com/techneissa/etude/issues/new?title=...&body=...`, at most `max_chars` long:
// frames are dropped from the end, and then the body is cut, until it fits.
std::string crash_issue_url(const CrashSummary& summary, std::size_t max_chars);
std::string percent_encode(std::string_view text);

// The oldest of `names` beyond the `keep` newest report names (other names are never listed).
std::vector<std::string> reports_to_prune(std::vector<std::string> names, std::size_t keep);

// Which report the start tells about: the newest one past `seen` (the newest already told).
struct CrashNoticePlan {
    std::string show; // a report's name, "" for none
    std::string seen; // the new `seen`
};
CrashNoticePlan plan_crash_notice(const std::vector<std::string>& names, const std::string& seen);

// The newest report of `names` whether it was told or not: what the crash screen sends. "" for none.
std::string newest_crash_report(const std::vector<std::string>& names);

} // namespace etude
