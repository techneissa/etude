// SPDX-License-Identifier: GPL-3.0-or-later
// The crash handler: on SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT or an uncaught C++ exception it
// writes `<data folder>/crashes/<time>.txt` (the version, the OS, the screen, the song's title,
// the stack and the last log lines) and lets the process die the way it would have, so systemd
// still sees the crash and starts the crash screen. The signal's side calls only
// async-signal-safe functions: `open`, `write`, `time`, `backtrace_symbols_fd`, `raise`, over
// memory that exists since the start. No SDL.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace etude::app {

// What the report says of the build and the run. Fixed buffers the app keeps up to date; one line
// each, longer text cut.
void set_crash_build(std::string_view version, std::string_view os);
void set_crash_screen(std::string_view screen);
void set_crash_song(std::string_view title);
// "Linux 6.12.25 aarch64", "macOS (Darwin 27.0.0) arm64".
std::string os_description();

// A thread's own alternate stack for the handlers, so a stack overflow on that thread still gets a
// report (the stack is per thread; the handlers are the process's). The first thing every thread
// the app starts does: use `spawn_thread` (crash_thread.hpp). Threads of SDL and of the libraries
// are not the app's to start.
void crash_thread_init();

// The seconds after which a crash that cannot finish its report (a lock held by another thread)
// is ended by SIGALRM at its default action.
inline constexpr unsigned kCrashAlarmSeconds = 3;

// Makes `<data_dir>/crashes`, warms `backtrace` (its first call loads code, which a handler must
// not), sets the alternate stack and the handlers.
void install_crash_handler(const std::filesystem::path& data_dir);

// The report for `signo` into `dir`, a file named by the time. `detail` (may be null) is words
// for signal 0, an uncaught exception. Async-signal-safe; the handler's whole job, split out so a
// test makes one without a signal. False when the file could not be made.
bool write_crash_report(const char* dir, int signo, const char* detail);

// `--crash-test HOW` (segv, bus, ill, fpe, abort, throw): crashes on purpose; false for any other
// word.
bool crash_on_purpose(std::string_view how);

// "SIGSEGV" and the like.
const char* crash_signal_name(int signo);

} // namespace etude::app
