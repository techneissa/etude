// SPDX-License-Identifier: GPL-3.0-or-later
// Startup: the objects of the window run made and started in order, the saved state loaded, the
// saved session restored and the options of this run applied.
#include "window_state.hpp"
#include "crash_handler.hpp"
#include "log_ring.hpp"

#include "about_demo.hpp"
#include "configs_demo.hpp"
#include "loop_wake.hpp"
#include "mac_update.hpp"
#include "update_service.hpp"
#include "upload_controller.hpp"
#include "upload_demo.hpp"

#include "etude/mac_install.hpp"
#include "etude/update_key.hpp"
#include "etude/version.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace etude::app {

// The folders the library lists: the per-user Songs folder, and the repository's test songs
// in a developer build.
std::vector<std::filesystem::path> song_roots_for(const std::filesystem::path& data_dir) {
    std::vector<std::filesystem::path> song_roots{data_dir / "Songs"};
#ifdef ETUDE_DEV_SONGS_DIR
    song_roots.emplace_back(ETUDE_DEV_SONGS_DIR);
#endif
    return song_roots;
}

etude::app::FolderWatcher::Options songs_watch_options(const std::filesystem::path& data_dir) {
    etude::app::FolderWatcher::Options options;
    options.root = data_dir / "Songs";
    std::error_code ec;
    std::filesystem::create_directories(options.root, ec); // the watcher needs a folder to watch
    options.on_report = [] { etude::app::wake_loop(etude::app::WakeSource::FolderWatch); };
    return options;
}

std::unique_ptr<etude::app::ui::UploadPort>
make_upload_port(const WindowOptions& opt, const std::filesystem::path& data_dir,
                 etude::app::OpenSongBox& open_song, etude::app::SongLengthBook& lengths) {
    if (!opt.upload_demo.empty()) {
        return std::make_unique<etude::app::DemoUploadPort>(opt.upload_demo);
    }
    etude::app::UploadServerOptions options;
    options.songs = data_dir / "Songs";
    options.removed = data_dir / "Removed songs";
    options.port = opt.upload_port;
    options.all_interfaces = opt.upload_all_interfaces;
    options.request_deadline = std::chrono::seconds(opt.upload_deadline_s);
    if (!opt.upload_bind.empty()) {
        options.bind = {opt.upload_bind};
    }
    options.open_song = [&open_song] { return open_song.get(); };
    options.song_length = [&lengths](const std::filesystem::path& file) {
        return lengths.get(file);
    };
    // A server bound to loopback by name (--upload-bind) shows that address; the app's
    // own never does.
    const bool show_loopback = !opt.upload_bind.empty();
    return std::make_unique<etude::app::UploadController>(std::move(options), show_loopback);
}

std::unique_ptr<etude::app::SourcesController>
make_sources(const WindowOptions& opt, const std::filesystem::path& data_dir) {
    etude::app::SourcesOptions options;
    options.roots = etude::app::default_volume_roots();
    options.songs_folder = data_dir / "Songs";
    if (!opt.fake_volume.empty()) {
        options.roots.pretend = opt.fake_volume;
    }
    if (opt.fake_share == "running") {
        options.share.forced = etude::app::ui::ShareState::Shared;
    } else if (opt.fake_share == "none") {
        options.share.forced = etude::app::ui::ShareState::NotSetUp;
    }
    auto sources = std::make_unique<etude::app::SourcesController>(std::move(options));
    if (opt.sources_open == "network") {
        sources->set_open(etude::app::ui::SourcesSheet::Network);
    } else if (opt.sources_open == "usb") {
        sources->set_open(etude::app::ui::SourcesSheet::Usb);
    }
    return sources;
}

