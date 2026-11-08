// SPDX-License-Identifier: GPL-3.0-or-later
// The app's log lines: each goes to stderr as it always did and, with its paths scrubbed, into a
// fixed ring of the last `kLogRingLines`, which a crash report prints. The ring is plain memory
// (no allocation after the start), so the crash handler reads it with nothing but `write`. No SDL.
#pragma once

#include "etude/crash_report.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app {

inline constexpr std::size_t kLogRingLines = 200;
inline constexpr std::size_t kLogRingLineChars = 240;

// printf into stderr and the ring. A line ends with its own newline, as it did with fprintf.
void log_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));

// A line into the ring alone (the scrubbed text of `line`, no newline), not on stderr.
void log_ring_add(std::string_view line);

// What a line may say about paths and names before it enters the ring (set once at the start,
// again when the profiles change).
void set_log_scrub(etude::ScrubRules rules);

// Async-signal-safe: writes the ring's lines, oldest first, one per line, to `fd`.
void log_ring_dump(int fd);

// The ring's lines, oldest first (tests and the report view of the developer).
std::vector<std::string> log_ring_lines();
void log_ring_clear();

} // namespace etude::app
