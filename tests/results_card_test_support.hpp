// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the results_card_*test.cpp files (split from results_card_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/results_card.hpp"
#include "ui/results_card_layout.hpp"
#include "etude/look.hpp"
#include "etude/screen_layout.hpp"
#include <doctest/doctest.h>
#include <cmath>
#include <vector>

namespace results_card_test_support {

using namespace etude;

const HandsPresent kBoth{true, true};

inline RunRecord flow_run() {
    RunRecord r;
    r.mode = RunMode::Flow;
    r.hands = HandFilter::Both;
    r.counts = {.perfect = 30, .good = 8, .early = 2, .late = 3, .missed = 5, .played = 0};
    r.wrong = 4;
    r.longest_combo = 21;
    r.left = {.graded = 20, .hit = 18};
    r.right = {.graded = 28, .hit = 25};
    r.all = {.graded = 48, .hit = 43};
    r.measures = {{1, 6, 2, 0}, {2, 6, 3, 2}, {4, 8, 1, 0}};
    return r;
}

inline AttemptChange first_try() { return {}; }

} // namespace results_card_test_support
