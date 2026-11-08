// SPDX-License-Identifier: GPL-3.0-or-later
// What a window run holds while it runs, and its stages as member functions: startup in the
// constructors (window_startup.cpp), opening and restoring songs (window_songs.cpp), the events
// (window_events.cpp), the frame (window_frame.cpp, window_draw.cpp) and shutdown
// (window_shutdown.cpp).
//
// The members are declared in the order the objects are made and are destroyed in the reverse
// order: threads, audio and MIDI start in that order, and the history, the settings and the piano
// are closed in the reverse. WindowBase holds what exists before the first frame's objects (the
// scene, the sheet view and the UI) can be made, because those take the look as the command line
// and the saved session left it; WindowState adds them and everything the frame loop uses.
#pragma once

#include "alloc_probe.hpp"
#include "audio_out.hpp"
#include "builtin_piano_sink.hpp"
#include "catalogue_service.hpp"
#include "crash_session.hpp"
#include "device_check_host.hpp"
#include "display_panel.hpp"
#include "folder_watcher.hpp"
#include "frame_log.hpp"
#include "hand_chords_job.hpp"
#include "history_store.hpp"
#include "icon_service.hpp"
#include "idle_loop.hpp"
#include "keyboard_session.hpp"
#include "learn_sound.hpp"
#include "latency_log.hpp"
#include "library_binding.hpp"
#include "ui/progress_ask.hpp"
#include "live_redraw.hpp"
#include "mic_inbox.hpp"
#include "mic_source.hpp"
#include "midi_input.hpp"
#include "midi_output.hpp"
#include "open_song_box.hpp"
#include "persistence.hpp"
#include "profile_looks.hpp"
#include "replay_session.hpp"
#include "resume_keeper.hpp"
#include "robot.hpp"
#include "scene.hpp"
#include "sections_controller.hpp"
#include "sheet/sheet_view.hpp"
#include "song_board_cache.hpp"
#include "song_controller.hpp"
#include "song_lengths.hpp"
#include "song_library.hpp"
#include "song_opener.hpp"
#include "song_practice_sync.hpp"
#include "song_transport.hpp"
#include "sources_controller.hpp"
#include "timed_sender.hpp"
#include "ui.hpp"
#include "ui_model.hpp"
#include "virtual_input.hpp"
#include "window_options.hpp"

#include "etude/count_band.hpp"
#include "etude/drill.hpp"
#include "etude/effects_governor.hpp"
#include "etude/keyboard_view.hpp"
#include "etude/look.hpp"
#include "etude/note_trail.hpp"
#include "etude/note_window.hpp"
#include "etude/renderer_recovery.hpp"
#include "etude/replay_marks.hpp"
#include "etude/results_card.hpp"
#include "etude/scale_overlay.hpp"
#include "etude/session_restore.hpp"
#include "etude/sound_sink.hpp"
#include "etude/toast.hpp"
#include "etude/trouble_run.hpp"
#include "etude/ui_grow_hold.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app {

// The window and renderer a run draws into, and the frame period it paces by.
struct WindowHandles {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    etude::RendererPath path = etude::RendererPath::Basic; // the one the renderer is
    bool vsync = false;
    std::int64_t frame_ns = 0;
};

// State shared with the SDL event watch that redraws during a live window resize.
struct RedrawHook {
    std::function<void()> draw;
    SDL_WindowID window = 0;
    std::int64_t frame_ns = 16'666'667;
    etude::app::LiveRedrawState state; // what the main loop is doing (live_redraw.hpp)
    bool redrawing = false;              // inside the watch's own redraw
};

// Called by SDL for every event; redraws during a live resize (window_events.cpp).
bool SDLCALL on_window_event_watch(void* userdata, SDL_Event* event);

// What the last run's results card was about, to tell when it no longer is.
struct CardWatch {
    std::uint32_t load_serial = 0;
    etude::PracticeMode mode = etude::PracticeMode::FreePlay;
    etude::HandFilter hands = etude::HandFilter::Both;
    int speed_percent = 100;
    bool loop = false;
    double loop_start_s = 0.0;
    double loop_end_s = 0.0;
    double position_s = 0.0;
    bool operator==(const CardWatch&) const = default;
};