std::unique_ptr<etude::app::ui::AboutPort> make_about_port(const WindowOptions& opt,
                                                             const std::filesystem::path& data_dir) {
    using etude::app::ui::AboutPort;
    std::unique_ptr<AboutPort> port;
    if (!opt.about_demo.empty()) {
        port = std::make_unique<etude::app::DemoAboutPort>(
            opt.about_demo.empty() ? "notchecked" : opt.about_demo);
    } else {
        etude::app::UpdateOptions options;
        options.fetcher = std::make_unique<etude::app::CurlFetcher>();
        options.current_version = std::string(etude::app_version());
        options.machine = etude::app::read_machine_os();
        options.key_ready = etude::update_key_ready();
#if defined(__APPLE__)
        // A Mac replaces its own app bundle where it stands (update_install_mac.cpp); where it
        // cannot (a disk image, App Translocation, a folder it cannot write) the status line says
        // why and Update opens the release page.
        const etude::MacInstallFacts facts =
            etude::app::gather_mac_install_facts(etude::app::running_executable());
        const etude::MacInstallVerdict verdict = etude::check_mac_install(facts);
        options.mac.bundle = facts.bundle;
        options.mac.blocked = verdict.reason;
        options.mac.quit = [] {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit); // from the worker thread, which SDL allows
        };
        if (verdict.can_install()) {
            options.update_dir = data_dir / "update";
            std::error_code made;
            std::filesystem::create_directories(options.update_dir, made);
            options.installs = !made;
        }
        options.opens_browser = true;
#else
        // A release installed under <prefix>/versions/ installs an update itself, through the root
        // helper; anything else (a build run from its own folder) opens the release page.
        const std::filesystem::path exe = etude::app::running_executable();
        if (!exe.empty()) {
            options.update_dir = etude::app::update_dir_for(exe);
        }
        options.installs = !options.update_dir.empty() &&
                           ::access(options.update_dir.c_str(), W_OK) == 0;
#endif
        options.state_file = data_dir / "update-check.txt";
        port = std::make_unique<etude::app::UpdateService>(
            std::move(options), [](const std::string& url) { return SDL_OpenURL(url.c_str()); });
    }
    if (opt.about_link == "github") {
        port->set_link_sheet(std::string(etude::kRepositoryUrl));
    } else if (opt.about_link == "coffee") {
        port->set_link_sheet(std::string(etude::kBuyMeACoffeeUrl));
    } else if (opt.about_link == "release") {
        port->set_link_sheet(port->update().release_page.empty()
                                 ? "https://github.com/techneissa/etude/releases/tag/v1.1.0"
                                 : port->update().release_page);
    }
    return port;
}

// "Get songs": the free catalogues' listings (cached beside the settings, so they are
// searched offline too) and downloads into the Songs folder. Nothing is fetched until the
// player turns them on.
etude::app::CatalogueService::Options
catalogue_options_for(const WindowOptions& opt, const std::filesystem::path& data_dir) {
    etude::app::CatalogueService::Options catalogue_options;
    catalogue_options.cache_dir = data_dir / "Catalogue cache";
    catalogue_options.songs_dir = data_dir / "Songs";
    if (opt.catalogue_sites.empty()) {
        catalogue_options.fetcher = std::make_unique<etude::app::CurlFetcher>();
    } else {
        catalogue_options.fetcher = std::make_unique<etude::app::FolderFetcher>(
            opt.catalogue_sites, opt.catalogue_speed, opt.catalogue_fail_first);
    }
    return catalogue_options;
}

// The steps run in the order the objects were made when this was one function: the audio, the
// saved state, the library, the history, the catalogues, the input, the songs, the saved session
// and the options of this run.
WindowBase::WindowBase(WindowOptions& options, const WindowHandles& handles)
    : opt(options), window(handles.window), renderer(handles.renderer), vsync(handles.vsync),
      frame_ns(handles.frame_ns), latency_log(options.latency_log), recovery(handles.path) {
    held.reserve(128);
    held_notes.reserve(128);
    bars.reserve(1024);
    last_press.fill(etude::app::kNeverPressed);
    audio.start(opt.audio);
    load_saved_state();
    scan_library();
    open_history();
    start_catalogues();
    choose_input();
    setup_songs();
    restore_saved_session();
    apply_run_options();
}

