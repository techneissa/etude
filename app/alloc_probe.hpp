// SPDX-License-Identifier: GPL-3.0-or-later
// Counts the heap allocations (operator new, and Dear ImGui's allocator once installed) the
// calling thread makes while counting is on. The bench uses it to show the UI's steady-state frame
// allocates nothing of its own. An allocation is "ours" when the code that asked is in this
// executable or in the C++ runtime (std::string growing, say), and "system" when it comes from
// another library, such as the graphics driver. Not counting costs one thread-local flag test per
// allocation, and not tracking the heap one more flag test per allocation and free.
#pragma once

#include <cstdint>

namespace etude::app {

// Switches counting on or off for the calling thread.
void count_allocations(bool on);
// The allocations this thread made while counting was on; they only ever grow.
std::uint64_t allocations_counted();        // ours
std::uint64_t system_allocations_counted(); // from other libraries
// Heap size, for the memory bench: while tracking is on, every thread's operator new adds the
// block's size and operator delete takes it off. track_heap(true) starts from zero, so the live
// size is the net change since then (a block freed that was made before counts below zero). The
// peak is the highest the live size reached since the start or the last heap_reset_peak().
void track_heap(bool on);
std::int64_t heap_live_bytes();
std::int64_t heap_peak_bytes();
void heap_reset_peak();
// Routes Dear ImGui's allocations through the counter. Call before the ImGui context is created.
void count_imgui_allocations();

} // namespace etude::app
