// SPDX-License-Identifier: GPL-3.0-or-later
// Opening a song, installing it under the controller, and bringing back the saved session's song.
#include "window_state.hpp"
#include "log_ring.hpp"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace etude::app {

etude::app::SongOptions WindowBase::song_options() {
    etude::app::SongOptions o;
    o.hit_window_ms = model.app.hit_window_ms;
    o.latency_ms = model.app.latency_correction_ms;
    o.lookahead_s = model.look.visible_seconds;
    o.resume_countdown_s = model.app.resume_countdown_s;
    o.skip_back_seconds = model.app.skip_back_seconds;
    o.skip_forward_seconds = model.app.skip_forward_seconds;
    o.sound_lead_ns = opt.song_lead_ns;
    o.metronome = model.app.metronome;
    o.click_voice = model.app.metronome_sound;
    o.click_volume = model.app.metronome_volume;
    o.metronome_grid.accent = model.app.metronome_accent;
    o.metronome_grid.subdivision = model.app.metronome_subdivision;
    o.default_count_in_bars = etude::kDefaultCountInBars;
    o.accompaniment_volume = model.app.accompaniment_volume;
    // The microphone reports a note some time after its onset.
    o.input_delay_ms = model.app.input_mode == etude::InputMode::Midi ? 0 : etude::kMicSettleMs;
    return o;
}

// A song the player renamed is shown under their name wherever it is open.
void WindowBase::player_title(etude::app::LoadedSong& song) {
    const auto it = history.song_prefs().find(song.hash);
    if (it != history.song_prefs().end() && !it->second.title.empty()) {
        song.title = it->second.title;
    }
}

const etude::app::UiModel& WindowBase::to_save() {
    if (input_override && model.app.input_mode == *input_override) {
        overridden = model;
        overridden.app.input_mode = saved_input_mode;
        return overridden;
    }
    return model;
}

// How a file is read besides its bytes: an ABC tune, and the chord symbols setting.
etude::app::SongLoadOptions WindowBase::load_options(const std::filesystem::path& path,
                                                       std::string tune) {
    etude::app::SongLoadOptions o;
    o.abc_chords = model.app.abc_chords;
    // An ABC tune the library has listed is read alone, not by reading and listing its
    // file again.
    o.known = library.abc_known(path, tune);
    o.tune = std::move(tune);
    return o;
}

// Puts a loaded song under the controller: its paths, its place in the list, and the
// card of the run before it goes. Opens and the restore of the saved session share it.
void WindowBase::install_song(const std::filesystem::path& path, const std::string& tune,
                              etude::app::LoadedSong song, etude::app::PreparedSong* prepared,
                              bool from_session) {
    drill.reset(); // a song of the library is not a drill
    song.library_path = library.relative_path(path, tune);
    song.absolute_path = etude::join_abc_location(etude::app::absolute_path_text(path), tune);
    std::string open_text = song.absolute_path; // for the upload server's guard
    player_title(song);
    practice_sync.song_closing(); // the song that goes keeps its last change
    if (prepared != nullptr) {
        songs.load(std::move(song), midi_clock_ns(), std::move(*prepared));
    } else {
        songs.load(std::move(song), midi_clock_ns());
    }
    practice_sync.song_opened(from_session, midi_clock_ns());
    song_index = library.index_of(path, tune);
    open_song_ref = etude::app::SongPath{path, tune};
    open_song_box.set(std::move(open_text));
    ++songs_opened;
    results_card.reset();
    // A replay of the old song's run must not stay over the new song: its view closes (what it
    // sounded is let go with the next frame) and its review goes.
    replay.close_view();
    replay.forget_review();
}