// A song asked for while the app runs, and when.
struct PendingOpen {
    etude::app::SongPath song;
    std::int64_t since_ns = 0;
    // The section the library's chip chose: the song opens looping it (once, not kept).
    std::optional<etude::app::LibraryBinding::SectionOpen> loop;
};

// What the part of the keyboard on screen was worked out from.
struct ViewInputs {
    int width = -1;
    etude::KeySize size = etude::KeySize::FitKeyboard;
    int keyboard_lowest = -1;
    int keyboard_highest = -1;
    float px_per_mm = 0.0f;
    bool operator==(const ViewInputs&) const = default;
};

// The values one frame's steps hand on to each other.
struct FrameCtx {
    std::int64_t now = 0;
    bool live = false;
    etude::LookSettings shown_look;
    etude::app::UiFrameInfo info;
    int out_w = 0;
    int out_h = 0;
    etude::app::SheetOffer sheet_offer;
    etude::ViewMode wanted_view = etude::ViewMode::Falling;
    bool sheet_refused = false;
    etude::SheetPlan sheet_plan;
    bool keyboard_prompt = false;
};

// How the history opens: on the profile the settings file names.
HistoryOptions history_options_for(const std::filesystem::path& data_dir);

// The folders the library lists: the per-user Songs folder, and the repository's test songs in a
// developer build.
std::vector<std::filesystem::path> song_roots_for(const std::filesystem::path& data_dir);

// "Get songs": where the catalogues are read from (the network, or a folder with --catalogue-sites) and cached.
etude::app::CatalogueService::Options
catalogue_options_for(const WindowOptions& opt, const std::filesystem::path& data_dir);

// The Songs folder's watcher: files that arrive from outside the app (the network folder, the phone
// upload page, a copy in Finder) show up in the library, and its worker wakes the frame loop.
etude::app::FolderWatcher::Options songs_watch_options(const std::filesystem::path& data_dir);

// The port the Add songs page reads: the real phone upload server (it starts when the page's switch
// goes on, listening on the private LAN addresses, and never lets the open song be renamed, moved
// or deleted), or with --upload-demo a fixed view.
std::unique_ptr<etude::app::ui::UploadPort>
make_upload_port(const WindowOptions& opt, const std::filesystem::path& data_dir,
                 etude::app::OpenSongBox& open_song, etude::app::SongLengthBook& lengths);

// The Add songs page's network folder and USB stick.
std::unique_ptr<etude::app::SourcesController>
make_sources(const WindowOptions& opt, const std::filesystem::path& data_dir);

// A developer run (a script, the audit, a headless window): nothing it does may
// outlive it, so it opens no device check and changes no icon.
bool developer_run(const WindowOptions& opt);
// The best title the icon colours are given with no history: --icon-title, else 4 in the About demo
// (some colours locked, some not), else -1 (the real one, from the profiles).
int icon_title_for(const WindowOptions& opt);

// The port the About page reads: the real update check (it looks only when asked, or once a day
// when the player turned that on, and never installs by itself), or with --about-demo a fixed view.
std::unique_ptr<etude::app::ui::AboutPort> make_about_port(const WindowOptions& opt,
                                                             const std::filesystem::path& data_dir);

// Everything a window run has before the scene, the sheet view and the UI exist.
struct WindowBase {
    WindowBase(WindowOptions& options, const WindowHandles& handles);
    WindowBase(const WindowBase&) = delete;
    WindowBase& operator=(const WindowBase&) = delete;

    WindowOptions& opt;
    SDL_Window* window;
    SDL_Renderer* renderer; // replaced when the device is lost (window_recover.cpp)
    bool vsync;
    const std::int64_t frame_ns;
    const bool latency_log;
    etude::RendererRecovery recovery;

    std::unique_ptr<MidiInput> midi = std::make_unique<MidiInput>();
    Session session;
    NoteTrail trail;
    std::vector<std::uint8_t> held;
    // Free play at a fixed key size: the held keys for the key view, and where it rested.
    std::vector<int> held_notes;
    std::optional<etude::KeyRange> view_range;
    std::vector<etude::TrailBar> bars;
    std::array<std::int64_t, 128> last_press{};
    std::array<std::uint8_t, 128> last_velocity{};
    etude::app::LatencyLog latency;
    // The built-in piano. MIDI notes play it only with
    // --audio-latency-log, to measure press-to-sound: a connected piano makes its own sound.
    etude::app::AudioOut audio;

