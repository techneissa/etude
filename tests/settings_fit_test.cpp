// SPDX-License-Identifier: GPL-3.0-or-later
// The audit's two settings checks fail on known bad cases: a label cut with an ellipsis and a page
// that scrolls, at 1366x768 and up, and only list them on a small screen (app/ui/settings_fit.hpp).
#include "../app/ui/fit_text.hpp"
#include "../app/ui/settings_fit.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <string>

using namespace etude::app::ui;

namespace {

// "Replays kept per song" in a room too short for it, cut as the kit cuts it (one unit a byte).
std::string cut_to_room(float room) {
    auto measure = [](const char* line) { return static_cast<float>(std::strlen(line)); };
    return std::string(shorten_line("Replays kept per song", room, measure).view());
}

} // namespace

TEST_CASE("settings fit: a label cut with an ellipsis fails at 1366x768 and 1920x1080") {
    const std::string shown = cut_to_room(20.0f);
    REQUIRE(shown != "Replays kept per song");
    CHECK(shown.size() >= 3);
    CHECK(shown.substr(shown.size() - kEllipsis.size()) == std::string(kEllipsis));
    CHECK(text_was_cut("Replays kept per song", shown));
    CHECK(cut_verdict(1366.0f, 768.0f) == FitVerdict::Fail);
    CHECK(cut_verdict(1920.0f, 1080.0f) == FitVerdict::Fail);
}

TEST_CASE("settings fit: a label that fits whole is not a finding, and a cut on 800x480 is only listed") {
    CHECK_FALSE(text_was_cut("Replays kept per song", "Replays kept per song"));
    CHECK(cut_verdict(800.0f, 480.0f) == FitVerdict::Listed);
    CHECK(cut_verdict(1366.0f, 700.0f) == FitVerdict::Listed); // a short window is not 1366x768
}

TEST_CASE("settings fit: a page that scrolls fails at 1366x768 and up, is listed below, and a page that fits passes") {
    CHECK(scroll_verdict(120.0f, 1920.0f, 1080.0f) == FitVerdict::Fail);
    CHECK(scroll_verdict(12.0f, 1366.0f, 768.0f) == FitVerdict::Fail);
    CHECK(scroll_verdict(120.0f, 800.0f, 480.0f) == FitVerdict::Listed);
    CHECK(scroll_verdict(0.0f, 1366.0f, 768.0f) == FitVerdict::Ok);
    CHECK(scroll_verdict(0.3f, 1366.0f, 768.0f) == FitVerdict::Ok); // a rounding is not a scroll
    CHECK(std::string(verdict_word(FitVerdict::Fail)) == "FAIL");
}

TEST_CASE("settings fit: a known scrolling page is reported not failed, its entry goes stale once it "
          "fits, and every other page stays strict") {
    // No page is listed now: every one fits (View, Practice with the hands rows and Notes included).
    CHECK(kKnownScrolls.empty());
    CHECK(known_scroll_issue("page-view").empty());
    CHECK(known_scroll_issue("page-view-learn-colours").empty());
    // Any other page has no entry, so its scroll is a failure.
    CHECK(known_scroll_issue("page-practice-hands").empty());
    CHECK(known_scroll_issue("page-practice").empty());
    CHECK(known_scroll_issue("page-input").empty());
    CHECK(known_scroll_issue("page-notes").empty());
    // Notes is strict: a scroll of it at 1366x768 or 1920x1080 is a failure (the audit measures the
    // scroll with ImGui, `page-scroll`; this is the verdict it gives), and fitting passes.
    for (const float w : {1366.0f, 1920.0f}) {
        const float h = w == 1366.0f ? 768.0f : 1080.0f;
        CHECK(scroll_verdict(110.0f, w, h, known_scroll_issue("page-notes")) == FitVerdict::Fail);
        CHECK(scroll_verdict(0.0f, w, h, known_scroll_issue("page-notes")) == FitVerdict::Ok);
    }
    CHECK(scroll_verdict(120.0f, 1366.0f, 768.0f, known_scroll_issue("page-practice")) ==
          FitVerdict::Fail);
    CHECK(scroll_verdict(120.0f, 1920.0f, 1080.0f, "#000") == FitVerdict::Known); // a listed page, were there one
    CHECK(std::string(verdict_word(FitVerdict::Known)) == "known");
    // A listed page that now fits at 1366x768 is a failure to remove its entry; below that size it
    // is not looked at.
    CHECK(scroll_verdict(0.0f, 1366.0f, 768.0f, "#471") == FitVerdict::Stale);
    CHECK(scroll_verdict(0.0f, 800.0f, 480.0f, "#471") == FitVerdict::Ok);
    CHECK(std::string(verdict_word(FitVerdict::Stale)) == "stale");
}
