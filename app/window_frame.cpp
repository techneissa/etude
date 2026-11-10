// SPDX-License-Identifier: GPL-3.0-or-later
// The frame, first half: the input and the song move on (the second half, window_draw.cpp, lays
// the frame out, draws and presents it).
#include "window_state.hpp"

#include "etude/update_handoff.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <utility>

namespace etude::app {

// Headless: the target the frames are drawn into, as big as the window in pixels (it
// follows a --resize).
void WindowState::ensure_headless_target() {
    if (!opt.headless) {
        return;
    }
    int pw = 0;
    int ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    if (headless_target != nullptr && pw == headless_w && ph == headless_h) {
        return;
    }
    gfx::set_target(renderer, nullptr);
    gfx::destroy_texture(headless_target);
    headless_target =
        gfx::create_texture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET,
                            std::max(1, pw), std::max(1, ph));
    headless_w = pw;
    headless_h = ph;
    if (headless_target == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "headless target failed: %s", SDL_GetError());
    }
    gfx::set_target(renderer, headless_target);
}

// Waits for a free back buffer: Metal blocks for one in the first draw after a present, and
// that wait has to come before the MIDI drain, not after. Returns when the frame's work
// began.
std::int64_t WindowState::begin_frame() {
    ensure_headless_target();
    gfx::clear(renderer, 0, 0, 0, 255);
    gfx::flush(renderer);
    return midi_clock_ns();
}

// Everything after the events: drain MIDI, move the view, draw, present. `live` is a redraw
// from inside the OS's window-resize loop, which must not feed the effects governor (its
// frame times are the resize's, not the scene's).
void WindowState::finish_frame(std::int64_t work_start_ns, bool live) {
    FrameCtx ctx;
    ctx.live = live;
    drain_input(live);
    ctx.now = midi_clock_ns();
    audio.update(ctx.now);
    update_song_settings(ctx.now);
    run_robot(ctx.now);
    take_songs();
    poll_library();
    update_song(ctx.now);
    show_demo_cards();
    keep_session();
    keep_resume_point();
    settle_look(ctx);
    update_status_lines();
    place_ui(ctx);
    engrave_sheet(ctx);
    fit_to_sheet(ctx);
    gather_song_notes(ctx);
    follow_key_view(ctx);
    fill_status(ctx);
    draw_scene(ctx);
    draw_ui(ctx);
    send_learn_sound();
    after_draw(live);
    present(live);
    govern_frame(work_start_ns, live);
}

// What the Learn screen asked for while it was drawn (Play, Stop) goes to the sound in the same
// frame: a tap is answered in draw_ui, so waiting for the next frame's update_song would spend a
// frame of the notes' look-ahead before the first was sent.
void WindowState::send_learn_sound() {
    // Practice was tapped: the drill opens in the player, its notes falling.
    if (std::optional<DrillRequest> asked = learn_sound.take_drill()) {
        start_drill(asked->spec, std::move(asked->drill));
    }
    // A melody was tapped: it opens in the player, ready to play.
    if (std::optional<etude::app::OpenedMelody> melody = learn_sound.take_melody()) {
        start_melody(std::move(*melody));
    }
    learn_sound.take(song_events);
    send_song(song_events);
}

void WindowState::drain_input(bool live) {
    // Drain MIDI last, right before drawing, so a key press is in this frame.
    idle.begin_frame(live, frame_moves(songs, ui, session, model.look, scene) ||
                               (replay.viewing() && replay.playing()));
    bool label_changed = false;
    // --frame-log: what the MIDI ports cost this frame (`ports` in a stall's line); a
    // frame in which an input came or went, or that spent a millisecond or more on them, prints
    // its own line.
    const std::int64_t ports_began_ns = etude::app::frame_clock_ns();
    const etude::app::PortChanges port_changes = [&] {
        const etude::app::FrameDetailScope ports(etude::app::FrameDetail::Ports);
        return midi->update();
    }();
    const KeyTransition unplugged = apply_port_changes(session, *midi, port_changes, label_changed);
    if (apply_keyboard_choice(session, model.session)) {
        label_changed = true;
    }
    update_device_check(port_changes);
    follow_midi_ports();
    if (unplugged.type == KeyTransitionType::AllReleased) {
        trail.apply(unplugged, midi_clock_ns());
        last_press.fill(etude::app::kNeverPressed);
    }
    if (session.layout.range().lowest != typing_keyboard.lowest ||
        session.layout.range().highest != typing_keyboard.highest) {
        typing_keyboard = session.layout.range();
        virtual_input.set_keyboard(typing_keyboard);
    }
    gather_presses();
    // The microphone's settings; its notes were gathered with the keyboard's above.
    mic.apply(model.app, midi_clock_ns());
    mic.write_back(model.app);
    gather_presses(); // the keys a microphone that just closed let go
    {
        const etude::app::FrameDetailScope ports(etude::app::FrameDetail::Ports);
        piano_out.update();
    }
    if (log_frames) {
        const std::int64_t ports_ns = etude::app::frame_clock_ns() - ports_began_ns;
        if (port_changes.opened > 0 || port_changes.closed > 0 || label_changed ||
            ports_ns >= 1'000'000) {
            std::printf("frame %lld: MIDI ports took %.2f ms (inputs +%d -%d)\n",
                        static_cast<long long>(frame_index), static_cast<double>(ports_ns) / 1e6,
                        port_changes.opened, port_changes.closed);
        }
    }
}