    // Saved state first: the look, the three user slots, the app settings and the session.
    etude::app::Persistence persist{settings_directory(opt.settings_dir)};
    etude::app::UiModel model;
    // Transient feedback waits here and goes to the toast (the Ui shows one at a time, a newer
    // one replacing the last). A problem the player must not miss is a warning, which stays up
    // longer.
    etude::PendingToast pending_toast;
    // The crash handler, and the notice of the last crash (window_crash.cpp).
    etude::app::CrashSession crash;

    // Songs: the library folders, the practice history, and the controller that plays them.
    const std::filesystem::path data_dir{settings_directory(opt.settings_dir)};
    etude::app::SongLibrary library{song_roots_for(data_dir)};
    etude::app::HistoryStore history{data_dir, history_options_for(data_dir)};
    double history_open_ms = 0.0;
    etude::app::LibraryBinding library_view{library, history, data_dir / "Removed songs"};
    etude::app::CatalogueService catalogues{catalogue_options_for(opt, data_dir)};
    // New songs from outside the app: the library lists the folder again and one toast says how
    // many came. A song the app downloaded itself has its own notice, so it is taken off the
    // watcher's count (`own_downloads`, forgotten a few seconds after one, `watch_quiet_until`).
    etude::app::FolderWatcher songs_watch{songs_watch_options(data_dir)};
    std::size_t downloads_seen = 0;
    etude::OwnDownloads own_downloads;
    // The song that is open, as the upload server's threads may read it; and the server's port.
    etude::app::OpenSongBox open_song_box;
    // How long the songs play, from what the library parsed, for the phone page's list.
    etude::app::SongLengthBook song_lengths;
    bool lengths_stale = true; // the library changed since the book was made
    std::unique_ptr<etude::app::ui::UploadPort> upload_port{
        make_upload_port(opt, data_dir, open_song_box, song_lengths)};
    std::chrono::steady_clock::time_point watch_quiet_until{};
    std::unique_ptr<etude::app::SourcesController> sources{make_sources(opt, data_dir)};
    std::unique_ptr<etude::app::ui::AboutPort> about_port{make_about_port(opt, data_dir)};
    // The app icon's colour at run time and the best title for its row on the About page.
    etude::app::IconService icon_service{developer_run(opt), icon_title_for(opt)};
    etude::InputMode saved_input_mode = etude::InputMode::Midi;
    std::optional<etude::InputMode> input_override;
    // What the settings file should hold while a --input override is in force.
    etude::app::UiModel overridden;
    etude::app::MicSource mic{{opt.mic_wav, opt.mic_log}};
    std::vector<MidiEvent> mic_events;
    std::vector<etude::app::UnsureMark> unsure_new;
    std::vector<etude::app::UnsureMark> unsure_marks;
    std::vector<etude::app::UnsureMarker> unsure_draw;
    etude::app::SongController songs;
    etude::app::HandChordsFeed chord_feed; // the hands' chord labels, named on a worker
    etude::SessionState session_scratch;
    // The resume point (window_resume.cpp): what the last run left, what this start does with it, and the
    // keeper that writes this run's own.
    etude::app::ResumeKeeper resume;
    etude::ResumeAction resume_action = etude::ResumeAction::Fresh;
    etude::ResumePoint resume_loaded;
    etude::SessionState resume_scratch;
    // The sections of the loaded song, kept in the settings file with the rest.
    etude::app::SectionsController sections{songs, model.sections};
    std::optional<std::size_t> song_index;
    // The last run's results card, shown until the song plays again or what it was about
    // changes (the song, the mode, the hands, the speed, the loop or the position).
    std::optional<etude::ResultsCard> results_card;
    // The Learn drill the open song is (window_drill.cpp), when it is one: its finger numbers show on
    // the falling notes, its run ends in the drill's "How it went" card, and Faster plays it again
    // at the next tempo. `drill_card_asked`: that card's Card button was tapped, and the window opens
    // the Learn card the drill came from.
    std::optional<etude::DrillSpec> drill;
    bool drill_card_asked = false;
    bool start_drill(const etude::DrillSpec& spec);
    // The same with the drill already made (what Practice asked for): it is made once.
    bool start_drill(const etude::DrillSpec& spec, etude::Drill made);
    // A bundled melody of the Learn screen (window_songs.cpp): put under the controller as a library
    // song is, paused at its start, with no file of the player's; the history knows it by its file's
    // hash.
    void start_melody(etude::app::OpenedMelody melody);
    std::uint32_t results_serial = 0;
    // The run the card is about: its summary and replay, built on the history thread, and the
    // replay view with its own clock and sound (replay_session.hpp).
    ReplaySession replay;
    bool results_demo_shown = false;
    bool drill_demo_started = false;
    bool run_demo_shown = false;
    CardWatch card_watch;
    CardWatch run_watch;
    etude::RunPhase last_run_phase = etude::RunPhase::Idle;
    // The speed, count-in, hands and transposition of each song, kept in the history by the
    // song's content hash.
    etude::app::SongPracticeSync practice_sync{history, songs};
    // The song that is open, to find its place in the list again when the list is rearranged
    // (an ABC file is replaced by its tunes once the worker has read it).
    std::optional<etude::app::SongPath> open_song_ref;
    // The saved song is an ABC tune the worker has not listed yet: it is brought back once it
    // has, and the saved session stays as it is until then. Opening another song first
    // cancels it.
    bool restore_waiting = false;
    bool restore_cancelled = false; // another song was opened while waiting
    bool restore_deferred = false;  // the library changed while a song was being opened
    std::optional<etude::app::SongPath> restore_target; // the ABC file being waited for
    // A song asked for while the app runs is read and parsed on the opener's thread; the old
    // song keeps drawing until the new one is taken. Repeated PageDown steps on from the song
    // asked for last (`pending_index`), not from the one still open.
    etude::app::SongOpener opener{{}, std::chrono::milliseconds(opt.dev_open_delay_ms)};
    std::optional<PendingOpen> pending_open;
    // Bars the song being opened practises as soon as it is in (Progress's "Practise today").
    std::optional<etude::app::LibraryBinding::BarsOpen> pending_bars;
    // The saved session's song, read through the opener (the library path, then the absolute
    // one); `restore_waiting` stays set until it is in.
    etude::app::SavedSongRestore saved_restore;
    std::int64_t restore_began_ns = 0;
    // The controller's practice choices when the saved session's own were put in, to tell what
    // the player changes while the song is read.
    etude::SessionState restore_at_start;
    std::string restore_name; // the file being read, for the "Opening..." state
    std::optional<std::size_t> pending_index;
    std::string opening_status;
    long songs_opened = 0;        // songs brought in, for scripted runs to line up with
    bool library_opening = false; // the library asked for the song being opened
    bool restore_scripted = false;
    etude::TroubleRun demo_run; // --run-demo: an invented run, for the shots and the audit
    long frame_index = 0;
    double history_poll_max_us = 0.0;
    etude::app::SectionsController::HistoryNote last_history_note =
        etude::app::SectionsController::HistoryNote::None;

