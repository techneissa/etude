// SPDX-License-Identifier: GPL-3.0-or-later
// The command line, second half: the options of the song, the developer and the scripted steps
// (cli_parse.cpp has the rest, and calls these).
#include "cli.hpp"
#include "crash_screen.hpp"

#include "etude/results_card.hpp"
#include "etude/session_state.hpp"
#include "ui.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace etude::app {

// The options of the song and how it is practised.
int take_song_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    if (std::strcmp(argv[i], "--song") == 0 && i + 1 < argc) {
        opt.song_path = argv[++i];
        if (opt.song_path == "none") {
            opt.song_path.clear(); // as if no --song was given: the audit's free-play scenarios
        }
    } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
        const char* m = argv[++i];
        if (std::strcmp(m, "flow") == 0) {
            opt.mode = etude::PracticeMode::Flow;
        } else if (std::strcmp(m, "wait") == 0) {
            opt.mode = etude::PracticeMode::Wait;
        } else {
            std::fprintf(stderr, "unknown mode: %s\n", m);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--hands") == 0 && i + 1 < argc) {
        const char* h = argv[++i];
        if (std::strcmp(h, "both") == 0) {
            opt.hands = etude::HandFilter::Both;
        } else if (std::strcmp(h, "left") == 0) {
            opt.hands = etude::HandFilter::Left;
        } else if (std::strcmp(h, "right") == 0) {
            opt.hands = etude::HandFilter::Right;
        } else {
            std::fprintf(stderr, "unknown hands: %s\n", h);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--song-sound") == 0 && i + 1 < argc) {
        const char* m = argv[++i];
        if (std::strcmp(m, "mute") == 0) {
            opt.song_sound = etude::app::SongSound::Mute;
        } else if (std::strcmp(m, "other-hand") == 0) {
            opt.song_sound = etude::app::SongSound::OtherHand;
        } else if (std::strcmp(m, "both-hands") == 0) {
            opt.song_sound = etude::app::SongSound::BothHands;
        } else {
            std::fprintf(stderr, "unknown song sound: %s\n", m);
            print_usage();
            return 2;
        }
    } else if (std::strcmp(argv[i], "--play") == 0) {
        opt.play = true;
    } else if (std::strcmp(argv[i], "--metronome") == 0 && i + 1 < argc) {
        const char* m = argv[++i];
        if (std::strcmp(m, "on") != 0 && std::strcmp(m, "off") != 0) {
            std::fprintf(stderr, "--metronome takes on or off, not %s\n", m);
            return 2;
        }
        opt.metronome = std::strcmp(m, "on") == 0;
    } else if (std::strcmp(argv[i], "--count-in") == 0 && i + 1 < argc) {
        opt.count_in_bars = std::atoi(argv[++i]);
        if (*opt.count_in_bars < 0 || *opt.count_in_bars > etude::kMaxCountInBars) {
            std::fprintf(stderr, "--count-in takes 0, 1 or 2 bars, not %s\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
        opt.speed_percent = std::atoi(argv[++i]);
        if (*opt.speed_percent < etude::kMinSpeedPercent ||
            *opt.speed_percent > etude::kMaxSpeedPercent) {
            std::fprintf(stderr, "--speed takes %d to %d, not %s\n", etude::kMinSpeedPercent,
                         etude::kMaxSpeedPercent, argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--seek") == 0 && i + 1 < argc) {
        opt.seek_s = std::atof(argv[++i]);
        if (!(opt.seek_s >= 0.0 && opt.seek_s < 86400.0)) {
            std::fprintf(stderr, "bad seek time: %s\n", argv[i]);
            return 2;
        }
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// --crash-demo and --crash-test: the notice after a crash with an invented report, and a crash on
// purpose.
int take_crash_option(int& i, int argc, char* argv[], WindowOptions& opt) {
    if (std::strcmp(argv[i], "--crash-screen-reason") == 0 && i + 1 < argc) {
        opt.crash_screen_reason = argv[++i];
    } else if (std::strcmp(argv[i], "--crash-screen-press") == 0 && i + 1 < argc) {
        opt.crash_screen_press = argv[++i];
        if (!crash_button_named(opt.crash_screen_press)) {
            std::fprintf(stderr, "--crash-screen-press takes restart, restart-send, try-again or send-report, not %s\n",
                         argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--crash-demo") == 0 && i + 1 < argc) {
        opt.crash_demo = argv[++i];
    } else if (std::strcmp(argv[i], "--crash-test") == 0 && i + 1 < argc) {
        opt.crash_test = argv[++i];
    } else {
        return kNotTaken;
    }
    return kTaken;
}

// --summary-demo and --replay-demo[=S]: the run's review and replay for an invented run;
// --melodies-demo: the Melodies page with invented scores; --configs-demo: the Configs page's three
// saved configs filled.
int take_replay_demo_option(int& i, int argc, char* argv[], WindowOptions& opt) {
    if (std::strcmp(argv[i], "--summary-demo") == 0) {
        opt.summary_demo = true;
    } else if (std::strcmp(argv[i], "--long-profile-name") == 0) {
        opt.long_profile_name = true;
    } else if (std::strcmp(argv[i], "--configs-demo") == 0) {
        opt.configs_demo = true;
    } else if (std::strcmp(argv[i], "--melodies-demo") == 0) {
        opt.melodies_demo = true;
    } else if (std::strcmp(argv[i], "--summary-board") == 0) {
        opt.summary_demo = true;
        opt.summary_board = true;
    } else if (std::strcmp(argv[i], "--summary-runs") == 0) {
        opt.summary_demo = true;
        opt.summary_board = true;
        opt.summary_runs = true;
    } else if (std::strcmp(argv[i], "--summary-short") == 0) {
        opt.summary_demo = true;
        opt.summary_board = true;
        opt.summary_short = true;
    } else if (std::strcmp(argv[i], "--summary-low") == 0) {
        opt.summary_demo = true;
        opt.summary_board = true;
        opt.summary_low = true;
    } else if (std::strcmp(argv[i], "--summary-lowest") == 0) {
        opt.summary_demo = true;
        opt.summary_board = true;
        opt.summary_lowest = true;
    } else if (std::strncmp(argv[i], "--replay-demo", 13) == 0 &&
               (argv[i][13] == '\0' || argv[i][13] == '=')) {
        opt.replay_demo_s = argv[i][13] == '=' ? std::atof(argv[i] + 14) : 0.0;
        if (!(opt.replay_demo_s >= 0.0 && opt.replay_demo_s <= 36000.0)) {
            std::fprintf(stderr, "bad --replay-demo: %s (seconds, 0..36000)\n", argv[i]);
            return 2;
        }
    } else {
        return take_crash_option(i, argc, argv, opt);
    }
    return kTaken;
}

// The developer options.
int take_dev_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    if (std::strcmp(argv[i], "--robot") == 0) {
        opt.robot = true;
    } else if (std::strncmp(argv[i], "--results-demo", 14) == 0 &&
               (argv[i][14] == '\0' || argv[i][14] == '=')) {
        opt.results_demo = true;
        for (const char* p = argv[i][14] == '=' ? argv[i] + 15 : ""; *p != '\0';) {
            char* end = nullptr;
            const long bar = std::strtol(p, &end, 10);
            if (end == p || bar < 0 || bar >= etude::kMaxCardBars) {
                std::fprintf(stderr, "bad --results-demo list: %s (chips 0..%d)\n", argv[i],
                             etude::kMaxCardBars - 1);
                return 2;
            }
            opt.results_demo_off |= 1u << bar;
            p = *end == ',' ? end + 1 : end;
        }
    } else if (std::strcmp(argv[i], "--drill-demo") == 0) {
        opt.results_demo = opt.drill_demo = true; // the invented run, over a drill, not the song
    } else if (std::strcmp(argv[i], "--results-demo-trouble") == 0 && i + 1 < argc) {
        opt.results_demo_trouble = std::atoi(argv[++i]);
        if (opt.results_demo_trouble < 0 || opt.results_demo_trouble > etude::kMaxCardBars) {
            std::fprintf(stderr, "bad --results-demo-trouble: %s (0..%d)\n", argv[i],
                         etude::kMaxCardBars);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--ear-seed") == 0 && i + 1 < argc) {
        opt.ear_seed = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--fake-clock-hz") == 0 && i + 1 < argc) {
        opt.fake_clock_hz = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--combo-fixed") == 0 && i + 1 < argc) {
        opt.combo_fixed = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--order-demo") == 0) {
        opt.order_demo = true;
    } else if (std::strcmp(argv[i], "--profiles-demo") == 0) {
        opt.profiles_demo = true;
    } else if (std::strncmp(argv[i], "--run-demo=", 11) == 0) {
        opt.run_demo = argv[i] + 11;
        if (opt.run_demo != "checkin" && opt.run_demo != "summary") {
            std::fprintf(stderr, "bad --run-demo: %s (checkin or summary)\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--song-lead-ms") == 0 && i + 1 < argc) {
        const double lead_ms = std::atof(argv[++i]);
        if (!(lead_ms >= 0.0 && lead_ms <= 500.0)) {
            std::fprintf(stderr, "bad song lead: %s (0..500 ms)\n", argv[i]);
            return 2;
        }
        opt.song_lead_ns = static_cast<std::int64_t>(lead_ms * 1e6);
    } else if (std::strcmp(argv[i], "--dev-frame-jitter-ms") == 0 && i + 1 < argc) {
        opt.dev_frame_jitter_ms = std::atoi(argv[++i]);
        if (opt.dev_frame_jitter_ms < 0 || opt.dev_frame_jitter_ms > 200) {
            std::fprintf(stderr, "bad frame jitter: %s (0..200 ms)\n", argv[i]);
            return 2;
        }
    } else if (std::strcmp(argv[i], "--catalogue-sites") == 0 && i + 1 < argc) {
        opt.catalogue_sites = argv[++i];
    } else if (std::strcmp(argv[i], "--catalogue-speed") == 0 && i + 1 < argc) {
        const long long speed = std::atoll(argv[++i]);
        if (speed < 0) {
            std::fprintf(stderr, "bad catalogue speed: %s (bytes a second)\n", argv[i]);
            return 2;
        }
        opt.catalogue_speed = static_cast<std::uint64_t>(speed);
    } else if (std::strcmp(argv[i], "--catalogue-fail-first") == 0) {
        opt.catalogue_fail_first = true;
    } else if (std::strcmp(argv[i], "--library-query") == 0 && i + 1 < argc) {
        opt.library_query = argv[++i];
    } else if (std::strcmp(argv[i], "--dev-open-delay-ms") == 0 && i + 1 < argc) {
        opt.dev_open_delay_ms = std::atoi(argv[++i]);
        if (opt.dev_open_delay_ms < 0 || opt.dev_open_delay_ms > 5000) {
            std::fprintf(stderr, "bad open delay: %s (0..5000 ms)\n", argv[i]);
            return 2;
        }
    } else {
        return take_replay_demo_option(i, argc, argv, opt);
    }
    return kTaken;
}

// The options of the scripted steps and the icon audit.
int take_script_option(int& i, int argc, char* argv[], CommandLine& cl) {
    WindowOptions& opt = cl.opt;
    if ((std::strcmp(argv[i], "--tap") == 0 || std::strcmp(argv[i], "--drag") == 0 ||
         std::strcmp(argv[i], "--move") == 0 || std::strcmp(argv[i], "--type") == 0 ||
         std::strcmp(argv[i], "--shot") == 0 || std::strcmp(argv[i], "--shots") == 0 ||
         std::strcmp(argv[i], "--resize") == 0 || std::strcmp(argv[i], "--info") == 0 ||
         std::strcmp(argv[i], "--key") == 0 || std::strcmp(argv[i], "--press") == 0 ||
         std::strcmp(argv[i], "--touch") == 0 || std::strcmp(argv[i], "--fdrag") == 0 ||
         std::strcmp(argv[i], "--fhold") == 0 || std::strcmp(argv[i], "--fholddrag") == 0 ||
         std::strcmp(argv[i], "--rdrag") == 0 || std::strcmp(argv[i], "--pinch") == 0 ||
         std::strcmp(argv[i], "--device-check") == 0 ||
         std::strcmp(argv[i], "--simulate-device-lost") == 0 ||
         std::strcmp(argv[i], "--fake-keyboard") == 0 ||
         std::strcmp(argv[i], "--fake-unplug") == 0 || std::strcmp(argv[i], "--fake-note") == 0 ||
         std::strcmp(argv[i], "--quit-frame") == 0) &&
        i + 1 < argc) {
        if (!opt.script.parse(argv[i], argv[i + 1])) {
            std::fprintf(stderr, "bad value for %s: %s\n", argv[i], argv[i + 1]);
            print_usage();
            return 2;
        }
        ++i;
    } else if ((std::strcmp(argv[i], "--icon-audit") == 0 ||
                std::strcmp(argv[i], "--audit-label") == 0 ||
                std::strcmp(argv[i], "--audit-tap") == 0 ||
                std::strcmp(argv[i], "--audit-wheel") == 0 ||
                std::strcmp(argv[i], "--audit-png") == 0 ||
                std::strcmp(argv[i], "--ui-scale") == 0) &&
               i + 1 < argc) {
        if (!opt.audit.parse(argv[i], argv[i + 1])) {
            std::fprintf(stderr, "bad value for %s: %s\n", argv[i], argv[i + 1]);
            print_usage();
            return 2;
        }
        ++i;
    } else {
        return kNotTaken;
    }
    return kTaken;
}

} // namespace etude::app