// The song: settings, the menu, the player's notes, then its MIDI out.
void WindowState::update_song_settings(std::int64_t now) {
    // An accepted offer that the keyboard's own octave button shifted what it sends: the
    // layout follows, so the screen keyboard, the keys expected and the folding are what
    // the keyboard really sends.
    if (int shift = 0; songs.take_keyboard_shift(&shift)) {
        const std::optional<etude::KeyRange> moved =
            etude::shifted_range(base_range(session), shift);
        if (moved) {
            session.sent_shift = shift;
            rebuild_layout(session);
        }
    }
    songs.set_keyboard_shift(session.sent_shift);
    songs.set_keyboard(session.layout.range(), now);
    // A slider being dragged changes its setting every frame: the hit window and the
    // latency are applied when the finger lifts.
    songs.set_options(song_options(), now, ui.slider_held());
    if (const bool open = ui.panel_open(); open != panel_was_open) {
        panel_was_open = open;
        if (scripted) {
            std::printf("frame %ld: settings %s\n", frame_index, open ? "open" : "closed");
        }
        if (open) {
            songs.menu_opened(now);
        } else {
            songs.menu_closed(now);
        }
    }
}

void WindowState::run_robot(std::int64_t now) {
    if (opt.robot) {
        robot_events.clear();
        robot.update(songs, now, robot_events);
        for (const MidiEvent& ev : robot_events) {
            on_event(ev);
        }
    }
}

// Takes in the song the opener or the saved restore has ready, and the words for a slow open.
void WindowState::take_songs() {
    const long opened_before = songs_opened;
    take_saved_restore();
    take_opened_song();
    if (songs_opened != reserved_for_open) {
        // A new song: room for its densest view in the frame's lists and the scene's
        // meshes, so the notes coming into view grow nothing at the look-ahead the song
        // was opened with; raising "Notes ahead" later grows them once.
        reserved_for_open = songs_opened;
        const std::size_t densest = songs.densest_window();
        visible_notes.reserve(densest);
        song_bars.reserve(densest);
        scene.reserve_notes(densest);
    }
    if (library_opening && (!pending_open || !ui.library_open())) {
        // The library's song is in (or failed: the toast says why, and the library stays
        // for another choice), or the player closed the library meanwhile.
        library_opening = false;
        if (songs_opened != opened_before && !pending_open) {
            ui.close_library();
        }
    }
    if (scripted && songs_opened != logged_opened) {
        logged_opened = songs_opened;
        std::printf("frame %ld: opened %s\n", frame_index,
                    open_song_ref ? open_song_ref->path.filename().string().c_str() : "");
    }
    if (pending_open && opening_status.empty() &&
        etude::app::opening_shown(etude::app::frame_clock_ns() - pending_open->since_ns)) {
        opening_status =
            etude::app::opening_text(pending_open->song.path, pending_open->song.tune);
    } else if (saved_restore.active() && opening_status.empty() &&
               etude::app::opening_shown(etude::app::frame_clock_ns() - restore_began_ns)) {
        opening_status = etude::app::opening_text(restore_name, {});
    }
}