    // Startup steps, in the order the constructor runs them (window_startup.cpp).
    void load_saved_state();
    void start_crash_reports();
    void start_resume();
    void scan_library();
    void open_history();
    void start_catalogues();
    void choose_input();
    void setup_songs();
    void restore_saved_session();
    void apply_run_options();

    // Songs: opening, installing and restoring them (window_songs.cpp).
    etude::app::SongOptions song_options();
    void player_title(etude::app::LoadedSong& song);
    const etude::app::UiModel& to_save();
    etude::app::SongLoadOptions load_options(const std::filesystem::path& path, std::string tune);
    void install_song(const std::filesystem::path& path, const std::string& tune,
                      etude::app::LoadedSong song, etude::app::PreparedSong* prepared = nullptr,
                      bool from_session = false);
    void log_install(const std::string& title, std::size_t notes, std::int64_t began_ns);
    bool adopt_song(const std::filesystem::path& path, const std::string& tune,
                    etude::app::SongFileLoad load,
                    etude::app::PreparedSong* prepared = nullptr);
    bool open_song(const std::filesystem::path& path, const std::string& tune = {});
    void request_song(const std::filesystem::path& path, const std::string& tune,
                      std::optional<etude::app::LibraryBinding::SectionOpen> loop = {});
    void take_opened_song();
    void begin_saved_restore(bool known_missing);
    void finish_saved_restore(etude::app::SavedSongRestore::Result result);
    void take_saved_restore();
    void apply_saved_preferences();
    bool restore_step();

