// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli.hpp"

#include "bench.hpp"
#include "cli_upload.hpp"
#include "library_bench.hpp"
#include "mac_update.hpp"
#include "settings_check.hpp"
#include "sheet/sheet_dump.hpp"
#include "spike_bench.hpp"
#include "synth_test.hpp"
#include "ui/exact_shot.hpp"
#include "crash_screen_run.hpp"
#include "window_run.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace etude::app {
namespace {

// etude --png-diff A.png B.png [TOLERANCE]: the largest difference of any channel between two
// PNGs (0..255) and how many pixels differ, no window. Exit 0 when it is within TOLERANCE
// (default 1 level), 1 when not, 2 when a file cannot be read or the sizes differ. The check that
// a headless --shot is the pixels of a visible one.
int png_diff(const char* a_path, const char* b_path, int tolerance) {
    SDL_Surface* a_raw = SDL_LoadPNG(a_path);
    SDL_Surface* b_raw = SDL_LoadPNG(b_path);
    if (a_raw == nullptr || b_raw == nullptr) {
        std::fprintf(stderr, "png-diff: cannot read %s: %s\n", a_raw == nullptr ? a_path : b_path,
                     SDL_GetError());
        SDL_DestroySurface(a_raw);
        SDL_DestroySurface(b_raw);
        return 2;
    }
    SDL_Surface* a = SDL_ConvertSurface(a_raw, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* b = SDL_ConvertSurface(b_raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(a_raw);
    SDL_DestroySurface(b_raw);
    if (a == nullptr || b == nullptr || a->w != b->w || a->h != b->h) {
        std::fprintf(stderr, "png-diff: sizes differ (%dx%d and %dx%d)\n", a ? a->w : 0,
                     a ? a->h : 0, b ? b->w : 0, b ? b->h : 0);
        SDL_DestroySurface(a);
        SDL_DestroySurface(b);
        return 2;
    }
    int most = 0;
    long differing = 0;
    for (int y = 0; y < a->h; ++y) {
        const auto* pa =
            static_cast<const std::uint8_t*>(a->pixels) + static_cast<std::ptrdiff_t>(y) * a->pitch;
        const auto* pb =
            static_cast<const std::uint8_t*>(b->pixels) + static_cast<std::ptrdiff_t>(y) * b->pitch;
        for (int x = 0; x < a->w; ++x) {
            int px = 0;
            for (int c = 0; c < 4; ++c) {
                px = std::max(px, std::abs(static_cast<int>(pa[x * 4 + c]) -
                                           static_cast<int>(pb[x * 4 + c])));
            }
            most = std::max(most, px);
            differing += px > 0 ? 1 : 0;
        }
    }
    std::printf(
        "png-diff: %dx%d, largest channel difference %d, %ld pixels differ (tolerance %d)\n", a->w,
        a->h, most, differing, tolerance);
    SDL_DestroySurface(a);
    SDL_DestroySurface(b);
    return most <= tolerance ? 0 : 1;
}

// etude --sheet-dump FILE [options]: the options after the file name.
int sheet_dump(int argc, char* argv[]) {
    etude::app::SheetDumpOptions o;
    o.song = argv[2];
    for (int i = 3; i < argc; ++i) {
        const bool more = i + 1 < argc;
        if (std::strcmp(argv[i], "--out") == 0 && more) {
            o.out_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && more) {
            o.width_px = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--space") == 0 && more) {
            o.staff_space_px = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--abc-chords") == 0) {
            o.abc_chords = true;
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            o.verbose = true;
        } else if (std::strcmp(argv[i], "--window") == 0 && more) {
            std::sscanf(argv[++i], "%dx%d", &o.window_w, &o.window_h);
        } else if (std::strcmp(argv[i], "--view") == 0 && more) {
            o.both = std::strcmp(argv[++i], "both") == 0;
        } else if (std::strcmp(argv[i], "--names") == 0 && more) {
            o.names = argv[++i];
        } else if (std::strcmp(argv[i], "--chord-symbols") == 0) {
            o.chord_symbols = true;
        } else if (std::strcmp(argv[i], "--spacing") == 0 && more) {
            std::sscanf(argv[++i], "%f,%f", &o.spacing_linear, &o.spacing_non_linear);
        }
    }
    return run_sheet_dump(o);
}

// The bench starts from the defaults, not the saved look, so runs compare.
int run_bench_mode(CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    BenchOptions& bench_options = cl.bench_options;
    etude::LookSettings look;
    look.background = etude::BackgroundStyle::Motion; // every effect on: the worst case
    apply_look_options(opt, look);
    if (opt.style != nullptr) {
        bench_options.styles.clear(); // --style narrows --bench-all to that one style
    }
    if (opt.size.width > 0) {
        bench_options.width = opt.size.width;
        bench_options.height = opt.size.height;
        bench_options.exact_window = true;
    }
    bench_options.panel = opt.panel;
    etude::app::ui::exact_shot() =
        !bench_options.screenshot.empty() || !bench_options.combo_demo.empty();
    return etude::app::run_bench(bench_options, look);
}

int run_window_mode(CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    if (cl.size_given) {
        // --size is the bench's pixel size; a headless window draws that many pixels too. A
        // visible window is sized by --window, so --size there would be silently ignored.
        if (!opt.headless) {
            std::fprintf(stderr, "--size needs --headless (or --bench / --screenshot); "
                                 "a visible window is sized with --window WxH\n");
            return 2;
        }
        if (opt.size.width > 0 && (opt.size.width != cl.bench_options.width ||
                                   opt.size.height != cl.bench_options.height)) {
            std::fprintf(stderr, "--size %dx%d and --window %dx%d disagree; give one\n",
                         cl.bench_options.width, cl.bench_options.height, opt.size.width,
                         opt.size.height);
            return 2;
        }
        opt.size.width = cl.bench_options.width;
        opt.size.height = cl.bench_options.height;
    }
    return opt.crash_screen ? run_crash_screen(opt) : run_window(opt);
}

} // namespace

std::optional<int> run_tool(int argc, char* argv[]) {
    // The Mac update's atomic swap of two bundle folders, before anything else starts.
    if (argc == 4 && std::strcmp(argv[1], "--swap-bundle") == 0) {
        return run_swap_bundle(argv[2], argv[3]);
    }
    if (argc >= 4 && std::strcmp(argv[1], "--png-diff") == 0) {
        return png_diff(argv[2], argv[3], argc >= 5 ? std::atoi(argv[4]) : 1);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--sheet-dump") == 0) {
        return sheet_dump(argc, argv);
    }
    return std::nullopt;
}

int run_command(CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    BenchOptions& bench_options = cl.bench_options;
    if (cl.check_settings) {
        return run_settings_check(opt.size.width > 0 ? opt.size.width : 1366,
                                  opt.size.height > 0 ? opt.size.height : 768, opt.song_path);
    }
    if (cl.bench || !bench_options.screenshot.empty() || !bench_options.combo_demo.empty()) {
        return run_bench_mode(cl);
    }
    if (cl.timing) {
        cl.timing->direct = cl.timing_direct;
        cl.timing->song = opt.song_path;
        cl.timing->lead_ns = opt.song_lead_ns;
        cl.timing->frame_jitter_ms = opt.dev_frame_jitter_ms;
        cl.timing->seconds = cl.timing_seconds;
        cl.timing->audio = opt.audio;
        return run_song_timing(*cl.timing);
    }
    if (cl.synth_test) {
        return run_synth_test(opt.audio);
    }
    if (opt.get_songs_check) {
        return run_get_songs_check();
    }
    if (opt.upload_serve) {
        return run_upload_serve(opt);
    }
    if (!opt.watch_log.empty()) {
        return run_watch_log(opt.watch_log);
    }
    if (!cl.scan_bench.empty()) {
        return run_scan_bench(cl.scan_bench);
    }
    if (cl.spike_bench) {
        return run_spike_bench(static_cast<std::size_t>(cl.spike_notes));
    }
    if (cl.hand_memory_bench) {
        return run_hand_memory_bench(static_cast<std::size_t>(cl.spike_notes));
    }
    if (cl.search_bench > 0) {
        return run_search_bench(
            static_cast<std::size_t>(cl.search_songs > 0 ? cl.search_songs : cl.search_bench));
    }
    if (cl.midi_log) {
        return run_midi_log(opt, cl.log_look.octave_offset());
    }
    return run_window_mode(cl);
}

} // namespace etude::app
