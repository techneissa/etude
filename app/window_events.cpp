// SPDX-License-Identifier: GPL-3.0-or-later
// The events: SDL events and the shortcuts, the player's presses and the live redraw during a
// resize.
#include "etude/accidental_text.hpp"
#include "ear_session.hpp"
#include "window_state.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace etude::app {

// Called by SDL for every event, on the thread that produces it. Only the three that mean "the
// window has a new size or needs repainting", and only when the main loop cannot draw them
// itself soon (live_redraw_due): outside its frames, or while an OS loop holds its event pump.
bool SDLCALL on_window_event_watch(void* userdata, SDL_Event* event) {
    auto* hook = static_cast<RedrawHook*>(userdata);
    etude::app::WindowRedrawEvent kind = etude::app::WindowRedrawEvent::Exposed;
    switch (event->type) {
    case SDL_EVENT_WINDOW_EXPOSED:
        // SDL marks the expose its live-resize timer sends with data1 = 1.
        kind = event->window.data1 == 1 ? etude::app::WindowRedrawEvent::LiveExposed
                                        : etude::app::WindowRedrawEvent::Exposed;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        kind = etude::app::WindowRedrawEvent::Resized;
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        kind = etude::app::WindowRedrawEvent::PixelResized;
        break;
    default:
        return true;
    }
    if (event->window.windowID != hook->window || hook->redrawing || !SDL_IsMainThread()) {
        return true;
    }
    const std::int64_t now = etude::app::frame_clock_ns();
    if (etude::app::live_redraw_due(hook->state, kind, now, hook->frame_ns)) {
        hook->redrawing = true;
        hook->draw();
        hook->redrawing = false;
        hook->state.last_redraw_ns = etude::app::frame_clock_ns();
    }
    return true;
}

etude::SoundSink& WindowState::active_sink() {
    return piano_out.connected() ? static_cast<etude::SoundSink&>(piano_out)
                                 : static_cast<etude::SoundSink&>(builtin_sink);
}

void WindowState::send_song(std::vector<etude::SongSoundEvent>& events) {
    for (const etude::SongSoundEvent& e : events) {
        if (song_to_midi) {
            midi_sender.submit(e);
        } else {
            builtin_song.submit(e);
        }
    }
    events.clear();
}

// The card is on screen: the settings sheet and the section editor hide it, and a key that
// answers the card must not reach it while it is hidden.
bool WindowState::card_up() { return results_card.has_value() && !ui.panel_open(); }

// A trouble-bar run's check-in or summary is on screen (the settings sheet and the section
// editor hide it, and a key that answers it must not reach it then).
// A check-in or a summary exists (it may be hidden by the settings sheet or the editor).
bool WindowState::run_present() {
    const etude::RunPhase phase = shown_run().phase();
    return phase == etude::RunPhase::CheckIn || phase == etude::RunPhase::Done;
}

bool WindowState::run_card_up() {
    const etude::RunPhase phase = shown_run().phase();
    return !results_card.has_value() && !ui.panel_open() &&
           (phase == etude::RunPhase::CheckIn || phase == etude::RunPhase::Done);
}

// The shortcuts while the UI has none. None of them is a
// typing key, so playing on the computer keyboard never triggers one.
void WindowState::shortcut(const SDL_KeyboardEvent& key) {
    if (replay.viewing()) {
        if (!key.repeat || key.scancode == SDL_SCANCODE_LEFT || key.scancode == SDL_SCANCODE_RIGHT) {
            replay_key(key);
        }
        return;
    }
    const std::int64_t now = midi_clock_ns();
    // Auto-repeat acts only for skip (Left/Right, throttled below) and speed (Up/Down);
    // every other shortcut acts once per press.
    const bool repeats = key.scancode == SDL_SCANCODE_LEFT || key.scancode == SDL_SCANCODE_RIGHT ||
                         key.scancode == SDL_SCANCODE_UP || key.scancode == SDL_SCANCODE_DOWN;
    if (key.repeat && !repeats) {
        return;
    }
    switch (key.scancode) {
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (ui.card_ordering()) {
            ui.card_order_key(true); // Start, in the order shown
        } else if (run_card_up()) {
            // The check-in's primary button (Next bar), or the summary's Done.
            run_action(shown_run().phase() == etude::RunPhase::Done
                           ? etude::RunCardAction::Done
                           : etude::RunCardAction::Next);
        } else if (key.scancode != SDL_SCANCODE_SPACE) {
            // Return does nothing else.
        } else if (run_present() && !card_up()) {
            // The check-in is hidden (the settings sheet is over it): Space must not play
            // the song on behind it.
        } else if (card_up()) {
            card_action({etude::CardAction::Replay}); // the card's Replay
        } else {
            songs.toggle_play(now);
        }
        break;
    case SDL_SCANCODE_R:
        if (run_card_up() && shown_run().phase() == etude::RunPhase::CheckIn) {
            run_action(etude::RunCardAction::Again);
        }
        break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT: {
        // One step per tap (Settings: skip back / forward seconds); held, the key's
        // repeats step again, at most five times a second.
        if (!key.repeat || now - last_skip_ns >= 200'000'000) {
            if (key.scancode == SDL_SCANCODE_RIGHT) {
                songs.skip_forward(now);
            } else {
                songs.skip_back(now);
            }
            last_skip_ns = now;
        }
        break;
    }
    case SDL_SCANCODE_HOME:
        songs.replay(now);
        break;
    case SDL_SCANCODE_UP:
        songs.change_speed(etude::kSpeedStepPercent, now);
        break;
    case SDL_SCANCODE_DOWN:
        songs.change_speed(-etude::kSpeedStepPercent, now);
        break;
    case SDL_SCANCODE_LEFTBRACKET:
        songs.set_loop_start_here(now);
        break;
    case SDL_SCANCODE_RIGHTBRACKET:
        songs.set_loop_end_here(now);
        break;
    case SDL_SCANCODE_BACKSLASH:
        songs.clear_loop(now);
        break;
    default:
        shortcut_practice(key, now);
        break;
    }
}