    // The profiles (window_profiles.cpp): what the switcher shows, and what a choice does.
    ProfileInfo profile_info(bool panel_open);
    void profile_choice(const ProfileChoice& choice);
    void switch_profile(std::int64_t id);
    void delete_profile(std::int64_t id);
    void profile_deleted(const etude::app::ProfileDeleted& gone);
    void add_profile(const ProfileChoice& choice);
    void edit_profile(const ProfileChoice& choice);
    void apply_active_profile();
    void practise_opened_bars(const etude::app::LibraryBinding::BarsOpen& bars);
    void reopen_profile_song();
    void refresh_profile_streaks();
    void refresh_profile_titles(bool panel_open);
    void refresh_icon_title();
    void demo_profiles(); // --profiles-demo
    void seed_card_run(etude::RunRecord& record); // --summary-board: the card's run, in the history
    void add_demo_run(std::int64_t id, etude::RunRecord record);
    bool profiles_demo_done = false;
    std::vector<int> profile_streaks;
    etude::app::ProfileLookCache profile_look_cache; // the other profiles' looks (Configs)
    std::vector<int> profile_freezes; // the streak freezes each has banked, in the same order
    std::size_t streaks_key = static_cast<std::size_t>(-1);
    std::int32_t streaks_day = 0;
    // Each profile's overall title for the switcher's tiles: asked of the history thread (the Progress
    // page's numbers, shared) when the panel opens, and read from the answer.
    std::vector<int> profile_titles;
    etude::app::ui::ProgressAsked titles_asked;
    std::shared_ptr<const etude::ProgressView> titles_from;
    bool titles_panel_open = false;
    // The best title of any profile, for the icon colours (window_profiles.cpp): worked out while the
    // About page asks for it, the same way as the switcher's titles.
    etude::app::ui::ProgressAsked icon_asked;
    std::shared_ptr<const etude::ProgressView> icon_from;
    bool icon_page_open = false;
    // The summary page's Board tab (window_profiles.cpp): the boards of the card's song, per period.
    void fill_board_info(BoardInfo& board, std::uint32_t card_serial, const etude::RunRecord& run);
    std::int32_t cached_today();
    static constexpr std::int64_t kTodayEveryNs = 1'000'000'000;
    std::int64_t today_read_ns = 0; // when the local day was last looked up
    std::int32_t today_cached = 0;
    SongBoardCache song_board;

    // The toast, the history, the results card and the run's check-in (window_runs.cpp).
    void show_message(std::string text, bool warning = false);
    void poll_history();
    CardWatch watch_now();
    void card_action(const etude::CardChoice& choice);
    const etude::TroubleRun& shown_run();
    void run_action(etude::RunCardAction action);
    void save_runs();
};

// Everything the window run has: the base, the scene, the sheet view, the UI and the frame loop's
// own state.
struct WindowState : WindowBase {
    WindowState(WindowOptions& options, const WindowHandles& handles);

    etude::app::Scene scene{renderer, model.look};
    etude::LookSettings applied_look = model.look;
    // The free-play scale overlay as drawn: worked out when the player's choice or the song
    // changes, never per frame (core scale_overlay.hpp). Off while a song is loaded.
    etude::ScaleOverlay applied_scale;
    etude::ScaleOverlayMask scale_mask;
    // Sheet music: engraved on its own thread, drawn over the note area (or its top strip).
    etude::app::SheetView sheet_view{renderer};
    // The song whose missing sheet music was already explained, so the toast shows once.
    std::uint32_t sheet_refusal_told = 0;
    etude::app::Ui ui{window, renderer};
    // The bar's transport, the section band and the resume countdown act on `songs`.
    etude::app::SongTransport transport{songs};
    std::int64_t last_activity_ns = INT64_MIN / 2;
    char out_of_range_line[64] = "";
    char dropped_line[64] = "";
    // Steps effects down when frames run over the refresh period.
    etude::EffectsGovernor governor{{.budget_ns = frame_ns}};
    char effects_line[64] = "";
    std::int64_t last_present_ns = midi_clock_ns();

    // The part of the keyboard on screen (Settings > View > Key size): the core picks the
    // window each frame and the follower slides the drawn one there.
    etude::KeyViewFollower view_follower;
    std::optional<KeyboardLayout> view_layout;
    etude::KeyViewFrame view_frame;
    ViewInputs view_inputs;
    std::int64_t view_ns = 0;

