// SPDX-License-Identifier: GPL-3.0-or-later
// What the command line asks of the window mode, and the few things the options imply: the look
// they override, the settings folder, the renderer.
#pragma once

#include "audio_out.hpp"
#include "dev_script.hpp"
#include "song_sound.hpp"
#include "ui.hpp"
#include "ui/icon_audit.hpp"

#include "etude/look.hpp"
#include "etude/note_style.hpp"
#include "etude/panel_size.hpp"
#include "etude/renderer_path.hpp"
#include "etude/results_card.hpp"
#include "etude/session_state.hpp"
#include "etude/song_schedule.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>

namespace etude::app {

// When main() began, for --startup-log.
extern std::chrono::steady_clock::time_point g_process_start;

// Window size from --window; zero means the default 1280x720 with the display's full pixel density.
struct WindowSize {
    int width = 0;
    int height = 0;
};

// What the command line asks of the window mode.
struct WindowOptions {
    bool latency_log = false;
    // --headless: the window is created hidden and every frame is drawn into an offscreen target
    // of the window's pixel size, read back for --shot and --icon-audit and never presented: the
    // same renderer and pixels as a visible window, with nothing on screen.
    bool headless = false;
    WindowSize size;
    // --panel-mm=WxH: the display's physical size in millimetres (a panel that reports none, or a
    // headless run standing for one); else the system's report, else 96 dpi.
    std::optional<etude::PanelMm> panel_mm;
    // Startup look overrides, applied on top of the saved look (not saved by themselves).
    const char* names = nullptr;                   // off, letters or solfege
    const char* octaves = nullptr;                 // scientific or yamaha
    const char* style = nullptr;                   // a note style name
    std::optional<int> glow;                       // --glow 0..100: the block glow
    std::optional<etude::BeatLines> beat_lines;  // --beat-lines off|beats|half|bars
    std::optional<etude::BlockLayout> blocks;    // --blocks key-width|even
    const char* colors = nullptr;                  // a colour scheme name, see parse_colors
    bool high_contrast = false;                    // --high-contrast: High contrast on for this run
    std::optional<etude::KeySize> key_size;      // --key-size
    std::optional<etude::ViewMode> view;         // --view falling|sheet|both
    std::optional<etude::RendererPath> renderer; // --renderer basic|gpu|gl
    bool show_typing_keys = false;                 // --show-typing-keys
    bool startup_log = false;   // --startup-log: print the time to the first frame
    bool debug_effects = false; // the effects level in the status line
    std::string settings_dir;   // empty: SDL's per-user folder
    // --crash-screen: only the screen that says etude closed unexpectedly, with Restart and
    // Restart and send report (or, when the update's data was not put back, the reason with Try again
    // and Send report). Opens no history, audio, MIDI or song. Started by systemd after a crash
    // (deploy/pi/etude-crash-screen.service); exits 0 when the player restarts the app.
    bool crash_screen = false;
    // Developer: --crash-screen-reason TEXT shows the screen as if the update's data could not be put
    // back for that reason ("demo" is a made-up long one), for the shots and the audit; nothing is
    // written either way when a demo is on.
    std::string crash_screen_reason;
    // Developer: --crash-screen-press NAME taps that button of the crash screen a moment after it opens
    // (restart, restart-send, try-again, send-report), for a test with no one at the screen. It leaves
    // the same notes as a tap would.
    std::string crash_screen_press;
    // --hit-effect: what a key press shows at the hit line (this run)
    std::optional<etude::HitEffect> hit_effect;
    std::optional<etude::app::PanelTab> panel;
    etude::app::DevScript script;
    etude::app::IconAudit audit; // --icon-audit: where each icon sits in its button
    etude::app::AudioOptions audio;
    // Microphone (this run): --input, --mic-wav, --mic-log.
    std::optional<etude::InputMode> input_mode;
    std::string mic_wav;
    bool mic_log = false;
    bool frame_log = false;    // --frame-log: print every frame over kFrameStallNs, split by phase
    bool count_allocs = false; // --allocs: count the main thread's heap allocations per frame
    // Songs (this run): --song, --mode, --hands, --song-sound, --play, --robot.
    std::string song_path;
    std::optional<etude::PracticeMode> mode;
    std::optional<etude::HandFilter> hands;
    std::optional<etude::app::SongSound> song_sound;
    bool play = false;                // start playing at once
    std::optional<bool> metronome;    // --metronome on|off (this run)
    std::optional<int> count_in_bars; // --count-in 0|1|2 (this run)
    std::optional<int> speed_percent; // --speed 25..200 (this run)
    double seek_s = -1.0;             // start the song here (seconds)
    bool robot = false;               // developer: play the practised notes, for checking the modes
    // Developer: show a results card for an invented run once the song is open, for the audit and
    // for screenshots; `--results-demo=1,3` leaves the chips 1 and 3 (from 0) out.
    bool results_demo = false;
    bool drill_demo = false; // with results_demo: over a Learn drill, with its "How it went" card
    bool melodies_demo = false; // the Learn Melodies page with invented best scores (some in progress, some passed)
    std::uint32_t results_demo_off = 0;
    // Developer: with --results-demo, only the first N of its six bars went wrong (the rest were
    // played clean), so the song's hardest bars are suggested in the room left.
    int results_demo_trouble = etude::kMaxCardBars;
    // Developer: show a trouble-bar run's check-in ("checkin") or summary ("summary") for an
    // invented run, once the song is open.
    std::string run_demo;
    // Developer: with --results-demo, the card opens on the order of its bars.
    bool order_demo = false;
    bool long_profile_name = false; // developer: the longest profile and config names
    bool configs_demo = false;  // developer: the three saved configs hold three different looks
    bool profiles_demo = false; // developer: three more profiles with a streak each, once the history loads
    // Developer: a results card for an invented run with its replay, once the song is open: the
    // "How it went" page open on it (--summary-demo), or the replay view open at this many seconds,
    // paused (--replay-demo[=S]; negative: off). --results-demo gets the invented replay too.
    bool summary_demo = false;
    bool summary_board = false; // ... on its Board tab (--summary-board, which implies --summary-demo)
    bool summary_runs = false;  // ... on its Runs tab (--summary-runs, which implies --summary-board)
    bool summary_short = false; // ... for a run of 5 notes, too few to score (--summary-short)
    bool summary_low = false;   // ... for a run under the player's best five (--summary-low)
    bool summary_lowest = false; // ... for the lowest run of the best five (--summary-lowest)
    double replay_demo_s = -1.0;
    // Developer: how far ahead of reaching the hit line a note of the song is handed over (0 hands
    // each over when the frame that passes it runs, as before notes were scheduled), and a random
    // sleep of 0..N ms after every frame, to see the song keep time while the frame time varies.
    std::int64_t song_lead_ns = etude::kSongSoundLeadNs;
    int dev_frame_jitter_ms = 0;
    // Developer: the combo the scene shows, whatever the song does (--combo-fixed N), for shots.
    int combo_fixed = -1;
    // Developer: --fake-clock-hz N moves the app's clock 1/N s per frame (a scripted headless run).
    int fake_clock_hz = 0;
    std::uint64_t ear_seed = 0; // developer: the ear card's questions come from this seed (0: random)
    // Developer: the loader thread waits this long before reading each song, as a slow card does,
    // to see the "Opening" state and a newer request replace an older one.
    int dev_open_delay_ms = 0;
    // Developer: "Get songs" reads the catalogues from this folder (DIR/host/path) instead of the
    // network, a song file arriving at this many bytes a second (0: at once), each file failing
    // once first when asked.
    std::string catalogue_sites;
    std::uint64_t catalogue_speed = 0;
    bool catalogue_fail_first = false;
    bool get_songs_check = false; // fetch each catalogue's listing once, print it, exit
    // Developer: --upload-serve runs the phone upload server with no window (the Songs folder of
    // the settings folder), prints its PIN and address and runs until it turns itself off or
    // Ctrl-C; --upload-port N and --upload-bind ADDR pick where. --watch-log DIR prints what the
    // folder watcher reports for DIR until Ctrl-C.
    bool upload_serve = false;
    int upload_port = 8080;
    std::string upload_bind;            // empty: the private LAN addresses (loopback when none)
    int upload_deadline_s = 180;        // --upload-deadline-s N: the longest a connection may last
    bool upload_all_interfaces = false; // --upload-all-interfaces: 0.0.0.0, only when asked for
    // Developer: --upload-demo off|ready|receiving|done|nonetwork gives the Add songs page a fixed
    // view with no server behind it (for the shots and the audit).
    std::string upload_demo;
    std::string watch_log;
    // Developer: --about-demo STATE gives the About page a fixed view of the update check with no
    // network behind it (see about_demo.hpp); --about-link github|coffee|release starts it with
    // that link's QR sheet open (for the shots and the audit).
    std::string about_demo;
    // Developer: --icon-title N stands for the best title any profile holds (1 .. 8) in the About
    // page's icon colours, with no history behind it; the About demo alone gives 4. -1: none.
    int icon_title = -1;
    std::string about_link;
    // Developer: --crash-demo mac|pi shows the notice after a crash with a made-up report (a browser
    // on the Mac, none on the Pi), and with --crash-screen the report that screen sends; --crash-test
    // segv|bus|ill|fpe|abort|throw crashes the app on purpose once it has started.
    std::string crash_demo;
    std::string crash_test;
    // Developer: --fake-volume DIR makes DIR the only USB stick (listed while the folder exists);
    // --fake-share running|none gives the network folder tile that answer; --sources-open
    // network|usb starts the Add songs page with that view open (for the shots).
    std::string fake_volume;
    std::string fake_share;
    std::string sources_open;
    std::string library_query;    // --library-query: the library's search starts with this
};

// Applies the look options of this run on top of the saved look.
void apply_look_options(const WindowOptions& o, etude::LookSettings& look);

// Where the settings file lives: the --settings-dir option, else SDL's per-user folder.
std::filesystem::path settings_directory(const std::string& option);

// The renderer for this run: --renderer, else the one the settings file asks for.
etude::RendererPath renderer_for(const WindowOptions& opt);

// Seconds east of UTC here, at t (for the day a run belongs to).
std::int32_t local_utc_offset_s(std::time_t t);

} // namespace etude::app