// 1, 2 and 3 practise the right, left or both hands (with the results card up they choose those
// hands on it), M cycles the song sound, B the metronome, Q the mode, I the view.
void WindowState::shortcut_practice(const SDL_KeyboardEvent& key, std::int64_t now) {
    switch (key.scancode) {
    // 1, 2 and 3 practise the right, left or both hands; with the results card up they
    // choose those hands on it and start the run, as a tap on the card does.
    case SDL_SCANCODE_1:
    case SDL_SCANCODE_2:
    case SDL_SCANCODE_3: {
        const etude::HandFilter hands =
            key.scancode == SDL_SCANCODE_1   ? etude::HandFilter::Right
            : key.scancode == SDL_SCANCODE_2 ? etude::HandFilter::Left
                                             : etude::HandFilter::Both;
        switch (etude::card_hand_key(card_up() ? &*results_card : nullptr, hands)) {
        case etude::HandKey::Start:
            card_action({etude::CardAction::Hands, hands, 0});
            break;
        case etude::HandKey::Hint:
            show_message(etude::hand_hint(hands));
            break;
        case etude::HandKey::Pass:
            songs.set_practice_hands(hands, now);
            break;
        }
        break;
    }
    case SDL_SCANCODE_M:
        songs.cycle_song_sound(now);
        show_message(std::string("Song sound: ") +
                     etude::app::song_sound_name(songs.song_sound()));
        break;
    case SDL_SCANCODE_B:
        model.app.metronome = !model.app.metronome;
        show_message(model.app.metronome ? "Metronome on" : "Metronome off");
        break;
    case SDL_SCANCODE_Q:
        songs.toggle_mode(now);
        break;
    case SDL_SCANCODE_I: {
        // Falling notes, sheet music, both. A song with no sheet music says why and stays
        // on the falling notes.
        const etude::app::SheetOffer offer = sheet_view.offer();
        if (offer.song && offer.known && !offer.offered) {
            show_message("No sheet music: " + std::string(offer.reason));
            break;
        }
        model.app.view_mode = etude::next_view_mode(model.app.view_mode);
        show_message(etude::view_mode_label(model.app.view_mode));
        break;
    }
    default:
        shortcut_sections(key, now);
        break;
    }
}