    // Playing without a MIDI keyboard: mouse, touch and typing become MidiEvents tagged as
    // virtual, go through on_event like MIDI, and also to the piano's MIDI output, or to the
    // built-in piano when no piano is connected.
    VirtualInput virtual_input;
    MidiOutput piano_out;
    etude::app::BuiltinPianoSink builtin_sink{audio.sink()};
    std::vector<MidiEvent> virtual_events;
    // The device check, and the open inputs it was last given.
    DeviceCheckHost device_check;
    std::vector<MidiInputInfo> check_inputs;
    bool check_ready = false;
    // The player's choice of MIDI ports going to the watcher thread, and the ports the Input page
    // lists (the watcher's plus the pretend keyboards).
    std::shared_ptr<MidiPorts> midi_ports = midi->ports();
    PortLists shown_ports;
    std::uint32_t shown_ports_seen = 0;
    std::vector<std::string> shown_pretend;
    PortFallback in_fallback;
    PortFallback out_fallback;
    OpenFailure out_open_failure;
    std::array<std::string_view, 128> typing_labels{};
    etude::KeyRange typing_keyboard{0, 0};
    int shown_octave_shift = virtual_input.typing().octave_shift();
    int shown_velocity = virtual_input.typing().velocity();

    // The song's sound goes where the player's does: the piano when connected, else the
    // built-in piano. A change of output silences what the old one had of the song. Both take
    // the song's events with their due times: the built-in piano starts each note on its
    // sample, the MIDI sender wakes for each message.
    etude::app::BuiltinSongSink builtin_song{audio.sink()};
    etude::app::TimedSender midi_sender{
        [this](const etude::PlaybackMessage& m) { piano_out.play(m); }};
    bool song_to_midi = false;
    std::vector<etude::SongSoundEvent> song_events;
    // The Learn screen's scales, sounded with the song's events (learn_sound.hpp).
    LearnSound learn_sound;
    // The player's note events of this frame, for the evaluators, in arrival order.
    std::vector<MidiEvent> frame_events;
    Robot robot;
    std::vector<MidiEvent> robot_events;
    bool panel_was_open = false;
    int logged_state = -1;
    long logged_opened = 0;
    bool logged_song = false;
    std::int64_t last_skip_ns = 0;
    std::vector<etude::VisibleNote> visible_notes;
    // The count band over the falling notes, and how far it is shown (count_band.hpp).
    etude::CountBand count_band;
    float count_band_shown = 0.0f;
    std::int64_t count_band_ns = 0;
    std::vector<etude::app::SongBar> song_bars;
    long reserved_for_open = -1; // the song (songs_opened) the lists above were sized for
    std::vector<etude::app::GradePopup> grade_popups;
    std::array<std::uint8_t, 128> hint_hands{};
    std::array<float, 128> wrong_flash{};       // per key, a wrong press's red flash (0 for none)
    std::array<std::uint8_t, 128> press_look{}; // per key, kPressFine, kPressWrong or pending
    std::string mic_label;
    // The review and the replay, as the UI is told each frame (window_replay.cpp).
    etude::app::ReviewInfo review_info;
    etude::app::ReplayInfo replay_info;
    std::vector<etude::ReplayBarMark> replay_marks;
    std::int64_t replay_ns = 0;
    bool replay_demo_done = false;
    // How far below the hit line a replay's marks are kept (song seconds): early bars lie there.
    static constexpr double kReplayBehindS = 0.4;

    std::int64_t next_frame_ns = midi_clock_ns();
    bool running = true;
    etude::app::IdleLoop idle =
        etude::app::make_idle_loop(std::getenv("ETUDE_NO_IDLE") == nullptr &&
                                     !opt.script.active() && !opt.audit.active() && !opt.robot);
    // The stall log (--frame-log, and --mic-log in a window): every frame over kFrameStallNs
    // prints its time by phase. `live` redraws inside the OS resize loop are not frames of the
    // loop and stay out of it.
    etude::app::FrameLog frame_log;
    // Scripted runs (the dev flags) print the song's state changes.
    const bool scripted = opt.script.active();
    const bool log_frames = opt.frame_log || opt.mic_log || opt.count_allocs;
    // With --allocs, the heap allocations of the main thread are counted from the
    // kAllocBaseFrame-th frame on (the first frames build caches), and printed at quit.
    static constexpr std::int64_t kAllocBaseFrame = 120;
    std::uint64_t alloc_base_ours = 0;
    std::uint64_t alloc_base_system = 0;
    std::int64_t alloc_base_frame = -1;
    std::int64_t frame_log_end_ns = etude::app::frame_clock_ns();
    // The UI's growth, held while the window is being resized (ui_grow_hold.hpp).
    etude::UiGrowHold ui_grow_hold;
    // The display's physical size, read once at start: the UI's sizes are real millimetres.
    etude::app::DisplayPanel panel;
    RedrawHook hook;
    // Headless: the target the frames are drawn into, as big as the window in pixels (it
    // follows a --resize).
    SDL_Texture* headless_target = nullptr;
    int headless_w = 0;
    int headless_h = 0;