void WindowBase::load_saved_state() {
    std::string loaded = persist.load(model);
    start_crash_reports();
    const bool problem = !loaded.empty();
    pending_toast.set(std::move(loaded), problem);
    start_resume();
}

void WindowBase::scan_library() {
    library.scan();
    library.start_loading(); // titles and lengths fill in behind the first frame
}

void WindowBase::open_history() {
    // The practice history is opened and read on its own thread; open() touches no file.
    const auto history_open_started = std::chrono::steady_clock::now();
    history.open();
    history_open_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                history_open_started)
                          .count();
}

void WindowBase::start_catalogues() {
    catalogues.set_online(model.app.get_songs_online);
    about_port->set_auto_check(model.app.update_auto_check);
    catalogues.start();
    library_view.set_catalogues(&catalogues);
}

void WindowBase::choose_input() {
    // The microphone. --mic-wav feeds a recording through the same path; --input picks the
    // source for this run (a recording alone implies the microphone).
    saved_input_mode = model.app.input_mode;
    input_override = opt.input_mode;
    if (!input_override && !opt.mic_wav.empty()) {
        input_override = etude::InputMode::Microphone;
    }
    if (input_override) {
        model.app.input_mode = *input_override;
    }
    mic_events.reserve(64);
}

void WindowBase::setup_songs() {
    sections.set_history(&history.runs());
    songs.set_options(song_options(), midi_clock_ns());
    songs.set_count_in_bars(model.session.count_in_bars);
    practice_sync.set_sections(&sections);
    practice_sync.set_accompaniment_default(model.app.accompaniment);
}

// The saved session: plan_restore decides what of it comes back.
void WindowBase::restore_saved_session() {
    if (!opt.song_path.empty()) {
        // The file named is the session's song when the content hash matches: then its speed,
        // count-in, hands and transposition come back from the session as they do at a normal
        // start; any other song takes its own from the history.
        const etude::SessionState left = model.session;
        model.session = etude::plan_restore(model.session, false, false, nullptr).state;
        songs.apply_session(model.session, midi_clock_ns());
        const etude::app::SongPath named = etude::app::split_song_path(opt.song_path);
        if (open_song(named.path, named.tune)) {
            songs.restore_song_settings(left, midi_clock_ns());
            if (left.song_hash != 0 && left.song_hash == songs.song().hash) {
                practice_sync.session_applied();
            }
        }
    } else if (model.session.has_song()) {
        // An ABC tune the worker has not listed yet is restored once it has (a scripted run
        // restores at once).
        const auto saved = library.resolve_song(model.session.song_path);
        const auto saved_abs = etude::app::split_song_path(model.session.song_abs_path);
        restore_target = saved ? saved
                               : (model.session.song_abs_path.empty()
                                      ? std::nullopt
                                      : std::optional<etude::app::SongPath>(saved_abs));
        // A run that scripts the song, or shows something about it, finds it open at the first
        // frame: the saved song is waited for here, before it.
        restore_scripted = opt.script.active() || opt.mode || opt.hands ||
                           opt.song_sound || opt.play || opt.seek_s >= 0.0 || opt.robot ||
                           opt.speed_percent || opt.count_in_bars || opt.results_demo ||
                           opt.summary_demo || opt.replay_demo_s >= 0.0 || !opt.run_demo.empty() ||
                           opt.panel;
        // The keys the player will have, so the song's tables are made for them.
        songs.set_keyboard(session.layout.range(), midi_clock_ns());
        restore_step();
        while (restore_scripted && saved_restore.active()) {
            if (auto result = saved_restore.poll(opener)) {
                finish_saved_restore(std::move(*result));
            } else {
                SDL_Delay(1);
            }
        }
    } else {
        // Free play restores nothing but the preferences.
        model.session = etude::plan_restore(model.session, false, false, nullptr).state;
        songs.apply_session(model.session, midi_clock_ns());
    }
    // The section last looped and the practice list item that was running, if the sections
    // still have them.
    if (!restore_waiting) {
        sections.restore(model.session, midi_clock_ns());
    }
}

