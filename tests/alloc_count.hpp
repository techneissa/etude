// SPDX-License-Identifier: GPL-3.0-or-later
// Counts heap allocations on the calling thread while counting is on: the test binary replaces the
// global operator new (alloc_count.cpp) for code that must not allocate.
#pragma once

namespace etude::test {

void start_counting_allocations();
// Stops counting and returns the number of allocations since the start.
int stop_counting_allocations();
// The frees on the calling thread during the last counted stretch (stop_counting_allocations() sets it).
int frees_counted();

} // namespace etude::test