    // A renderer lost after start-up (window_recover.cpp): recovered at a frame boundary, with play
    // paused. The `sim_` fields are the developer's --device-lost: the loss it pretends, the path
    // it asks for first and the presents it makes fail.
    long recoveries = 0;
    long recovered_frame = -100; // the frame of the last one
    bool sim_loss = false;
    std::optional<etude::RendererPath> sim_target;
    int sim_present_failures = 0;
    bool renderer_gone = false; // a renderer could not be made again: the run ends with an error
    bool recover_renderer();
    bool replace_renderer();
    void note_render_event(const SDL_Event& event);
    void take_simulated_loss();

    // The frame loop and what ends it (window_run.cpp, window_shutdown.cpp).
    void run_frames();
    void shutdown();

    // The events (window_events.cpp).
    etude::SoundSink& active_sink();
    void send_song(std::vector<etude::SongSoundEvent>& events);
    void send_learn_sound();
    bool card_up();
    bool run_present();
    bool run_card_up();
    void shortcut(const SDL_KeyboardEvent& key);
    void shortcut_practice(const SDL_KeyboardEvent& key, std::int64_t now);
    void shortcut_sections(const SDL_KeyboardEvent& key, std::int64_t now);
    void on_event(const MidiEvent& ev);
    void gather_presses();
    void handle_events();
    void before_events();
    void poll_events();
    bool route_event(const SDL_Event& event);
    void escape_key(const SDL_Event& event);
    void announce_typing_keys();
    bool quiet_device_check() const;
    void update_device_check(const PortChanges& changes);
    void follow_midi_ports();
    void take_dev_keyboards();
    void answer_device_check(etude::app::ui::DeviceCheckChoice choice);
    void track_crash_state();

    // The frame: update (window_frame.cpp), then draw and present (window_draw.cpp).
    void ensure_headless_target();
    std::int64_t begin_frame();
    void log_mark(etude::app::FramePhase phase, bool live) {
        if (log_frames && !live) {
            frame_log.mark(phase, etude::app::frame_clock_ns());
        }
    }
    void finish_frame(std::int64_t work_start_ns, bool live);
    void drain_input(bool live);
    void update_song_settings(std::int64_t now);
    void run_robot(std::int64_t now);
    void take_songs();
    void poll_library();
    void poll_songs_watch();
    void update_song(std::int64_t now);
    void show_demo_cards();
    void keep_session();
    void keep_resume_point();
    void close_resume_point();
    // The review and the replay of the last run (window_replay.cpp).
    void practise_review(int measure);
    void pin_review();
    void watch_review(int measure);
    void review_action(const etude::app::ReviewChoice& choice);
    void replay_action(const etude::app::ReplayChoice& choice);
    void replay_key(const SDL_KeyboardEvent& key);
    void update_replay(std::int64_t now);
    void show_replay_demo();
    void fill_replay_info(etude::app::UiFrameInfo& info);
    void gather_replay_notes();
    void settle_look(FrameCtx& ctx);
    void update_status_lines();
    void place_ui(FrameCtx& ctx);
    void engrave_sheet(FrameCtx& ctx);
    void fit_to_sheet(FrameCtx& ctx);
    void gather_song_notes(FrameCtx& ctx);
    void follow_key_view(FrameCtx& ctx);
    void fill_status(FrameCtx& ctx);
    void draw_scene(FrameCtx& ctx);
    void draw_ui(FrameCtx& ctx);
    void after_draw(bool live);
    void present(bool live);
    void govern_frame(std::int64_t work_start_ns, bool live);
};

} // namespace etude::app
