// SPDX-License-Identifier: GPL-3.0-or-later
// What the history store takes and gives for the Progress page (core progress_view.hpp).
#pragma once

#include "etude/medal_eval.hpp"
#include "etude/progress_view.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace etude::app {

struct ProgressRequest {
    std::int64_t profile_id = 0; // whose page; 0 is the active profile when the request is made
    std::int32_t today = 0;      // the local day key the page is for
    int goal_minutes = 0;        // the profile's daily goal (0: none)
    // What the medals need of each profile, taken from the frame's profiles when the request is made
    // (the history thread has the runs, not the settings): when it was made and its daily goal.
    struct Person {
        std::int64_t id = 0;
        std::int64_t created_utc_s = 0;
        int goal_minutes = 0;
    };
    std::vector<Person> people{};
    // Seconds east of UTC here (Anniversary counts local days), and which songs are versions of one piece
    // (the library's grouping, by content hash): both from the frame thread, like `people`.
    std::int32_t utc_offset_s = 0;
    std::shared_ptr<const PieceMap> pieces{};
};

struct ProgressAnswer {
    std::uint64_t serial = 0;
    // Built on the history thread from every profile's runs in the database. Null only when the
    // store closed or the database was unusable before it could answer.
    std::shared_ptr<const ProgressView> view;
};

} // namespace etude::app
