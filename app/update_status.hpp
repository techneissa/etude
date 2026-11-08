// SPDX-License-Identifier: GPL-3.0-or-later
// The words of the About page's Updates card and the rule for the daily check, as pure functions
// of the view and the clock, so tests/update_status_test.cpp holds them. No SDL.
#pragma once

#include "net_fetch.hpp"
#include "ui/about_port.hpp"

#include "etude/minisign.hpp"
#include "etude/update_handoff.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace etude::app {

// How long after an automatic attempt, a failed one too, the next one is due: at most one a day.
// "Check now" does not wait for it.
inline constexpr std::int64_t kAutoCheckEveryS = 24 * 60 * 60;

// The card's one status line: "Up to date", "Version 1.2.3 available", the reason of a failure.
std::string update_status_text(const ui::UpdateView& view);

// Why something failed, in two lengths: `full` is the whole reason, as it stands (the page shows it
// when it fits its line, and behind the (!) icon when it does not), `summary` a short form of it that
// fits the line on one or two lines. The one place that turns a failure into the player's text:
// today the check uses `fetch_failure_text`; the signature, space and helper texts are for the
// install path (the next change) and are tested and shown by the demo here.
struct FailureText {
    std::string full;
    std::string summary;
};

// A fetch that did not give its answer: GitHub's status ("GitHub answered 403 (rate limited)") or
// what went wrong reaching it ("Couldn't connect: timed out after 15 s").
FailureText fetch_failure_text(const FetchResult& result);

// A signature check that did not pass ("Signature doesn't match the release key").
FailureText signature_failure_text(SignatureError error);

// The update folder has no room for the download ("Not enough space: needs 24 MB").
FailureText space_failure_text(std::uint64_t needed_bytes);

// The helper's own words for an update that did not happen (`reason` as its status file has it):
// "Update to 1.2.0 failed: ..." in full, and a short form by what kind of trouble it names.
FailureText helper_failure_text(std::string_view version, std::string_view reason);

// What the About page says of the helper's status file: its words for a failed or rolled-back update
// (`helper_failure_text` of its version and reason), nothing for a status that needs none (none,
// applying, ok).
FailureText update_status_failure(const UpdateStatus& status);

// "Last checked 5 min ago"; empty when it never was; "Last checked: unknown" when the stored time
// is in the future (the clock was wrong when it was written).
std::string last_checked_text(std::int64_t now_s, std::int64_t last_s);

// True when "Check automatically" is on and a day has passed since the last attempt, whether it got
// an answer or not (`last_ok_s`, `last_try_s`: unix seconds, 0 for none). A time in the future is
// not trusted and counts as none.
bool auto_check_due(bool enabled, std::int64_t now_s, std::int64_t last_ok_s,
                    std::int64_t last_try_s);

// Whether the card shows "Check now" (and the switch): not while a check, a download or an
// install is on, and not in a build without the update key.
bool can_check_now(const ui::UpdateView& view);

// A name for the address a link sheet shows: "Etude on GitHub", "Buy Me a Coffee", "Release
// notes"; the address itself for any other.
std::string link_title(std::string_view url);

// The address as people type it: without "https://" and a trailing "/".
std::string link_address(std::string_view url);

} // namespace etude::app
