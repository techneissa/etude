// SPDX-License-Identifier: GPL-3.0-or-later
#include "log_ring.hpp"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace etude::app {

namespace {

struct Ring {
    char lines[kLogRingLines][kLogRingLineChars];
    std::atomic<std::size_t> count{0}; // lines ever written; the newest is count - 1
};

Ring g_ring;
std::mutex g_mutex; // writers only: the crash handler never takes it
etude::ScrubRules g_rules;

void store(const std::string& text) {
    std::lock_guard lock(g_mutex);
    const std::size_t at = g_ring.count.load(std::memory_order_relaxed) % kLogRingLines;
    std::size_t n = std::min(text.size(), kLogRingLineChars - 1);
    std::memcpy(g_ring.lines[at], text.data(), n);
    g_ring.lines[at][n] = '\0';
    g_ring.count.store(g_ring.count.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}

} // namespace

void log_printf(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    std::fputs(text, stderr);
    log_ring_add(text);
}

void log_ring_add(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    etude::ScrubRules rules;
    {
        std::lock_guard lock(g_mutex);
        rules = g_rules;
    }
    store(etude::scrub_line(line, rules));
}

void set_log_scrub(etude::ScrubRules rules) {
    std::lock_guard lock(g_mutex);
    g_rules = std::move(rules);
}

void log_ring_dump(int fd) {
    const std::size_t count = g_ring.count.load(std::memory_order_acquire);
    const std::size_t first = count > kLogRingLines ? count - kLogRingLines : 0;
    for (std::size_t i = first; i < count; ++i) {
        const char* line = g_ring.lines[i % kLogRingLines];
        std::size_t n = 0;
        while (n < kLogRingLineChars && line[n] != '\0') {
            ++n;
        }
        // Best effort: a failed write ends the dump, and nothing here may allocate or lock.
        if (::write(fd, line, n) < 0 || ::write(fd, "\n", 1) < 0) {
            return;
        }
    }
}

std::vector<std::string> log_ring_lines() {
    std::lock_guard lock(g_mutex);
    std::vector<std::string> out;
    const std::size_t count = g_ring.count.load(std::memory_order_acquire);
    for (std::size_t i = count > kLogRingLines ? count - kLogRingLines : 0; i < count; ++i) {
        out.emplace_back(g_ring.lines[i % kLogRingLines]);
    }
    return out;
}

void log_ring_clear() {
    std::lock_guard lock(g_mutex);
    g_ring.count.store(0, std::memory_order_release);
}

} // namespace etude::app
