// SPDX-License-Identifier: GPL-3.0-or-later
// What the icon audit reports about the settings sheet's fit, with no ImGui in it:
//
// - No cut text. At 1366x768 and up no label, row title or description in Settings is shortened with
//   an ellipsis ("Replays kept per …"): a label that does not fit wraps to a second line or its row
//   gives it more room. Below that size (800x480) a cut is listed but is not a failure.
// - No scrolling. At 1366x768 and up every settings page fits its body without scrolling. Below
//   that size a page may scroll; the audit lists the pages that do, at every size, so the page
//   issues know what is left.
// - A short list of known scrolling pages (`kKnownScrolls`), each with the issue that fits it. A
//   listed page is reported, not failed, so the audit stays green on main while the page issues are
//   open; every other page stays strict. The PR that fits a page removes its entry, and an entry for
//   a page that no longer scrolls at 1366x768 and up is itself a failure, so none outlives its fix.
#pragma once

#include <array>
#include <string_view>

namespace etude::app::ui {

// The window size (points, the window the audit opened) from which the settings sheet must show
// every page whole and uncut.
inline constexpr float kSettingsFitW = 1366.0f;
inline constexpr float kSettingsFitH = 768.0f;

constexpr bool settings_must_fit(float window_w, float window_h) {
    return window_w >= kSettingsFitW && window_h >= kSettingsFitH;
}

// A text the kit shortened to its room (`shown` is not `text`).
constexpr bool text_was_cut(std::string_view text, std::string_view shown) {
    return text != shown;
}

// A page scrolls when its content runs past its view by more than a rounding; `max_scroll` is how
// far ImGui can scroll it, in points.
constexpr bool page_scrolls(float max_scroll) { return max_scroll > 0.5f; }

enum class FitVerdict { Ok, Listed, Known, Stale, Fail };

// Settings pages known to scroll at 1366x768 and up, by audit scenario, with the issue that fits each.
struct KnownScroll {
    std::string_view scenario;
    std::string_view issue;
};
// Empty now that every page fits; a page that cannot yet gets an entry here with its issue.
inline constexpr std::array<KnownScroll, 0> kKnownScrolls{};

// The issue that fits `scenario`'s page, empty when it is not a known scrolling page.
constexpr std::string_view known_scroll_issue(std::string_view scenario) {
    for (const KnownScroll& k : kKnownScrolls) {
        if (k.scenario == scenario) {
            return k.issue;
        }
    }
    return {};
}

// A cut text is a failure where the sheet must fit, and only listed below it.
constexpr FitVerdict cut_verdict(float window_w, float window_h) {
    return settings_must_fit(window_w, window_h) ? FitVerdict::Fail : FitVerdict::Listed;
}

// A scrolling page is a failure where the sheet must fit, unless it is a known one (`known_issue`
// not empty: reported); a known page that does not scroll there is `Stale`. Below that size a scroll
// is only listed.
constexpr FitVerdict scroll_verdict(float max_scroll, float window_w, float window_h,
                                    std::string_view known_issue = {}) {
    const bool must_fit = settings_must_fit(window_w, window_h);
    if (!page_scrolls(max_scroll)) {
        return must_fit && !known_issue.empty() ? FitVerdict::Stale : FitVerdict::Ok;
    }
    if (!must_fit) {
        return FitVerdict::Listed;
    }
    return known_issue.empty() ? FitVerdict::Fail : FitVerdict::Known;
}

constexpr const char* verdict_word(FitVerdict v) {
    switch (v) {
    case FitVerdict::Fail:
        return "FAIL";
    case FitVerdict::Stale:
        return "stale";
    case FitVerdict::Known:
        return "known";
    case FitVerdict::Listed:
        return "listed";
    case FitVerdict::Ok:
        break;
    }
    return "ok";
}

} // namespace etude::app::ui
