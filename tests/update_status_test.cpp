// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/update_status.hpp"
#include "failure_samples.hpp"

#include "etude/update_feed.hpp"

#include <doctest/doctest.h>


using namespace etude;
using namespace etude::app;
using ui::UpdatePhase;
using ui::UpdateView;

namespace {

UpdateView in(UpdatePhase phase) {
    UpdateView v;
    v.phase = phase;
    v.version = "1.2.3";
    return v;
}

} // namespace

TEST_CASE("update status: one line for each state") {
    CHECK(update_status_text(in(UpdatePhase::NotSetUp)) == "Updates are not set up in this build");
    CHECK(update_status_text(in(UpdatePhase::NotChecked)) == "Not checked yet");
    CHECK(update_status_text(in(UpdatePhase::Checking)) == "Checking\xE2\x80\xA6");
    CHECK(update_status_text(in(UpdatePhase::UpToDate)) == "Up to date");
    CHECK(update_status_text(in(UpdatePhase::Available)) == "Version 1.2.3 available");
    CHECK(update_status_text(in(UpdatePhase::Downloading)) == "Downloading version 1.2.3");
    CHECK(update_status_text(in(UpdatePhase::Installing)) == "Installing\xE2\x80\xA6");
    UpdateView failed = in(UpdatePhase::Failed);
    CHECK(update_status_text(failed) == "Failed");
    failed.failure = "Could not check: No connection";
    CHECK(update_status_text(failed) == "Could not check: No connection");
}

TEST_CASE("update status: Check now is offered unless something is running or nothing is set up") {
    CHECK(can_check_now(in(UpdatePhase::NotChecked)));
    CHECK(can_check_now(in(UpdatePhase::UpToDate)));
    CHECK(can_check_now(in(UpdatePhase::Available)));
    CHECK(can_check_now(in(UpdatePhase::Failed)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::Checking)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::Downloading)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::Installing)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::NotSetUp)));
}

TEST_CASE("update status: last checked reads like a person would say it") {
    constexpr std::int64_t now = 1'800'000'000;
    CHECK(last_checked_text(now, 0).empty());
    CHECK(last_checked_text(now, now) == "Last checked just now");
    CHECK(last_checked_text(now, now + 30) == "Last checked just now");
    CHECK(last_checked_text(now, now + 500) == "Last checked: unknown"); // from a wrong clock
    CHECK(last_checked_text(now, now - 59) == "Last checked just now");
    CHECK(last_checked_text(now, now - 60) == "Last checked 1 min ago");
    CHECK(last_checked_text(now, now - 59 * 60) == "Last checked 59 min ago");
    CHECK(last_checked_text(now, now - 3600) == "Last checked 1 h ago");
    CHECK(last_checked_text(now, now - 23 * 3600) == "Last checked 23 h ago");
    CHECK(last_checked_text(now, now - 24 * 3600) == "Last checked yesterday");
    CHECK(last_checked_text(now, now - 47 * 3600) == "Last checked yesterday");
    CHECK(last_checked_text(now, now - 3 * 24 * 3600) == "Last checked 3 days ago");
}

TEST_CASE("update status: the daily check is due a day after a good check, never when off") {
    constexpr std::int64_t now = 1'800'000'000;
    CHECK_FALSE(auto_check_due(false, now, 0, 0));
    CHECK(auto_check_due(true, now, 0, 0));                           // never checked
    CHECK_FALSE(auto_check_due(true, now, now - kAutoCheckEveryS + 1, 0));
    CHECK(auto_check_due(true, now, now - kAutoCheckEveryS, 0));
    // A failed attempt counts: one attempt a day, however old the last good check is.
    CHECK_FALSE(auto_check_due(true, now, 0, now - 60));
    CHECK_FALSE(auto_check_due(true, now, now - 3 * kAutoCheckEveryS, now - 3600));
    CHECK_FALSE(auto_check_due(true, now, now - 3 * kAutoCheckEveryS, now - kAutoCheckEveryS + 1));
    CHECK(auto_check_due(true, now, now - 3 * kAutoCheckEveryS, now - kAutoCheckEveryS));
    // A time from the future is not trusted: the check is due.
    CHECK(auto_check_due(true, now, now + 10 * kAutoCheckEveryS, 0));
}

TEST_CASE("update status: names for the addresses a link sheet shows") {
    CHECK(link_title(kRepositoryUrl) == "Etude on GitHub");
    CHECK(link_title(kBuyMeACoffeeUrl) == "Buy Me a Coffee");
    CHECK(link_title("https://github.com/techneissa/etude/releases/tag/v1.2.3") ==
          "Release notes");
    CHECK(link_title("https://example.org/x") == "https://example.org/x");
}

TEST_CASE("update status: the address a link sheet types out") {
    CHECK(link_address("https://github.com/techneissa/etude") == "github.com/techneissa/etude");
    CHECK(link_address("https://buymeacoffee.com/techneissa/") == "buymeacoffee.com/techneissa");
    CHECK(link_address("http://example.org") == "http://example.org");
    CHECK(link_address("").empty());
}


