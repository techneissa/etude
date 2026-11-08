// SPDX-License-Identifier: GPL-3.0-or-later
// Reads the command line into a CommandLine, option by option. Each group function below takes the
// options it knows; the first to take one wins.
#include "cli.hpp"

#include "cli_values.hpp"
#include "crash_screen.hpp"
#include "etude/note_style.hpp"
#include "etude/results_card.hpp"
#include "etude/session_state.hpp"
#include "etude/sheet_view.hpp"
#include "etude/version.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>

namespace etude::app {
namespace {

// The options that name one of the look's values for this run: --colors, --key-size, --hit-effect
// and the renderer. 0 when argv[i] was one of them and its value is good (i moves past the value),
// 2 when the value is not a name, kNotTaken when argv[i] is another option.
int take_look_name(int& i, int argc, char* argv[], WindowOptions& opt,
                   BenchOptions& bench_options) {
    const char* const option = argv[i];
    if (i + 1 >= argc) {
        return kNotTaken;
    }
    const char* const value = argv[i + 1];
    bool good = true;
    if (std::strcmp(option, "--colors") == 0) {
        etude::ColorScheme parsed{};
        good = parse_option(parse_colors, "colours", value, &parsed);
        opt.colors = value;
    } else if (std::strcmp(option, "--key-size") == 0) {
        etude::KeySize parsed{};
        good = parse_option(parse_key_size, "key size", value, &parsed);
        opt.key_size = parsed;
    } else if (std::strcmp(option, "--hit-effect") == 0) {
        etude::HitEffect parsed{};
        good = parse_option(parse_hit_effect, "hit effect", value, &parsed);
        opt.hit_effect = parsed;
    } else if (std::strcmp(option, "--renderer") == 0) {
        etude::RendererPath parsed{};
        good = parse_option(etude::parse_renderer_path, "renderer", value, &parsed);
        opt.renderer = parsed;
        bench_options.renderer = parsed;
    } else {
        return kNotTaken;
    }
    ++i;
    return good ? 0 : 2;
}

// The options of the run itself: the logs, the input, the audio, the settings folder, headless
// and help. Each group function returns kTaken when argv[i] is one of its options (i moves
// past the value), kNotTaken when it is another, else the exit code for a bad value.
int take_run_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    BenchOptions& bench_options = cl.bench_options;
    if (std::strcmp(argv[i], "--midi-log") == 0) {
        cl.midi_log = true;
    } else if (std::strcmp(argv[i], "--latency-log") == 0) {
        opt.latency_log = true;
    } else if (std::strcmp(argv[i], "--audio-latency-log") == 0) {
        opt.audio.latency_log = true;
    } else if (std::strcmp(argv[i], "--mic-log") == 0) {
        opt.mic_log = true;
    } else if (std::strcmp(argv[i], "--frame-log") == 0) {
        opt.frame_log = true;
    } else if (std::strcmp(argv[i], "--mic-wav") == 0 && i + 1 < argc) {
        opt.mic_wav = argv[++i];
    } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
        const char* m = argv[++i];
        if (std::strcmp(m, "midi") == 0) {
            opt.input_mode = etude::InputMode::Midi;
        } else if (std::strcmp(m, "mic") == 0) {
            opt.input_mode = etude::InputMode::Microphone;
        } else if (std::strcmp(m, "both") == 0) {
            opt.input_mode = etude::InputMode::Both;
        } else {
            std::fprintf(stderr, "unknown input: %s (midi, mic or both)\n", m);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--sound-font") == 0 && i + 1 < argc) {
        opt.audio.sound_font = argv[++i];
    } else if (std::strcmp(argv[i], "--settings-dir") == 0 && i + 1 < argc) {
        opt.settings_dir = argv[++i];
    } else if (is_crash_screen_option(argv[i])) { // also the old --safe-mode, which old units still pass
        opt.crash_screen = true;
    } else if (std::strcmp(argv[i], "--startup-log") == 0) {
        opt.startup_log = true;
    } else if (std::strcmp(argv[i], "--headless") == 0) {
        opt.headless = true;
        bench_options.headless = true;
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
        print_usage();
        return 0;
    } else if (std::strcmp(argv[i], "--version") == 0) {
        std::printf("%s\n", version_line().c_str());
        return 0;
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// The options of the modes that open no window, and the timings they take.
int take_mode_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    BenchOptions& bench_options = cl.bench_options;
    if (std::strcmp(argv[i], "--synth-test") == 0) {
        cl.synth_test = true;
    } else if (std::strcmp(argv[i], "--bench-search") == 0) {
        cl.search_bench = 5000;
    } else if (std::strcmp(argv[i], "--bench-spikes") == 0) {
        cl.spike_bench = true;
    } else if (std::strcmp(argv[i], "--bench-hand-memory") == 0) {
        cl.hand_memory_bench = true;
    } else if (std::strcmp(argv[i], "--spike-notes") == 0 && i + 1 < argc) {
        cl.spike_notes = std::atoi(argv[++i]);
        if (cl.spike_notes < 100 || cl.spike_notes > 200000) {
            std::fprintf(stderr, "bad note count: %s (100..200000)\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--bench-scan") == 0 && i + 1 < argc) {
        cl.scan_bench = argv[++i];
    } else if (std::strcmp(argv[i], "--songs") == 0 && i + 1 < argc) {
        cl.search_songs = std::atoi(argv[++i]);
        if (cl.search_songs < 1 || cl.search_songs > 100000) {
            std::fprintf(stderr, "bad song count: %s (1..100000)\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--check-settings") == 0) {
        cl.check_settings = true;
    } else if (std::strcmp(argv[i], "--song-timing") == 0 && i + 1 < argc) {
        const char* what = argv[++i];
        if (std::strcmp(what, "synth") != 0 && std::strcmp(what, "midi") != 0) {
            std::fprintf(stderr, "--song-timing takes synth or midi, not %s\n", what);
            return 2;
        }
        cl.timing.emplace();
        cl.timing->midi = std::strcmp(what, "midi") == 0;
    } else if (std::strcmp(argv[i], "--timing-direct") == 0) {
        cl.timing_direct = true;
    } else if (std::strcmp(argv[i], "--timing-seconds") == 0 && i + 1 < argc) {
        cl.timing_seconds = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--bench") == 0) {
        cl.bench = true;
    } else if (std::strcmp(argv[i], "--bench-all") == 0) {
        cl.bench = true;
        bench_options.styles.assign(std::begin(etude::kAllNoteStyles),
                                    std::end(etude::kAllNoteStyles));
    } else if (std::strcmp(argv[i], "--get-songs-check") == 0) {
        opt.get_songs_check = true;
    } else if (std::strcmp(argv[i], "--upload-serve") == 0) {
        opt.upload_serve = true;
    } else if (std::strcmp(argv[i], "--upload-port") == 0 && i + 1 < argc) {
        opt.upload_port = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--upload-deadline-s") == 0 && i + 1 < argc) {
        opt.upload_deadline_s = std::max(1, std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--upload-all-interfaces") == 0) {
        opt.upload_all_interfaces = true;
    } else if (std::strcmp(argv[i], "--upload-bind") == 0 && i + 1 < argc) {
        opt.upload_bind = argv[++i];
    } else if (std::strcmp(argv[i], "--upload-demo") == 0 && i + 1 < argc) {
        opt.upload_demo = argv[++i];
    } else if (std::strcmp(argv[i], "--icon-title") == 0 && i + 1 < argc) {
        opt.icon_title = std::clamp(std::atoi(argv[++i]), 1, 8);
    } else if (std::strcmp(argv[i], "--about-demo") == 0 && i + 1 < argc) {
        opt.about_demo = argv[++i];
    } else if (std::strcmp(argv[i], "--about-link") == 0 && i + 1 < argc) {
        opt.about_link = argv[++i];
    } else if (std::strcmp(argv[i], "--fake-volume") == 0 && i + 1 < argc) {
        opt.fake_volume = argv[++i];
    } else if (std::strcmp(argv[i], "--fake-share") == 0 && i + 1 < argc) {
        opt.fake_share = argv[++i];
    } else if (std::strcmp(argv[i], "--sources-open") == 0 && i + 1 < argc) {
        opt.sources_open = argv[++i];
    } else if (std::strcmp(argv[i], "--watch-log") == 0 && i + 1 < argc) {
        opt.watch_log = argv[++i];
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// The look's options: style, glow, beat lines, blocks, colours, key size, hit effect, renderer.
int take_look_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    BenchOptions& bench_options = cl.bench_options;
    if (std::strcmp(argv[i], "--style") == 0 && i + 1 < argc) {
        opt.style = argv[++i];
        etude::NoteStyle parsed{};
        if (!etude::parse_note_style(opt.style, &parsed)) {
            std::fprintf(stderr, "unknown style: %s\n", opt.style);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--glow") == 0 && i + 1 < argc) {
        const int g = std::atoi(argv[++i]);
        if (g < etude::kMinBlockGlow || g > etude::kMaxBlockGlow) {
            std::fprintf(stderr, "bad glow: %s (0 to 100)\n", argv[i]);
            return 2;
        }
        opt.glow = g;
    } else if (std::strcmp(argv[i], "--beat-lines") == 0 && i + 1 < argc) {
        const char* mode = argv[++i];
        if (std::strcmp(mode, "off") == 0) {
            opt.beat_lines = etude::BeatLines::Off;
        } else if (std::strcmp(mode, "beats") == 0) {
            opt.beat_lines = etude::BeatLines::Beats;
        } else if (std::strcmp(mode, "half") == 0) {
            opt.beat_lines = etude::BeatLines::HalfBeats;
        } else if (std::strcmp(mode, "bars") == 0) {
            opt.beat_lines = etude::BeatLines::Bars;
        } else {
            std::fprintf(stderr, "unknown beat lines: %s\n", mode);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--blocks") == 0 && i + 1 < argc) {
        const char* blocks = argv[++i];
        if (std::strcmp(blocks, "key-width") == 0) {
            opt.blocks = etude::BlockLayout::KeyWidth;
        } else if (std::strcmp(blocks, "even") == 0) {
            opt.blocks = etude::BlockLayout::EvenLanes;
        } else {
            std::fprintf(stderr, "unknown block layout: %s\n", blocks);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--high-contrast") == 0) {
        opt.high_contrast = true;
    } else if (const int named = take_look_name(i, argc, argv, opt, bench_options);
               named != kNotTaken) {
        if (named != 0) {
            print_usage();
            return named;
        }
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// The options of the view: sheet or falling, typing keys, effects level, names, octaves, window.
int take_view_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    BenchOptions& bench_options = cl.bench_options;
    if (std::strcmp(argv[i], "--view") == 0 && i + 1 < argc) {
        const std::optional<etude::ViewMode> parsed = etude::parse_view_mode(argv[++i]);
        if (!parsed) {
            std::fprintf(stderr, "unknown view: %s (falling, sheet or both)\n", argv[i]);
            print_usage();
            return 2;
        }
        opt.view = parsed;
        bench_options.view = *parsed;
    } else if (std::strcmp(argv[i], "--show-typing-keys") == 0) {
        opt.show_typing_keys = true;
    } else if (std::strcmp(argv[i], "--debug-effects") == 0) {
        opt.debug_effects = true;
    } else if (std::strcmp(argv[i], "--names") == 0 && i + 1 < argc) {
        opt.names = argv[++i];
        if (std::strcmp(opt.names, "off") != 0 && std::strcmp(opt.names, "letters") != 0 &&
            std::strcmp(opt.names, "solfege") != 0) {
            std::fprintf(stderr, "unknown names mode: %s\n", opt.names);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--octaves") == 0 && i + 1 < argc) {
        opt.octaves = argv[++i];
        if (std::strcmp(opt.octaves, "scientific") != 0 &&
            std::strcmp(opt.octaves, "yamaha") != 0) {
            std::fprintf(stderr, "unknown octave numbering: %s\n", opt.octaves);
            print_usage();
            return 2;
        }
    } else if (std::strncmp(argv[i], "--panel-mm", 10) == 0 &&
               (argv[i][10] == '=' || (argv[i][10] == '\0' && i + 1 < argc))) {
        const char* value = argv[i][10] == '=' ? argv[i] + 11 : argv[++i];
        opt.panel_mm = etude::parse_panel_mm(value);
        if (!opt.panel_mm) {
            std::fprintf(stderr, "bad --panel-mm: %s (want WIDTHxHEIGHT in millimetres, e.g. 344x194)\n",
                         value);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
        if (std::sscanf(argv[++i], "%dx%d", &opt.size.width, &opt.size.height) != 2 ||
            opt.size.width < 100 || opt.size.height < 100) {
            std::fprintf(stderr, "bad window size: %s\n", argv[i]);
            print_usage();
            return 2;
        }
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// The options of the bench: the scene it draws and the pixels.
int take_bench_option(int& i, int argc, char* argv[], CommandLine& cl) {
    BenchOptions& bench_options = cl.bench_options;
    if (std::strcmp(argv[i], "--song-bars") == 0) {
        bench_options.song_bars = true;
    } else if (std::strcmp(argv[i], "--combo") == 0) {
        bench_options.combo = true;
    } else if (std::strcmp(argv[i], "--combo-demo") == 0 && i + 1 < argc) {
        bench_options.combo_demo = argv[++i];
    } else if (std::strcmp(argv[i], "--combo-out") == 0 && i + 1 < argc) {
        bench_options.combo_out = argv[++i];
    } else if (std::strcmp(argv[i], "--bar") == 0 && i + 1 < argc) {
        bench_options.bar_song = argv[++i];
    } else if (std::strcmp(argv[i], "--notes") == 0 && i + 1 < argc) {
        bench_options.notes = std::atoi(argv[++i]);
        if (bench_options.notes < 1 || bench_options.notes > 2000) {
            std::fprintf(stderr, "bad note count: %s\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
        bench_options.seconds = std::atof(argv[++i]);
        if (!(bench_options.seconds > 0.0 && bench_options.seconds <= 600.0)) {
            std::fprintf(stderr, "bad seconds: %s\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
        cl.size_given = true;
        if (std::sscanf(argv[++i], "%dx%d", &bench_options.width, &bench_options.height) != 2 ||
            bench_options.width < 100 || bench_options.height < 100) {
            std::fprintf(stderr, "bad size: %s\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--no-degrade") == 0) {
        bench_options.degrade = false;
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// The options of the screenshot, the panel and what the bench shows.
int take_shot_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    BenchOptions& bench_options = cl.bench_options;
    if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
        bench_options.screenshot = argv[++i];
    } else if (std::strcmp(argv[i], "--shot-pattern") == 0 && i + 1 < argc) {
        bench_options.shot_pattern = argv[++i];
        const std::string& p = bench_options.shot_pattern;
        if (p != "run" && p != "chord" && p != "song-chromatic" && p != "song-chord" &&
            p != "song-run" && p != "all") {
            std::fprintf(stderr, "unknown shot pattern: %s\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--shot-after-ms") == 0 && i + 1 < argc) {
        bench_options.shot_after_ms = std::atoi(argv[++i]);
        if (bench_options.shot_after_ms < 0 || bench_options.shot_after_ms > 2000) {
            std::fprintf(stderr, "bad time: %s\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--at") == 0 && i + 1 < argc) {
        bench_options.at_seconds = std::atof(argv[++i]);
        if (!(bench_options.at_seconds > 0.5 && bench_options.at_seconds < 600.0)) {
            std::fprintf(stderr, "bad time: %s\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--allocs") == 0) {
        bench_options.count_allocs = true;
        opt.count_allocs = true;
    } else if (std::strcmp(argv[i], "--bar-pinned") == 0) {
        bench_options.bar_pinned = true;
    } else if (std::strcmp(argv[i], "--library-view") == 0 && i + 1 < argc) {
        const char* view = argv[++i];
        if (std::strcmp(view, "grid") == 0) {
            bench_options.library_view = etude::BrowseView::Grid;
        } else if (std::strcmp(view, "list") == 0) {
            bench_options.library_view = etude::BrowseView::List;
        } else if (std::strcmp(view, "shelves") == 0) {
            bench_options.library_view = etude::BrowseView::Shelves;
        } else {
            std::fprintf(stderr, "unknown library view: %s\n", view);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--library-group") == 0 && i + 1 < argc) {
        if (std::strcmp(argv[++i], "my_groups") != 0) {
            std::fprintf(stderr, "--library-group takes my_groups\n");
            print_usage();
            return 2;
        }
        bench_options.library_my_groups = true;
    } else if (std::strcmp(argv[i], "--bar-style") == 0 && i + 1 < argc) {
        const char* style = argv[++i];
        if (std::strcmp(style, "strip") == 0) {
            bench_options.bar_style = etude::BarStyle::Strip;
        } else if (std::strcmp(style, "groups") == 0) {
            bench_options.bar_style = etude::BarStyle::Groups;
        } else if (std::strcmp(style, "dock") == 0) {
            bench_options.bar_style = etude::BarStyle::Dock;
        } else {
            std::fprintf(stderr, "unknown bar style: %s\n", style);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--panel") == 0 && i + 1 < argc) {
        etude::app::PanelTab tab{};
        if (!etude::app::parse_panel_tab(argv[++i], &tab)) {
            std::fprintf(stderr, "unknown panel page: %s\n", argv[i]);
            print_usage();
            return 2;
        }
        opt.panel = tab;
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// argv[i] as one of the options: the first group that knows it takes it.
int take_option(int& i, int argc, char* argv[], CommandLine& cl) {
    using Take = int (*)(int&, int, char*[], CommandLine&);
    for (const Take take :
         {take_run_option, take_mode_option, take_look_option, take_view_option, take_bench_option,
          take_shot_option, take_song_option, take_dev_option, take_script_option}) {
        const int taken = take(i, argc, argv, cl);
        if (taken != kNotTaken) {
            return taken;
        }
    }
    return kNotTaken;
}

} // namespace

std::optional<int> parse_command_line(int argc, char* argv[], CommandLine& cl) {
    for (int i = 1; i < argc; ++i) {
        const int taken = take_option(i, argc, argv, cl);
        if (taken == kNotTaken) {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            print_usage();
            return 2;
        }
        if (taken != kTaken) {
            return taken;
        }
    }
    if (cl.opt.octaves != nullptr) {
        cl.log_look.octave_numbering = std::strcmp(cl.opt.octaves, "yamaha") == 0
                                           ? etude::OctaveNumbering::Yamaha
                                           : etude::OctaveNumbering::Scientific;
    }
    return std::nullopt;
}

} // namespace etude::app