// The sections, the markers and the plan, and the next and previous song.
void WindowState::shortcut_sections(const SDL_KeyboardEvent& key, std::int64_t now) {
    switch (key.scancode) {
    case SDL_SCANCODE_COMMA:
        // With a check-in or a summary up, or hidden under the sheet, the keys that drive
        // the run are for the visible card only: here they do nothing.
        if (!run_present()) {
            sections.step(-1, now); // the previous section, or the list's previous item
        } else if (run_card_up() && shown_run().phase() == etude::RunPhase::CheckIn) {
            sections.step(-1, now); // this bar again
        }
        break;
    case SDL_SCANCODE_PERIOD:
        if (!run_present()) {
            sections.step(1, now);
        } else if (run_card_up() && shown_run().phase() == etude::RunPhase::CheckIn) {
            run_action(etude::RunCardAction::Skip); // the next bar passed over
        }
        break;
    case SDL_SCANCODE_N:
        sections.add_marker_at_playhead(now);
        show_message(sections.take_message());
        break;
    case SDL_SCANCODE_SLASH:
        if (run_present() &&
            !(run_card_up() && shown_run().phase() == etude::RunPhase::CheckIn)) {
            break; // the summary, or a check-in the sheet hides: nothing to start or end
        }
        if (sections.view().plan_running) {
            sections.plan_stop(now);
        } else {
            sections.plan_start(now);
        }
        show_message(sections.take_message());
        break;
    case SDL_SCANCODE_PAGEDOWN:
    case SDL_SCANCODE_PAGEUP:
        if (ui.library_open()) {
            // A screen of cards; with the library shut, the next or previous song.
            ui.library_page(key.scancode == SDL_SCANCODE_PAGEDOWN ? 1 : -1);
        } else if (const auto next = library.step(pending_open ? pending_index : song_index,
                                                  key.scancode == SDL_SCANCODE_PAGEDOWN ? 1 : -1)) {
            request_song(library.path_at(*next), library.tune_at(*next));
        }
        break;
    default:
        break;
    }
}

// One event, from a MIDI port or from the player's own presses: keys, trail, chord,
// latency.
void WindowState::on_event(const MidiEvent& ev) {
    const KeyTransition t = apply_event(session, ev);
    trail.apply(t, ev.time_ns);
    if (t.type != KeyTransitionType::None) {
        last_activity_ns = ev.time_ns;
    }
    if (ear_takes_answer(ev)) {
        ui.learn_note_on(ev.data1); // the Learn screen's ear card takes it as an answer
    }
    if (t.type == KeyTransitionType::Pressed) {
        last_press[t.note] = ev.time_ns;
        last_velocity[t.note] = t.velocity;
        if (latency_log && ev.source != etude::InputSource::Microphone &&
            session.layout.range().contains(t.note)) {
            latency.note_on(ev.time_ns);
        }
    }
    if (ev.is_virtual()) {
        etude::forward_to_sink(active_sink(), ev);
    }
    frame_events.push_back(ev);
    if (opt.script.active() && ev.type == etude::MidiEventType::NoteOn) {
        std::printf("player note-on %d (source %d)\n", ev.data1, static_cast<int>(ev.source));
    }
}

// The player's own presses (stamped as the OS delivered them), then MIDI: into the frame's
// events and the song's evaluators. Run before each action on the song as well as at the
// frame's drain, so a press stamped before a seek, a skip or a song switch is in before
// it (a second call finds nothing new).
void WindowState::gather_presses() {
    for (const MidiEvent& ev : virtual_events) {
        on_event(ev);
    }
    virtual_events.clear();
    const etude::InputMode source = model.app.input_mode;
    midi->drain([&](const MidiEvent& ev, std::size_t slot) {
        if (opt.audio.latency_log) {
            audio.play_midi(ev);
        }
        if (source == etude::InputMode::Microphone) {
            return; // the microphone is the player; keyboards are ignored
        }
        if (ev.type == etude::MidiEventType::NoteOn && ev.data2 > 0 && !ev.synthetic) {
            device_check.note_on(slot, ev.data1); // a note the app takes, so "looks right" is true
        }
        if (source == etude::InputMode::Both && ev.type == etude::MidiEventType::NoteOn) {
            mic.note_midi_press(ev.data1, ev.time_ns);
        }
        on_event(ev);
    });
    // The microphone's notes, stamped at the onset, after the keyboard's (Both mode merges
    // an onset into a key the keyboard pressed first). Pulled here with the rest, so a note
    // the analysis thread had decided before an action is in before it: its onset is
    // older than the action, so it is judged under the state it was played under. A note
    // still being analysed when the action comes can't be (it is stamped before the action
    // and arrives after it, like a MIDI event the OS delivers late).
    mic_events.clear();
    unsure_new.clear();
    mic.poll(midi_clock_ns(), mic_events, unsure_new);
    for (const MidiEvent& ev : mic_events) {
        on_event(ev);
    }
    for (const etude::app::UnsureMark& m : unsure_new) {
        songs.input_unsure(m.onset_ns);
        if (unsure_marks.size() >= 8) {
            unsure_marks.erase(unsure_marks.begin());
        }
        unsure_marks.push_back(m);
    }
    device_check.drop_note_ons(frame_events,
                               ui.device_check_up()); // the card's notes only light their keys
    songs.take_in(frame_events);
}

void WindowState::handle_events() {
    before_events();
    poll_events();
    announce_typing_keys();
}