// A bundled melody of the Learn screen (#375): put under the controller as a library song is, paused
// at its start, but with no file of the player's: it has no path, so the saved session never
// reopens it and the history knows it by its file's hash. Its practice choices come back as any
// song's do.
void WindowBase::start_melody(etude::app::OpenedMelody melody) {
    drill.reset();
    const std::string title = melody.song.title;
    practice_sync.song_closing(); // the song that goes keeps its last change
    songs.load(std::move(melody.song), midi_clock_ns());
    practice_sync.song_opened(false, midi_clock_ns());
    song_index.reset();
    open_song_ref.reset();
    open_song_box.set(std::string{});
    ++songs_opened;
    results_card.reset();
    replay.close_view();
    replay.forget_review();
    pending_toast.set_song(title, melody.composer, false, !melody.composer.empty());
}

// --frame-log: what the frame thread spent taking a song in (the `song` detail of the
// frame, which prints only when the whole frame stalls).
void WindowBase::log_install(const std::string& title, std::size_t notes, std::int64_t began_ns) {
    if (opt.frame_log || opt.count_allocs) {
        std::printf("song install: %s, %zu notes, %.2f ms\n", title.c_str(), notes,
                    static_cast<double>(etude::app::frame_clock_ns() - began_ns) / 1e6);
    }
}

// Brings a loaded song into the app: the toast, the list position, the restore's state.
bool WindowBase::adopt_song(const std::filesystem::path& path, const std::string& tune,
                            etude::app::SongFileLoad load, etude::app::PreparedSong* prepared) {
    const etude::app::FrameDetailScope loading(etude::app::FrameDetail::OpenSong);
    if (!load.ok) {
        pending_toast.set(load.error, true);
        return false;
    }
    player_title(load.song); // the toast says the player's name for it
    const bool split = load.song.hands_split;
    const std::string title = load.song.title;
    if (!load.warnings.empty()) {
        // A count only: the title and the warnings carry the player's own song names.
        log_printf( "etude: a song loaded with %zu warning(s)\n", load.warnings.size());
    }
    const std::int64_t install_began_ns = etude::app::frame_clock_ns();
    const std::size_t install_notes = load.song.song.notes.size();
    install_song(path, tune, std::move(load.song), prepared);
    log_install(title, install_notes, install_began_ns);
    restore_cancelled = restore_waiting;
    // The song-opened toast: the name, and under it the composer (the player's, else the
    // file's) with what the open noted (a split by pitch, an ABC tune's skipped parts).
    std::string composer = load.entry.composer;
    if (const auto it = history.song_prefs().find(load.song.hash);
        it != history.song_prefs().end() && !it->second.composer.empty()) {
        composer = it->second.composer;
    }
    std::string notes;
    if (split) {
        notes = "hands split by pitch";
    }
    if (!load.warnings.empty()) {
        // An ABC tune says what it skipped; a score file's only warning is a guessed
        // length.
        if (!notes.empty()) {
            notes += ", ";
        }
        notes += load.format == etude::SongFileFormat::Abc
                     ? load.warnings.front() +
                           (load.warnings.size() > 1
                                ? " (and " + std::to_string(load.warnings.size() - 1) + " more)"
                                : "")
                     : "note lengths guessed";
    }
    std::string second_line = composer;
    if (!notes.empty()) {
        second_line += composer.empty() ? notes : ", " + notes;
    }
    pending_toast.set_song(title, std::move(second_line), !load.warnings.empty(),
                           !composer.empty());
    return true;
}

// Start-up only, before the first frame: the song is read here.
bool WindowBase::open_song(const std::filesystem::path& path, const std::string& tune) {
    return adopt_song(path, tune, etude::app::load_song_file(path, load_options(path, tune)));
}

// A song asked for by a key or a script: read off the frame thread, adopted by
// `take_opened_song` on the frame that finds it ready. A request replaces the one before.
void WindowBase::request_song(const std::filesystem::path& path, const std::string& tune,
                              std::optional<etude::app::LibraryBinding::SectionOpen> loop) {
    // The opener serves one request, so a saved song still being read is let go: the
    // restore starts again if this open fails, and is cancelled if it opens.
    saved_restore.abandon();
    pending_bars.reset();
    opener.request(path, load_options(path, tune), songs.load_plan());
    pending_open = PendingOpen{{path, tune}, etude::app::frame_clock_ns(), loop};
    pending_index = library.index_of(path, tune);
    opening_status.clear();
}

