// SPDX-License-Identifier: GPL-3.0-or-later
// Shutdown: nothing may keep sounding, the song's run is saved, the history thread makes its last
// attempt and the settings are written.
#include "window_state.hpp"
#include "log_ring.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace etude::app {
namespace {

// The history thread makes its last attempt and keeps what it cannot write in the journal; runs
// that not even the journal could keep are told to the player while the window is up. The line on
// stderr is always written (headless, and the Pi's console, which has no display server for
// the message box; systemd keeps it in the journal).
void close_history(etude::app::HistoryStore& history, const WindowOptions& opt,
                   SDL_Window* window) {
    history.close();
    for (const std::string& lost : history.take_errors()) {
        log_printf( "etude: %s\n", lost.c_str());
        // Only a person at the window can click it away: a scripted, audited or headless
        // run has nobody to.
        if (!opt.headless && !opt.script.active() && !opt.audit.active()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Etude", lost.c_str(), window);
        }
    }
}

} // namespace

void WindowState::shutdown() {
    if (log_frames) {
        etude::app::set_active_frame_log(nullptr);
        frame_log.print_summary();
        if (alloc_base_frame >= 0 && frame_index > alloc_base_frame) {
            const auto frames = static_cast<double>(frame_index - alloc_base_frame);
            std::printf(
                "allocations per frame (frames %lld to %lld): ours %.2f, made by "
                "system libraries %.2f\n",
                static_cast<long long>(alloc_base_frame), static_cast<long long>(frame_index),
                static_cast<double>(etude::app::allocations_counted() - alloc_base_ours) / frames,
                static_cast<double>(etude::app::system_allocations_counted() -
                                    alloc_base_system) /
                    frames);
        }
    }
    // Nothing may keep sounding on the piano after the app is gone: the song first (its run is
    // saved, its notes stopped, every queued message sent), then the player's notes.
    {
        const std::int64_t now = midi_clock_ns();
        if (!restore_waiting) {
            etude::SessionState saved = songs.session_state(model.session);
            sections.save_into(saved);
            if (!(saved == model.session)) {
                model.session = saved;
            }
        }
        songs.shutdown(now, song_events);
        send_song(song_events);
        builtin_song.all_off();
        midi_sender.stop(); // delivers what is queued, then no more from the thread
        save_runs();
    }
    virtual_events.clear();
    virtual_input.release_all(virtual_events);
    for (const MidiEvent& ev : virtual_events) {
        etude::forward_to_sink(active_sink(), ev);
    }
    piano_out.all_off();
    practice_sync.flush();
    close_resume_point();
    persist.flush(to_save());
    close_history(history, opt, window);
}

} // namespace etude::app
