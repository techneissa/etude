// SPDX-License-Identifier: GPL-3.0-or-later
// Waking the frame loop while it waits for an event instead of drawing (core IdleGate). The MIDI
// and microphone threads call wake_loop() for every event; it is safe from any thread, and costs
// one atomic add while the loop is drawing. The wait itself is in idle_loop.hpp; this file holds
// the hand-over between the two, with no SDL in it.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace etude::app {

// Who asked for the frame: counted, so a test that finds the loop awake when nothing moves can say
// which thread woke it (WakeCounts, wake_report).
enum class WakeSource : std::uint8_t {
    Other,
    SongOpen,
    History,
    Library,
    HandChords,
    HandSplit,
    PracticeTables,
    Catalogue,
    Mic,
    Midi,
    Ports,
    FolderWatch,
    Sheet,
    Upload,
    Sources,
    Update,
    Count,
};
const char* wake_source_name(WakeSource source) noexcept;

// Any thread except an audio capture callback: asks the loop to run a frame now. Calls the wake hook
// (one SDL_PushEvent) at most once per idle period: only the call that finds the loop armed does.
void wake_loop(WakeSource source = WakeSource::Other) noexcept;

// What wakes a waiting loop (an SDL event push), set once by the loop. Called from any thread.
void set_wake_hook(void (*hook)()) noexcept;

// How many wake requests there have been. The loop reads it before it drains the inputs; a request
// that arrives after that must not be slept through.
std::uint64_t wake_requests() noexcept;

// The frame thread, just before it blocks: false when a request came after `seen` (do not block).
// Until disarm_wait(), a wake request calls the hook.
bool arm_wait(std::uint64_t seen) noexcept;
void disarm_wait() noexcept;

// How many wake requests each source has made since the start.
using WakeCounts = std::array<std::uint64_t, static_cast<std::size_t>(WakeSource::Count)>;
WakeCounts wake_counts() noexcept;
// "upload 3, library 1": the sources that woke the loop between two readings, or "none".
std::string wake_report(const WakeCounts& before, const WakeCounts& after);

} // namespace etude::app