// The library: its worker's changes, the saved song's restore waiting on them, and the library
// screen's catalogue.
void WindowState::poll_library() {
    poll_songs_watch();
    if (const std::optional<std::string> told = sources->take_notice()) {
        pending_toast.set(*told, true);
    }
    about_port->set_auto_check(model.app.update_auto_check); // the daily check, page open or not
    icon_service.sync(window, model.app.icon_colour); // the Dock icon or launcher: at start-up too
    refresh_icon_title();
    {
        // An update starts, and the app restarts for it, only from the idle screen.
        const etude::RunPhase run = sections.run().phase();
        etude::UpdateActivity activity;
        activity.playing = songs.playing();
        activity.counting_in = songs.counting_down();
        activity.run_in_progress = run == etude::RunPhase::Playing || run == etude::RunPhase::CheckIn;
        about_port->set_busy(!etude::update_may_start(activity));
    }
    {
        const etude::app::FrameDetailScope loading(etude::app::FrameDetail::Library);
        if (library.abc_chords() != model.app.abc_chords) {
            // The setting changed: the ABC songs are parsed again to match what opens (the
            // worker is told to stop and poll() starts the next pass; nothing waits).
            library.set_abc_chords(model.app.abc_chords);
        }
        const bool changed = library.poll();
        if (changed) {
            lengths_stale = true;
            // A file of several tunes was replaced by them: the open song moved in the
            // list.
            if (open_song_ref) {
                song_index = library.index_of(open_song_ref->path, open_song_ref->tune);
            }
            if (pending_open) {
                pending_index = library.index_of(pending_open->song.path, pending_open->song.tune);
            }
        }
        // The phone page's list shows each song's length: made from the library's parsed entries,
        // and only while the phone upload is on (it costs a pass over the list).
        if (lengths_stale && upload_port->view().on) {
            lengths_stale = false;
            song_lengths.set(etude::app::song_lengths_of(library));
        }
        // A song being opened settles first: if it opens, the saved song's restore is
        // cancelled; if it fails, the restore goes on.
        if (saved_restore.active()) {
            // The saved song is being read: nothing to decide until it is in.
        } else if (restore_waiting && pending_open) {
            restore_deferred = true;
        } else if (restore_waiting && (restore_cancelled || changed || restore_deferred)) {
            restore_deferred = false;
            // Another song opened meanwhile leaves the saved song's loop and plan alone.
            const bool restored = !restore_cancelled;
            if (restore_step() && restored) {
                sections.restore(model.session, midi_clock_ns());
            }
        }
        for (std::string& notice : library.take_notices()) {
            pending_toast.set(std::move(notice), true);
        }
        // The library screen's catalogue catches up in a slice of this frame.
        const std::time_t wall = std::time(nullptr);
        library_view.set_today(
            etude::local_day_from_utc(static_cast<std::int64_t>(wall), local_utc_offset_s(wall)));
        library_view.set_utc_offset_s(local_utc_offset_s(wall));
        library_view.set_current(song_index);
        catalogues.set_online(model.app.get_songs_online);
        library_view.catalogue().set_group_versions(model.app.group_versions);
        library_view.set_keyboard(session.layout.range());
        library_view.update(changed, static_cast<std::int64_t>(wall));
    }
}

// The song and the sections move on, its sound goes out, the history and the runs are kept.
void WindowState::update_song(std::int64_t now) {
    songs.update(now, frame_events);
    frame_events.clear();
    // Wait Mode: the microphone checks for the chord the song waits on.
    mic.set_expected(songs.expected_keys());
    sections.update(now);
    // The run's summary stays until it is dismissed, the song plays or what it was about
    // changes (as the results card does).
    {
        const etude::RunPhase phase = sections.run().phase();
        if (phase == etude::RunPhase::Done && last_run_phase != etude::RunPhase::Done) {
            run_watch = watch_now();
        }
        if (phase == etude::RunPhase::Done && (songs.playing() || !(run_watch == watch_now()))) {
            sections.run_dismiss();
        }
        last_run_phase = phase;
    }
    transport.update(now);
    if (scripted) {
        // Scripted checks: say when the song's state changes, to line up with captures.
        const auto state = static_cast<int>(songs.state());
        if (state != logged_state || songs.has_song() != logged_song) {
            logged_state = state;
            logged_song = songs.has_song();
            std::printf("frame %ld, %.3f s: song %s, state %d, at %.2f s\n", frame_index,
                        static_cast<double>(now) / 1e9,
                        songs.has_song() ? songs.song().title.c_str() : "none", state,
                        songs.position_s());
        }
    }
    if (piano_out.connected() != song_to_midi) {
        if (song_to_midi) {
            midi_sender.submit({etude::SongSoundEvent::Kind::StopAll, {}});
        } else {
            builtin_song.all_off();
        }
        song_to_midi = piano_out.connected();
        songs.silence(now);
    }
    songs.take_sound_events(song_events);
    learn_sound.set_frame(now, session.layout.range());
    send_song(song_events);
    poll_history();
    // The Melodies tiles' best scores, once the screen has asked for them, and again after a run or a
    // change of profile.
    learn_sound.update_melody_progress(history.runs(), history.active_profile());
    practice_sync.set_accompaniment_default(model.app.accompaniment);
    practice_sync.update(midi_clock_ns());
    save_runs();
    update_replay(now);
}

