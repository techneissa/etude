// SPDX-License-Identifier: GPL-3.0-or-later
// The window's side of crash reports: the handler and the notice at the start, and the screen and
// the song the report will name.
#include "window_state.hpp"

#include "crash_handler.hpp"
#include "log_ring.hpp"

namespace etude::app {

void WindowBase::start_crash_reports() {
    std::filesystem::path install;
    if (const char* base = SDL_GetBasePath()) {
        install = install_dir_of(base);
    }
    crash.start(data_dir, install);
    crash.begin_notice(data_dir);
    if (!opt.crash_demo.empty()) {
        crash.demo(opt.crash_demo);
    }
}

// Every frame the notice waits for the profiles' names; every so often, not every frame, the screen
// and the song: a report is no better for a screen name a frame fresher.
void WindowState::track_crash_state() {
    // The names are known once the history has loaded; a history that cannot open for now (busy, not
    // usable) stays unloaded for as long as it retries, and the notice does not wait for that: the
    // report is scrubbed of the paths and the names it has.
    const bool names_known = !history.loading() || history.state() != etude::app::HistoryState::Loading;
    std::vector<std::string> names;
    if (names_known) {
        for (const etude::Profile& profile : history.profiles().all()) {
            names.push_back(profile.name);
        }
    }
    crash.update(names_known, names);
    if (crash.take_sheet_request()) {
        ui.open_crash_report(); // "Restart and send report": the report to read, then the code
        log_printf("etude: the crash report's sheet opens (Restart and send report)\n");
    }
    if (frame_index % 15 != 0) {
        return;
    }
    const char* screen = ui.library_open()  ? "library"
                         : ui.learn_open()  ? "learn"
                         : ui.sheet_open()  ? "settings"
                         : ui.editor_open() ? "section editor"
                         : songs.has_song() ? (songs.playing() ? "playing a song" : "a song, paused")
                                            : "free play";
    crash.track(screen, songs.has_song() ? std::string_view(songs.song().title) : std::string_view());
}

} // namespace etude::app