void WindowState::before_events() {
    // The dev flags' --resize F:max|full|restore stand for the OS's own buttons, whose
    // animation runs inside the call as it runs inside the pump for a click: the watch
    // may redraw there (live_redraw.hpp).
    hook.state.pumping = true;
    hook.state.pump_start_ns = etude::app::frame_clock_ns();
    if (opt.fake_clock_hz > 0) {
        static const std::int64_t base = etude::app::midi_clock_ns();
        etude::app::set_fake_clock_ns(base + frame_index * 1'000'000'000LL / opt.fake_clock_hz);
    }
    opt.script.before_events(window, frame_index);
    hook.state.pumping = false;
    opt.audit.before_frame(window, frame_index);
    take_dev_keyboards();
    take_simulated_loss();
}

void WindowState::poll_events() {
    // A window the renderer was made on again may send the window system's burst of events in the
    // frames after (a recreated window resizes and exposes itself): not the frame's own work.
    const etude::app::FrameDetailScope window_answers(etude::app::FrameDetail::Recovery,
                                                         frame_index - recovered_frame <= 3);
    // Inside SDL_PollEvent an OS loop may hold the pump (live_redraw.hpp); a resize
    // the dev flags asked for above is the frame's own, drawn after the events.
    const auto poll = [this](SDL_Event* e) {
        hook.state.pumping = true;
        hook.state.pump_start_ns = etude::app::frame_clock_ns();
        const bool got = SDL_PollEvent(e);
        hook.state.pumping = false;
        idle.note_input(got);
        return got;
    };
    SDL_Event event;
    while (poll(&event)) {
        const bool used_by_ui = route_event(event);
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE &&
                   !used_by_ui) {
            escape_key(event);
        }
    }
}

// One event to the UI, the sound, the typing keys, the score and the shortcuts. Returns whether
// the UI used it.
bool WindowState::route_event(const SDL_Event& event) {
    const bool used_by_ui = ui.process_event(event, midi_clock_ns());
    audio.handle_event(event);
    if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED ||
        event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) {
        mic.devices_changed();
    }
    const bool playing_key = virtual_input.handle(event, window, used_by_ui, virtual_events);
    sheet_view.handle_event(event, window, midi_clock_ns(), ui.fingers_down() > 1);
    if (event.type == SDL_EVENT_KEY_DOWN && !playing_key && !used_by_ui &&
        (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0) {
        shortcut(event.key);
    }
    note_render_event(event);
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        songs.lost_focus(midi_clock_ns()); // nothing of the song keeps sounding
        if (scripted) {
            std::printf("frame %ld: focus lost\n", frame_index);
        }
    }
    return used_by_ui;
}

// Escape closes the profile switcher's sheet or panel first, then the settings panel, then the
// results card, then what the bar or the section band opened, then the device check, then the
// octave-shift offer, then quits.
void WindowState::escape_key(const SDL_Event& event) {
    if (ui.close_profile()) {
        // The profile panel or its sheet closed.
    } else if (ui.panel_open()) {
        ui.close_panel();
    } else if (replay.viewing()) {
        // Back to where the replay was opened from.
        if (!event.key.repeat) {
            replay.close_view();
        }
    } else if (ui.card_ordering()) {
        // Back to the card from the order of the bars.
        if (!event.key.repeat) {
            ui.card_order_key(false);
        }
    } else if (run_card_up()) {
        // End at a check-in, Done on the summary.
        if (!event.key.repeat &&
            (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0) {
            run_action(shown_run().phase() == etude::RunPhase::Done
                           ? etude::RunCardAction::Done
                           : etude::RunCardAction::End);
        }
    } else if (card_up()) {
        // Done, from a plain first press: not a repeat, not with Ctrl, Alt or Cmd.
        if (!event.key.repeat &&
            (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0) {
            card_action({etude::CardAction::Done});
        }
    } else if (ui.close_band_overlay()) {
        // A card or popover the bar or the band opened is closed.
    } else if (ui.device_check_up()) {
        answer_device_check(etude::app::ui::DeviceCheckChoice::Skip);
    } else if (ui.offer_card_shown()) {
        songs.dismiss_shift_offer(); // hidden until the song or keyboard is new
    } else {
        running = false;
    }
}

void WindowState::announce_typing_keys() {
    // Say so when the typing keys' octave or loudness changed.
    const etude::ComputerKeyboard& typing = virtual_input.typing();
    if (typing.octave_shift() != shown_octave_shift || typing.velocity() != shown_velocity) {
        shown_octave_shift = typing.octave_shift();
        shown_velocity = typing.velocity();
        show_message("Typing keys: A plays " +
                     etude::accidental_display(etude::note_name(typing.base_note(), false,
                                                                    model.look.octave_offset())) +
                     ", loudness " + std::to_string(typing.velocity()));
    }
}

} // namespace etude::app
