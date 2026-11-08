// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_status.hpp"

#include "etude/update_feed.hpp"

#include <algorithm>

namespace etude::app {

using ui::UpdatePhase;

namespace {

// A stored time this far ahead of the clock is not a time that happened.
constexpr std::int64_t kClockSlackS = 60;

} // namespace

std::string update_status_text(const ui::UpdateView& view) {
    switch (view.phase) {
    case UpdatePhase::NotSetUp:
        return "Updates are not set up in this build";
    case UpdatePhase::NotChecked:
        return view.last_checked_s > 0 ? "Not checked since starting" : "Not checked yet";
    case UpdatePhase::Checking:
        return "Checking…";
    case UpdatePhase::UpToDate:
        return "Up to date";
    case UpdatePhase::Available:
        return "Version " + view.version + " available";
    case UpdatePhase::Downloading:
        return "Downloading version " + view.version;
    case UpdatePhase::Installing:
        return "Installing…";
    case UpdatePhase::Failed:
        return view.failure.empty() ? std::string("Failed") : view.failure;
    }
    return {};
}

std::string last_checked_text(std::int64_t now_s, std::int64_t last_s) {
    if (last_s <= 0) {
        return {};
    }
    const std::int64_t ago = now_s - last_s;
    if (ago < -kClockSlackS) {
        return "Last checked: unknown";
    }
    if (ago < 60) {
        return "Last checked just now";
    }
    if (ago < 60 * 60) {
        return "Last checked " + std::to_string(ago / 60) + " min ago";
    }
    if (ago < 24 * 60 * 60) {
        return "Last checked " + std::to_string(ago / 3600) + " h ago";
    }
    const std::int64_t days = ago / (24 * 60 * 60);
    return days == 1 ? std::string("Last checked yesterday")
                     : "Last checked " + std::to_string(days) + " days ago";
}

namespace {

std::string status_words(int status) {
    switch (status) {
    case 403:
        return "GitHub answered 403 (rate limited)";
    case 429:
        return "GitHub answered 429 (too many requests)";
    case 404:
        return "GitHub answered 404 (not found)";
    default:
        break;
    }
    if (status >= 500) {
        return "GitHub answered " + std::to_string(status) + " (server problem)";
    }
    return "GitHub answered " + std::to_string(status);
}

std::string status_summary(int status) {
    switch (status) {
    case 403:
        return "GitHub rate limit (403)";
    case 429:
        return "Too many requests (429)";
    case 404:
        return "Not found (404)";
    default:
        break;
    }
    return status >= 500 ? "GitHub problem (" + std::to_string(status) + ")"
                         : "GitHub answered " + std::to_string(status);
}

bool has(std::string_view text, std::string_view word) {
    return text.find(word) != std::string_view::npos;
}

} // namespace

FailureText fetch_failure_text(const FetchResult& r) {
    if (r.status >= 400) {
        return {status_words(r.status), status_summary(r.status)};
    }
    const std::string cause = r.detail.empty() ? std::string() : ": " + r.detail;
    if (r.reason == "The site took too long") {
        return {"Couldn't connect" + (r.detail.empty() ? std::string(": timed out") : cause),
                "Connection timed out"};
    }
    if (has(r.reason, "certificate")) {
        return {"Couldn't verify GitHub's certificate" + cause, "Certificate check failed"};
    }
    if (r.reason == "The file is too large") {
        return {"The download is larger than the release said", "Download too large"};
    }
    if (r.reason == "No connection" || r.reason.empty()) {
        return {"Couldn't connect" + cause, "Couldn't connect"};
    }
    return {r.reason + cause, r.reason};
}

FailureText signature_failure_text(SignatureError error) {
    switch (error) {
    case SignatureError::None:
        return {};
    case SignatureError::KeyNotSet:
        return {"Updates are not set up in this build", "Updates are not set up"};
    case SignatureError::BadKey:
        return {"The release key built into this app is not valid", "Release key not valid"};
    case SignatureError::BadSignatureFile:
    case SignatureError::UnsupportedAlgorithm:
        return {"The signature file is damaged", "Signature file damaged"};
    case SignatureError::WrongKeyId:
        return {"Signed by another key than the release key", "Signed by another key"};
    case SignatureError::TrustedCommentMismatch:
        return {"Signed for a different release than this one", "Signed for another release"};
    case SignatureError::BadSignature:
    case SignatureError::BadTrustedCommentSignature:
        break;
    }
    return {"Signature doesn't match the release key", "Signature doesn't match"};
}

FailureText space_failure_text(std::uint64_t needed_bytes) {
    const std::uint64_t mb = (needed_bytes + (1024 * 1024 - 1)) / (1024 * 1024);
    return {"Not enough space: needs " + std::to_string(mb) + " MB", "Not enough space"};
}

FailureText helper_failure_text(std::string_view version, std::string_view reason) {
    const std::string head = version.empty() ? std::string("Update failed")
                                             : "Update to " + std::string(version) + " failed";
    FailureText out;
    out.full = reason.empty() ? head : head + ": " + std::string(reason);
    if (has(reason, "No space left")) {
        out.summary = "Not enough space";
    } else if (has(reason, "signature")) {
        out.summary = "Signature check failed";
    } else if (has(reason, "backed up")) {
        out.summary = "Data backup failed";
    } else if (has(reason, "first frame")) {
        out.summary = "New version didn't start";
    } else if (has(reason, "still running")) {
        out.summary = "App could not be stopped";
    } else {
        out.summary = head;
    }
    return out;
}

FailureText update_status_failure(const UpdateStatus& status) {
    if (status.state != UpdateState::RolledBack && status.state != UpdateState::Failed) {
        return {};
    }
    return helper_failure_text(status.version, status.reason);
}

bool auto_check_due(bool enabled, std::int64_t now_s, std::int64_t last_ok_s,
                    std::int64_t last_try_s) {
    if (!enabled) {
        return false;
    }
    const std::int64_t last = std::max(last_ok_s, last_try_s);
    return last <= 0 || last > now_s + kClockSlackS || now_s - last >= kAutoCheckEveryS;
}

bool can_check_now(const ui::UpdateView& view) {
    switch (view.phase) {
    case UpdatePhase::NotChecked:
    case UpdatePhase::UpToDate:
    case UpdatePhase::Available:
    case UpdatePhase::Failed:
        return true;
    case UpdatePhase::NotSetUp:
    case UpdatePhase::Checking:
    case UpdatePhase::Downloading:
    case UpdatePhase::Installing:
        return false;
    }
    return false;
}

std::string link_title(std::string_view url) {
    if (url == kRepositoryUrl) {
        return "Etude on GitHub";
    }
    if (url == kBuyMeACoffeeUrl) {
        return "Buy Me a Coffee";
    }
    if (url.find("/releases/") != std::string_view::npos) {
        return "Release notes";
    }
    return std::string(url);
}

std::string link_address(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.substr(0, kScheme.size()) == kScheme) {
        url.remove_prefix(kScheme.size());
    }
    while (!url.empty() && url.back() == '/') {
        url.remove_suffix(1);
    }
    return std::string(url);
}

} // namespace etude::app