TEST_CASE("failure text: a fetch says GitHub's status or what went wrong reaching it") {
    const FailureText limit = fetch_failure_text(fetched(403, "Not allowed"));
    CHECK(limit.full == "GitHub answered 403 (rate limited)");
    CHECK(limit.summary == "GitHub rate limit (403)");
    CHECK(fetch_failure_text(fetched(404, "Not found")).full == "GitHub answered 404 (not found)");
    CHECK(fetch_failure_text(fetched(429, "")).full == "GitHub answered 429 (too many requests)");
    CHECK(fetch_failure_text(fetched(503, "")).full == "GitHub answered 503 (server problem)");
    CHECK(fetch_failure_text(fetched(503, "")).summary == "GitHub problem (503)");
    CHECK(fetch_failure_text(fetched(418, "")).full == "GitHub answered 418");

    const FailureText slow =
        fetch_failure_text(fetched(0, "The site took too long", "timed out after 15 s"));
    CHECK(slow.full == "Couldn't connect: timed out after 15 s");
    CHECK(slow.summary == "Connection timed out");
    CHECK(fetch_failure_text(fetched(0, "The site took too long")).full ==
          "Couldn't connect: timed out");
    CHECK(fetch_failure_text(fetched(0, "No connection")).full == "Couldn't connect");
    CHECK(fetch_failure_text(fetched(0, "No connection", "could not resolve host")).full ==
          "Couldn't connect: could not resolve host");
    CHECK(fetch_failure_text(fetched(0, "")).full == "Couldn't connect");
    CHECK(fetch_failure_text(fetched(0, "The file is too large")).full ==
          "The download is larger than the release said");
    CHECK(fetch_failure_text(fetched(0, "The site's certificate could not be checked", "certificate has expired")).summary ==
          "Certificate check failed");
}

TEST_CASE("failure text: every reason is whole and the short form never longer") {
    for (const FailureText& t : every_failure_text()) {
        CAPTURE(t.full);
        CHECK_FALSE(t.full.empty());
        CHECK_FALSE(t.summary.empty());
        CHECK(t.summary.size() <= t.full.size());
        CHECK(t.full.find("...") == std::string::npos);
        CHECK(t.full.find("\xE2\x80\xA6") == std::string::npos); // never cut with an ellipsis
    }
}

TEST_CASE("failure text: a signature that does not match, a key that is not ours") {
    CHECK(signature_failure_text(SignatureError::BadSignature).full ==
          "Signature doesn't match the release key");
    CHECK(signature_failure_text(SignatureError::BadTrustedCommentSignature).full ==
          "Signature doesn't match the release key");
    CHECK(signature_failure_text(SignatureError::WrongKeyId).full ==
          "Signed by another key than the release key");
    CHECK(signature_failure_text(SignatureError::TrustedCommentMismatch).summary ==
          "Signed for another release");
    CHECK(signature_failure_text(SignatureError::None).full.empty());
}

TEST_CASE("failure text: not enough space says how much, rounded up to a whole MB") {
    CHECK(space_failure_text(24ull * 1024 * 1024).full == "Not enough space: needs 24 MB");
    CHECK(space_failure_text(24ull * 1024 * 1024 + 1).full == "Not enough space: needs 25 MB");
    CHECK(space_failure_text(1).full == "Not enough space: needs 1 MB");
    CHECK(space_failure_text(5).summary == "Not enough space");
}

TEST_CASE("failure text: the helper's words stand whole, with a short form by kind") {
    const FailureText full = helper_failure_text("1.2.0", "the new version did not reach its first frame");
    CHECK(full.full == "Update to 1.2.0 failed: the new version did not reach its first frame");
    CHECK(full.summary == "New version didn't start");
    CHECK(helper_failure_text("1.2.0", "No space left on device").summary == "Not enough space");
    CHECK(helper_failure_text("1.2.0", "data could not be backed up").summary == "Data backup failed");
    CHECK(helper_failure_text("1.2.0", "the signature does not match").summary ==
          "Signature check failed");
    CHECK(helper_failure_text("1.2.0", "the app is still running").summary ==
          "App could not be stopped");
    CHECK(helper_failure_text("1.2.0", "odd").summary == "Update to 1.2.0 failed");
    CHECK(helper_failure_text("", "odd").full == "Update failed: odd");
    CHECK(helper_failure_text("1.2.0", "").full == "Update to 1.2.0 failed");
}

TEST_CASE("failure text: only a failed or rolled-back update has helper words for the page") {
    UpdateStatus s;
    s.version = "1.2.0";
    s.reason = "the new version did not reach its first frame";
    CHECK(update_status_failure(s).full.empty());
    s.state = UpdateState::Ok;
    CHECK(update_status_failure(s).full.empty());
    s.state = UpdateState::Applying;
    CHECK(update_status_failure(s).full.empty());
    s.state = UpdateState::RolledBack;
    CHECK(update_status_failure(s).full ==
          "Update to 1.2.0 failed: the new version did not reach its first frame");
    CHECK(update_status_failure(s).summary == "New version didn't start");
    s.state = UpdateState::Failed;
    s.reason.clear();
    CHECK(update_status_failure(s).full == "Update to 1.2.0 failed");
}
