// SPDX-License-Identifier: GPL-3.0-or-later
// `etude --bench-spikes [--spike-notes N]`: times what the frame thread does on the one frame a
// change lands on, over a made-up song of N notes (default 150,000): a change of hands in Flow and
// in Wait, a change of transposition with the update after it, the end of a run of 16,384 events,
// a section suggestion over 200 runs and the small per-load items. For each change it prints the
// frame thread's own time (the call and the updates after it until the change is in force) and how
// long the change took to come into force. No window. Returns the process exit code.
// run_hand_memory_bench() measures the heap of the hand split's steps the same way.
#pragma once

#include <cstddef>

namespace etude::app {

int run_spike_bench(std::size_t notes);

// `etude --bench-hand-memory [--spike-notes N]`: the heap a song whose hands the split decides
// costs, over a one-track MIDI file of N notes (default 150,000) with no hand names. For opening
// it (the loader, the tables a worker makes ahead, the controller taking it over), a "Split at"
// change, a second one, going back to the first, and "From the sheet": the highest the heap
// (operator new, every thread) reached above where the step started, and what it stayed above it.
// No window.
int run_hand_memory_bench(std::size_t notes);

} // namespace etude::app