void WindowBase::take_opened_song() {
    if (!pending_open) {
        return;
    }
    if (auto opened = opener.take()) {
        const auto loop = pending_open->loop;
        pending_open.reset();
        pending_index.reset();
        opening_status.clear();
        if (loop) {
            practice_sync.open_with_loop(loop->hash, loop->start_s, loop->end_s, loop->section_id);
        }
        const bool adopted =
            adopt_song(opened->path, opened->tune, std::move(opened->load), &opened->prepared);
        practice_sync.clear_opening_loop(); // a failed load leaves nothing for the next open
        if (adopted && pending_bars) {
            practise_opened_bars(*pending_bars);
        }
        pending_bars.reset();
    }
}

// The bars Progress named, practised as "Practise these bars" would: one loop over them, together.
void WindowBase::practise_opened_bars(const etude::app::LibraryBinding::BarsOpen& named) {
    if (!songs.has_song() || songs.song().hash != named.hash) {
        return;
    }
    std::vector<int> chosen;
    const std::size_t most = static_cast<std::size_t>(etude::kMaxCardBars);
    for (int bar = named.first; bar <= named.last && chosen.size() < most; ++bar) {
        chosen.push_back(bar);
    }
    const etude::Song& played = songs.song().song;
    const etude::BarPractice practice = etude::plan_bar_practice(
        chosen, played.measures, played.duration_s, etude::BarPracticeKind::Together);
    if (!sections.practise_bars(practice, midi_clock_ns())) {
        show_message(sections.take_message());
    }
}

// Brings back the saved session's song. An ABC tune is read through the library's
// listing, so while the worker has not listed that file the restore waits for it
// (`restore_waiting`) and the saved session stays as it is. The song itself is read off
// the frame thread (`saved_restore`): `begin_saved_restore` asks for it, and
// `finish_saved_restore` runs on the frame that finds it read. `known_missing`: the library
// has said the file or the tune is not there, so nothing is read.
void WindowBase::begin_saved_restore(bool known_missing) {
    etude::app::SavedSongRestore::Request request;
    request.saved = model.session;
    if (!known_missing) {
        request.library = library.resolve_song(model.session.song_path);
        if (!model.session.song_abs_path.empty()) {
            request.absolute = etude::app::split_song_path(model.session.song_abs_path);
        }
    }
    request.options = [&](const etude::app::SongPath& song) {
        return load_options(song.path, song.tune);
    };
    // The hands and the key the session will apply, so the tables made while the song is
    // read are the ones it needs.
    request.plan = songs.load_plan_for(model.session);
    restore_name.clear();
    if (request.library) {
        restore_name = request.library->path.filename().string();
    } else if (request.absolute) {
        restore_name = request.absolute->path.filename().string();
    }
    restore_began_ns = etude::app::frame_clock_ns();
    saved_restore.begin(opener, std::move(request));
    restore_waiting = true;
}

