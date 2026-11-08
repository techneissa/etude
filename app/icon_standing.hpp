// SPDX-License-Identifier: GPL-3.0-or-later
// The best title and the most medals among the profiles a progress view holds, which the About page's
// icon colours and the medal beside them show. Taken from the household of the latest view, so a
// profile that is deleted drops out with the next answer of the history.
#pragma once

#include "etude/progress_view.hpp"

#include <algorithm>

namespace etude::app {

struct IconStanding {
    int title = 0;  // 1 Beginner .. 8 Virtuoso; 0: no view yet
    int points = 0; // the most medals any profile holds
};

inline IconStanding icon_standing(const etude::ProgressView* view) {
    IconStanding out;
    if (view == nullptr) {
        return out;
    }
    for (const etude::ProfileProgress& p : view->household) {
        out.title = std::max(out.title, p.medals.title());
        out.points = std::max(out.points, p.medals.points());
    }
    return out;
}

} // namespace etude::app
