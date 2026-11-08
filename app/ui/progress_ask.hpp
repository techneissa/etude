// SPDX-License-Identifier: GPL-3.0-or-later
// When the Progress page asks the history for its numbers again, and what it shows while it has none:
// plain decisions, kept apart from the drawing so the tests can run them.
#pragma once

#include <cstddef>
#include <cstdint>

namespace etude::app::ui {

// What the last request was for. A new one is due when the page has just opened, the profile in use
// or its daily goal changed, the household gained or lost a person (a profile added or deleted:
// the Household view and the ranks move), or the local day turned over (the calendar, the streak and
// the week all move on at midnight).
struct ProgressAsked {
    bool fresh = true; // the page has just opened
    std::int64_t profile = -1;
    int goal = -1;
    std::int32_t day = -1;
    std::size_t people = 0;

    bool due(std::int64_t now_profile, int now_goal, std::int32_t now_day, std::size_t now_people) const {
        return fresh || profile != now_profile || goal != now_goal || day != now_day ||
               people != now_people;
    }
    void note(std::int64_t now_profile, int now_goal, std::int32_t now_day, std::size_t now_people) {
        fresh = false;
        profile = now_profile;
        goal = now_goal;
        day = now_day;
        people = now_people;
    }
    void reopen() { fresh = true; }
};

// What the page shows in place of its cards: Ready draws them; Loading says it is working; Failed
// says the history could not answer (never "working" for ever).
enum class ProgressStatus : std::uint8_t { Ready, Loading, Failed };

inline ProgressStatus progress_status(bool have_numbers_for_profile, bool request_failed) {
    if (have_numbers_for_profile) {
        return ProgressStatus::Ready;
    }
    return request_failed ? ProgressStatus::Failed : ProgressStatus::Loading;
}

} // namespace etude::app::ui
