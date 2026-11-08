// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/icon_standing.hpp"
#include "../app/ui/progress_ask.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;

namespace {

// A profile whose first `held` achievements are at tier 8: 8 * held medals.
ProfileProgress profile(std::int64_t id, int held) {
    ProfileProgress p;
    p.profile_id = id;
    for (int i = 0; i < held; ++i) {
        p.medals.tier[static_cast<std::size_t>(i)] = kTierCount;
    }
    return p;
}

} // namespace

TEST_CASE("icon standing: the best title and the most medals among the profiles") {
    ProgressView view;
    view.household = {profile(1, 3), profile(2, 12), profile(3, 6)};
    const IconStanding s = icon_standing(&view);
    CHECK(s.points == 96);
    CHECK(s.title == title_for_points(96));
    CHECK(s.title == 5);
}

TEST_CASE("icon standing: no view, or no profile in it, is not known") {
    CHECK(icon_standing(nullptr).title == 0);
    CHECK(icon_standing(nullptr).points == 0);
    const ProgressView empty;
    CHECK(icon_standing(&empty).title == 0);
    CHECK(icon_standing(&empty).points == 0);
}

TEST_CASE("icon standing: a deleted profile's medals leave with the next answer, which is asked for") {
    // The household has three people; the one with the most medals is deleted.
    ProgressView before;
    before.household = {profile(1, 3), profile(2, 12), profile(3, 6)};
    ProgressView after;
    after.household = {profile(1, 3), profile(3, 6)};
    CHECK(icon_standing(&before).points == 96);
    CHECK(icon_standing(&after).points == 48);
    CHECK(icon_standing(&after).title == title_for_points(48));
    CHECK(icon_standing(&after).title < icon_standing(&before).title);
    // Losing a person makes a new request due, so the page asks again while it is open.
    ui::ProgressAsked asked;
    asked.note(1, 30, 100, 3);
    CHECK_FALSE(asked.due(1, 30, 100, 3));
    CHECK(asked.due(1, 30, 100, 2));
    // And one that was closed asks again when it opens.
    asked.reopen();
    CHECK(asked.due(1, 30, 100, 2));
}