void WindowBase::finish_saved_restore(etude::app::SavedSongRestore::Result result) {
    opening_status.clear();
    const std::string tune = result.song ? result.song->tune : std::string();
    etude::app::SongFileLoad& load = result.load;
    const etude::RestorePlan plan =
        etude::plan_restore(model.session, result.probe.found, result.probe.hash_matches,
                              load.ok ? &load.song.song : nullptr);
    const std::string name =
        model.session.song_path.empty() ? model.session.song_abs_path : model.session.song_path;
    // What the player changed while the song was read stays as they left it.
    model.session = etude::keep_player_changes(plan.state, restore_at_start,
                                                 songs.session_state(model.session));
    // The hands and the key first (the song's tables were made for them), then the song,
    // then the rest of the session.
    std::optional<etude::app::FrameDetailScope> loading;
    if (plan.load_song) {
        loading.emplace(etude::app::FrameDetail::OpenSong);
    }
    practice_sync.restore_session(
        plan, model.session,
        [&](bool from_session) {
            const std::int64_t install_began_ns = etude::app::frame_clock_ns();
            const std::string title = load.song.title;
            const std::size_t install_notes = load.song.song.notes.size();
            install_song(result.song->path, tune, std::move(load.song), &result.prepared,
                         from_session);
            log_install(title, install_notes, install_began_ns);
        },
        midi_clock_ns());
    loading.reset();
    switch (plan.outcome) {
    case etude::RestoreOutcome::SongMissing:
        pending_toast.set(std::string("Last song not found: ") + name, true,
                          {std::string_view("Last song not found: ").size(), name.size()});
        break;
    case etude::RestoreOutcome::SongUnreadable:
        pending_toast.set(std::string("Last song can't be read: ") + name, true,
                          {std::string_view("Last song can't be read: ").size(), name.size()});
        break;
    case etude::RestoreOutcome::SongChanged:
        pending_toast.set("Last song changed since; starting it from the top", true);
        break;
    case etude::RestoreOutcome::Restored:
    case etude::RestoreOutcome::NoSong:
        break;
    }
    restore_waiting = false;
    restore_cancelled = false;
}

// Once a frame while a saved song is being read: takes it in when it is ready.
void WindowBase::take_saved_restore() {
    if (!saved_restore.active()) {
        return;
    }
    if (auto result = saved_restore.poll(opener)) {
        finish_saved_restore(std::move(*result));
        sections.restore(model.session, midi_clock_ns());
    }
}

// The choices that do not belong to a song come back at once, while the song is waited for
// or read; the controller's state then is what a change by the player is told from.
void WindowBase::apply_saved_preferences() {
    songs.apply_session(etude::plan_restore(model.session, false, false, nullptr).state,
                        midi_clock_ns());
    restore_at_start = songs.session_state(model.session);
}

bool WindowBase::restore_step() {
    etude::app::AbcRestoreState now;
    now.scripted = restore_scripted;
    now.cancelled = restore_cancelled;
    now.library_pending = library.state().pending() > 0;
    if (restore_target) {
        std::string extension = restore_target->path.extension().string();
        for (char& c : extension) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        now.abc = extension == ".abc";
        if (now.abc) {
            now.status = library.abc_status(restore_target->path, restore_target->tune);
        }
    }
    const etude::app::AbcRestoreStep step = etude::app::decide_abc_restore(now);
    switch (step) {
    case etude::app::AbcRestoreStep::Wait:
        if (!restore_waiting) {
            restore_waiting = true;
            library.prioritize(restore_target->path);
            apply_saved_preferences();
        }
        return false;
    case etude::app::AbcRestoreStep::Cancel:
        break;
    case etude::app::AbcRestoreStep::NotFound:
    case etude::app::AbcRestoreStep::Restore:
        // The song is read off the frame thread: the restore is over when it is in.
        if (!restore_waiting) { // not already put in while the listing was waited for
            apply_saved_preferences();
        }
        begin_saved_restore(step == etude::app::AbcRestoreStep::NotFound);
        return false;
    }
    restore_waiting = false;
    restore_cancelled = false;
    return true;
}

// Files that arrived, changed or left the Songs folder without the app doing it: the library lists
// the folder again (it parses only the files whose size or time changed), and one toast says how
// many new songs came. A song the app downloaded itself already had its notice, and is not counted.
void WindowState::poll_songs_watch() {
    const std::size_t landed = library_view.downloads_landed();
    if (landed != downloads_seen) {
        own_downloads.landed(landed - downloads_seen);
        downloads_seen = landed;
        watch_quiet_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    } else if (std::chrono::steady_clock::now() >= watch_quiet_until) {
        own_downloads.forget();
    }
    const std::optional<etude::WatchReport> report = songs_watch.take();
    if (!report) {
        return;
    }
    library.rescan();
    const std::string text = etude::new_songs_text(own_downloads.take(report->new_songs));
    if (!text.empty()) {
        pending_toast.set(text, false);
    }
}

} // namespace etude::app
