// SPDX-License-Identifier: GPL-3.0-or-later
// The command line: what it asked for besides the window run's own options (WindowOptions), the
// usage text, and the modes that run without a window.
#pragma once

#include "bench.hpp"
#include "song_timing_test.hpp"
#include "window_options.hpp"

#include "etude/look.hpp"

#include <optional>
#include <string>

namespace etude::app {

// What an option function returns when argv[i] is one of its options and its value is good.
inline constexpr int kTaken = -2;
// What it returns for an option that is not its own.
inline constexpr int kNotTaken = -1;

struct CommandLine {
    bool midi_log = false;
    bool synth_test = false;
    int search_bench = 0;
    int search_songs = 0;
    std::string scan_bench;         // --bench-scan DIR
    bool spike_bench = false;       // --bench-spikes
    bool hand_memory_bench = false; // --bench-hand-memory
    int spike_notes = 150000;       // --spike-notes N
    bool timing_direct = false;
    double timing_seconds = 20.0;
    std::optional<SongTimingOptions> timing;
    WindowOptions opt;
    bool bench = false;
    bool check_settings = false;
    BenchOptions bench_options;
    bool size_given = false;
    // --octaves also sets the numbering of the headless log.
    etude::LookSettings log_look;
};

// Reads argv into `cl`. Returns the exit code when the run is over already (a bad option, --help),
// else nothing.
std::optional<int> parse_command_line(int argc, char* argv[], CommandLine& cl);

// The option groups of cli_parse_dev.cpp: the song, the developer options and the scripted steps.
// Each returns kTaken when argv[i] is one of its options (i moves past the value), kNotTaken when
// it is another, else the exit code for a bad value.
int take_song_option(int& i, int argc, char* argv[], CommandLine& cl);
int take_dev_option(int& i, int argc, char* argv[], CommandLine& cl);
int take_script_option(int& i, int argc, char* argv[], CommandLine& cl);

void print_usage();

// The tools that take the whole command line as theirs, before any option is read: --png-diff and
// --sheet-dump. Returns their exit code, or nothing when argv names neither.
std::optional<int> run_tool(int argc, char* argv[]);

// Runs the mode the command line chose: a bench, a timing, the MIDI log, or the window.
int run_command(CommandLine& cl);

// etude --midi-log: prints decoded events and key transitions to stdout, no window.
int run_midi_log(const WindowOptions& opt, int octave_offset);

} // namespace etude::app