void WindowBase::apply_run_options() {
    {
        const std::int64_t t = midi_clock_ns();
        if (opt.mode) {
            songs.set_mode(*opt.mode, t);
        }
        if (opt.hands) {
            songs.set_practice_hands(*opt.hands, t);
        }
        if (opt.song_sound) {
            songs.set_song_sound(*opt.song_sound, t);
        }
        if (opt.metronome) {
            model.app.metronome = *opt.metronome;
            songs.set_options(song_options(), t);
        }
        if (opt.count_in_bars) {
            songs.set_count_in_bars(*opt.count_in_bars);
        }
        if (opt.speed_percent) {
            songs.set_speed(*opt.speed_percent, t);
        }
    }
    apply_look_options(opt, model.look);
    if (opt.configs_demo) {
        for (int slot = 0; slot < etude::kUserPresetSlots; ++slot) {
            // --long-profile-name also gives the second config the longest name (48 bytes).
            const bool longest = opt.long_profile_name && slot == 1;
            const std::string name = longest ? "Wolfgang Amadeus Mozart's Practice Config Number"
                                             : "Config " + std::to_string(slot + 1);
            model.user_presets.save_slot(slot, name,
                                         configs_demo_look(static_cast<std::size_t>(slot)));
        }
    }
    if (opt.view) {
        model.app.view_mode = *opt.view;
    }
    persist.adopt(to_save()); // the options are for this run; only a later change is saved
    practice_sync.adopt();
}

WindowState::WindowState(WindowOptions& options, const WindowHandles& handles)
    : WindowBase(options, handles) {
    device_check.load(model.app, quiet_device_check());
    ui.set_library(&library_view);
    ui.set_learn(&learn_sound);
    if (opt.melodies_demo) {
        learn_sound.use_demo_scores();
    }
    if (opt.ear_seed != 0) {
        ui.set_ear_seed(opt.ear_seed);
    }
    ui.set_upload(upload_port.get());
    ui.set_sources(sources.get());
    ui.set_about(about_port.get());
    ui.set_icons(&icon_service);
    ui.set_library_query(opt.library_query);
    if (opt.panel) {
        ui.open_panel(*opt.panel);
    } else if (resume_action == etude::ResumeAction::Resume) {
        ui.open_resume_screen(resume_loaded.screen); // the screen the player was on, as they left it
    } else if (resume_action == etude::ResumeAction::OpenSongList) {
        ui.open_library();
    }
    panel.resolve(window, opt.panel_mm, opt.headless);
    log_printf( "%s\n", panel.describe().c_str());
    virtual_events.reserve(64);
    song_events.reserve(256);
    frame_events.reserve(64);
    virtual_input.set_ui_owns_point([this](float nx, float ny) { return ui.owns_point(nx, ny); });
    virtual_input.set_ui_blocks_presses([this] { return ui.blocks_key_presses(); });
    sheet_view.set_ui_owns_point([this](float nx, float ny) { return ui.owns_point(nx, ny); });
    // The watcher thread finds and opens the piano's output port after each scan of the inputs.
    midi->add_port_task(piano_out.port_task(midi_ports));
    ui.set_midi_ports(&shown_ports);
    if (opt.seek_s >= 0.0) {
        songs.seek(opt.seek_s, midi_clock_ns());
    }
    if (opt.play) {
        songs.play(midi_clock_ns());
    }
    songs.set_event_pull([this] { gather_presses(); });
    if (log_frames) {
        etude::app::set_active_frame_log(&frame_log);
    }
    if (opt.count_allocs) {
        etude::app::count_allocations(true);
    }
    hook.window = SDL_GetWindowID(window);
    hook.frame_ns = frame_ns;
    if (!opt.crash_test.empty()) {
        // Developer: the crash handler's test crashes the app here, with the window made.
        log_printf("etude: crashing on purpose (%s)\n", opt.crash_test.c_str());
        if (!crash_on_purpose(opt.crash_test)) {
            log_printf("etude: --crash-test takes segv, bus, ill, fpe, abort or throw\n");
        }
    }
}

} // namespace etude::app