// The developer's invented run and check-in, once the song is open.
void WindowState::show_demo_cards() {
    if (opt.drill_demo && !drill_demo_started && songs.has_song() && frame_index >= 3) {
        // The drill the card is about: G major, the right hand, paused, so the card comes up.
        drill_demo_started = true;
        etude::DrillSpec spec;
        spec.root_pc = 7;
        spec.hands = etude::HandsChoice::Right;
        start_drill(spec);
        songs.pause(midi_clock_ns());
    }
    if (opt.results_demo && !results_demo_shown && songs.has_song() && frame_index >= 5 &&
        !songs.playing()) {
        // An invented run: both hands, Flow Mode, six bars with misses.
        etude::RunRecord run;
        run.mode = etude::RunMode::Flow;
        run.hands = etude::HandFilter::Both;
        run.counts = {.perfect = 142, .good = 31, .early = 8, .late = 6, .missed = 27};
        run.wrong = 9;
        run.longest_combo = 38;
        run.left = {.graded = 90, .hit = 80};
        run.right = {.graded = 124, .hit = 108};
        run.all = {.graded = 214, .hit = 187};
        run.measures = {{3, 8, 7, 0},  {5, 8, 6, 1},  {8, 8, 5, 0},
                        {12, 8, 4, 2}, {14, 8, 3, 0}, {17, 8, 2, 0}};
        for (std::size_t i = static_cast<std::size_t>(opt.results_demo_trouble);
             i < run.measures.size(); ++i) {
            run.measures[i].missed = 0; // played clean
            run.measures[i].wrong = 0;
        }
        etude::AttemptChange change;
        change.has_previous = true;
        change.accuracy = 0.874;
        change.previous_accuracy = 0.81;
        change.trend = etude::Trend::Better;
        results_card = etude::make_results_card(
            run, change, true, songs.hands_available(), false, songs.song().song.measures,
            songs.song().difficulty.get(), model.app.weighted_score);
        results_card->serial = ++results_serial;
        if (drill) {
            results_card->drill_bpm = drill->bpm;
            results_card->drill_title = songs.song().title;
        }
        results_card->start_bars &= ~opt.results_demo_off;
        card_watch = watch_now();
        results_demo_shown = true;
    }
    if (!opt.run_demo.empty() && demo_run.phase() == etude::RunPhase::Idle && !run_demo_shown &&
        songs.has_song() && frame_index >= 5 && !songs.playing()) {
        // An invented run over three bars the player put in their own order: bar 8 first,
        // cleared on its third pass; then bar 3 and bar 12.
        const std::array<etude::TroubleBar, 3> demo_bars{
            {{8, 8, 0, 33}, {3, 3, 0, 20}, {12, 12, 0, 41}}};
        demo_run.start(demo_bars);
        // clean, graded, missed, wrong, then perfect, good, early, late, played, combo
        demo_run.on_pass({false, 8, 3, 1, 3, 1, 1, 0, 0, 2});
        demo_run.on_pass({false, 8, 2, 0, 4, 1, 0, 1, 0, 3});
        demo_run.on_pass({true, 8, 0, 0, 6, 1, 0, 1, 0, 8});
        if (opt.run_demo == "summary") {
            demo_run.command(etude::RunCommand::Next);
            demo_run.on_pass({false, 6, 2, 0, 3, 0, 1, 0, 0, 3});
            demo_run.command(etude::RunCommand::Skip); // gave bar 3 up
            demo_run.on_pass({false, 8, 4, 2, 2, 1, 0, 1, 0, 2});
            demo_run.command(etude::RunCommand::End); // ended on bar 12
        }
        run_demo_shown = true;
    }
    show_replay_demo();
}

// The results card goes when the song plays or what it was about changes; a paused song's state
// is kept in the session.
void WindowState::keep_session() {
    if (results_card && (songs.playing() || !(card_watch == watch_now()))) {
        results_card.reset();
    }
    if (!songs.playing() && !restore_waiting) {
        // Saved while paused; a playing song moves every frame. Built in a spare so the
        // strings in it are not allocated again each frame. Not while the saved song waits
        // for the library: the session still names it.
        session_scratch = model.session;
        songs.write_session_state(session_scratch);
        sections.save_into(session_scratch);
        if (!(session_scratch == model.session)) {
            model.session = session_scratch;
        }
    }
}

} // namespace etude::app
