// SPDX-License-Identifier: GPL-3.0-or-later
// The frame, second half: lays the frame out, draws the scene and the UI, and presents it.
#include "window_state.hpp"

#include "update_start.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace etude::app {
namespace {

// The bar fades while keys are held or were pressed this recently.
constexpr std::int64_t kPlayingHoldNs = 3'000'000'000;

} // namespace

void WindowState::settle_look(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    // Black keys are spelled by the key at the playhead when the look says so: the scene
    // and the score get the look with that settled, so a key change that moves between
    // flats and sharps is the one moment the names are made again.
    const etude::KeyChange* played_key =
        songs.has_song() ? etude::key_at_seconds(songs.song().song.keys, songs.position_s())
                         : nullptr;
    ctx.shown_look = etude::with_spelling_for_key(
        model.look, etude::key_spells_flats(played_key, songs.transposition().semitones()));
    const etude::LookSettings& shown_look = ctx.shown_look;
    if (!(shown_look == applied_look)) {
        scene.set_settings(shown_look);
        applied_look = shown_look;
    }
    // The scale overlay is free play's: it waits out a song and is back when the song is closed.
    const etude::ScaleOverlay wanted_scale{model.app.scale_overlay && !songs.has_song(),
                                             model.app.scale_root, model.app.scale_type,
                                             model.app.scale_fingers};
    if (!(wanted_scale == applied_scale)) {
        applied_scale = wanted_scale;
        scale_mask = etude::make_scale_overlay(wanted_scale);
    }
    scene.set_scale_overlay(scale_mask.on ? &scale_mask : nullptr);
    const std::int64_t trail_span_ns = scene.trail_span_ns();
    trail.prune(now, trail_span_ns);
    trail.visible(now, trail_span_ns, bars);
    session.keys.held_notes(held);
    // The hands' chord labels (#272): named on a worker from the notes' hands, the
    // transposition and the spelling, again when one of them changes.
    chord_feed.update({songs.has_song() && model.look.chord_label, songs.load_serial(),
                       songs.notes_serial(), songs.transposition().semitones(),
                       model.look.names_spelling == etude::NameSpelling::Flats},
                      [&] {
                          etude::app::HandChordsRequest request;
                          const etude::Song& played = songs.song().song;
                          request.notes = songs.note_table();
                          request.measures = songs.measure_table();
                          request.tempo = played.tempo;
                          request.keys = played.keys;
                          request.end_tick = played.end_tick;
                          request.duration_s = played.duration_s;
                          request.semitones = songs.transposition().semitones();
                          request.prefer_flats =
                              model.look.names_spelling == etude::NameSpelling::Flats;
                          return request;
                      });
}

void WindowState::update_status_lines() {
    out_of_range_line[0] = '\0';
    if (session.out_of_range > 0) {
        std::snprintf(out_of_range_line, sizeof out_of_range_line,
                      "%llu %s outside the keyboard's range",
                      static_cast<unsigned long long>(session.out_of_range),
                      session.out_of_range == 1 ? "note" : "notes");
    }
    dropped_line[0] = '\0';
    if (const std::uint64_t dropped = midi->dropped(); dropped > 0) {
        std::snprintf(dropped_line, sizeof dropped_line, "%llu MIDI events dropped",
                      static_cast<unsigned long long>(dropped));
    }
}

void WindowState::place_ui(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    // The top row, the bar and the settings go on top of the finished scene.
    int& out_w = ctx.out_w;
    int& out_h = ctx.out_h;
    gfx::output_size(renderer, &out_w, &out_h);
    etude::app::UiFrameInfo& info = ctx.info;
    info.width_px = static_cast<float>(out_w);
    info.height_px = static_cast<float>(out_h);
    {
        // Pixels per ImGui point, the way the UI host derives it (output size over window size).
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(window, &win_w, &win_h);
        info.pixel_density =
            win_w > 0 ? static_cast<float>(out_w) / static_cast<float>(win_w) : 1.0f;
    }
    // Real millimetres: the panel's size (read once), the Settings override, and the UI scale on
    // top. The scene lays itself out with the same value, so its keyboard and the UI's rows agree.
    info.px_per_mm = panel.px_per_mm(info.width_px, info.height_px, info.pixel_density,
                                     model.app.screen_width_mm, opt.audit.ui_scale());
    info.real_px_per_mm = panel.px_per_mm(info.width_px, info.height_px, info.pixel_density,
                                          model.app.screen_width_mm, 1.0f);
    etude::app::set_display_px_per_mm(info.px_per_mm);
    scene.set_px_per_mm(info.px_per_mm);
    // The replay's bar is the pinned strip with its band under it, whatever the bar settings say.
    const etude::PinnedBar pinned_bar =
        replay.viewing() ? etude::PinnedBar{true, etude::BarStyle::Strip, songs.has_song(), false}
                         : etude::PinnedBar{model.app.bar_pinned, model.app.bar_style,
                                              songs.has_song(), ui.lane_wanted()};
    scene.set_pinned_bar(pinned_bar);
    info.ui_grow = ui_grow_hold.grow(info.width_px, info.height_px, info.px_per_mm, now);
    scene.set_ui_grow(info.ui_grow);
    {
        const etude::RectF card = ui.offer_card_rect();
        scene.set_combo_keep_out({card.x * info.pixel_density, card.y * info.pixel_density,
                                  card.w * info.pixel_density, card.h * info.pixel_density});
    }
    scene.set_combo_clear_y(ui.hanging_band_bottom() * info.pixel_density);
    scene.set_ui_unit(etude::ui_scale(info.width_px, info.height_px, info.px_per_mm,
                                        info.pixel_density, info.ui_grow));
    info.layout = etude::compute_layout(info.width_px, info.height_px, info.px_per_mm, model.look,
                                          pinned_bar, info.pixel_density, info.ui_grow);
    info.now_ns = now;
}

void WindowState::engrave_sheet(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    etude::app::UiFrameInfo& info = ctx.info;
    const etude::LookSettings& shown_look = ctx.shown_look;
    // Sheet music: engraved for the view chosen (the Sheet view's while the falling notes
    // show, so a switch has its pictures ready) and shown unless the song has none.
    sheet_view.sync_song(songs, now);
    sheet_view.set_helps(shown_look, info.px_per_mm);
    const etude::app::SheetOffer sheet_offer = sheet_view.offer();
    // The replay is drawn over the falling notes: no sheet music while it is up.
    const etude::ViewMode wanted_view =
        replay.viewing() ? etude::ViewMode::Falling : model.app.view_mode;
    const bool sheet_refused = sheet_offer.known && !sheet_offer.offered;
    const etude::RectF score_room = etude::score_area(info.layout);
    // Room beside the paper for the three side buttons: a cell each, a hit area and the dead gap.
    const float sheet_corner_px = etude::app::ui::tok::kCell *
                                  etude::ui_scale(info.width_px, info.height_px, info.px_per_mm,
                                                    info.pixel_density, info.ui_grow);
    const etude::SheetPlan engrave_plan = etude::plan_sheet(
        score_room,
        wanted_view == etude::ViewMode::Falling ? etude::ViewMode::Sheet : wanted_view,
        songs.has_song(), sheet_view.two_staves(), info.px_per_mm, sheet_corner_px,
        sheet_view.name_room(info.px_per_mm));
    const etude::SheetPlan sheet_plan =
        etude::plan_sheet(score_room, sheet_refused ? etude::ViewMode::Falling : wanted_view,
                            songs.has_song(), sheet_view.two_staves(), info.px_per_mm,
                            sheet_corner_px, sheet_view.name_room(info.px_per_mm));
    sheet_view.update(songs, engrave_plan, sheet_plan, now, model.look.reduced_effects);
    {
        // The score's gestures: a double tap starts there (Wait Mode: waits on that chord;
        // a start outside the loop ends it, as the scrubber's does), a long press dragged
        // across bars sets the loop.
        etude::app::SheetRequest req;
        while (sheet_view.take_request(req)) {
            const etude::TempoMap& tempo = songs.song().song.tempo;
            if (req.kind == etude::app::SheetRequest::Kind::Start) {
                const std::int64_t tick =
                    songs.mode() == etude::PracticeMode::Wait ? req.onset_tick : req.beat_tick;
                songs.seek(tempo.seconds_at(tick), now);
                if (!songs.playing()) {
                    songs.play(now);
                }
            } else if (req.kind == etude::app::SheetRequest::Kind::Loop) {
                const double start = tempo.seconds_at(req.start_tick);
                if (songs.set_loop(start, tempo.seconds_at(req.end_tick), now)) {
                    songs.seek(start, now);
                }
            }
        }
    }
    ctx.sheet_offer = sheet_offer;
    ctx.wanted_view = wanted_view;
    ctx.sheet_refused = sheet_refused;
    ctx.sheet_plan = sheet_plan;
}

void WindowState::fit_to_sheet(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    etude::app::UiFrameInfo& info = ctx.info;
    const etude::SheetPlan& sheet_plan = ctx.sheet_plan;
    const etude::app::SheetOffer& sheet_offer = ctx.sheet_offer;
    const etude::ViewMode wanted_view = ctx.wanted_view;
    const bool sheet_refused = ctx.sheet_refused;
    {
        // The falling notes keep to the note area, below the score when it shows.
        etude::RectF falling = info.layout.note_area;
        if (sheet_plan.shown) {
            const float top = std::max(falling.y, sheet_plan.notes.y);
            falling.h = std::max(0.0f, falling.bottom() - top);
            falling.y = top;
        }
        scene.set_falling_area(falling, !sheet_plan.shown);
        // A count-in or the resume countdown shows in a band across the top of the
        // falling notes, which hold still meanwhile (count_band.hpp); it eases in and out
        // over a sheet's time (at once with reduced effects). With no falling notes on
        // screen (sheet music alone) it stays in the top row.
        const bool counting = songs.count_in_label() > 0 || songs.counting_down();
        const float step = model.look.reduced_effects
                               ? 1.0f
                               : static_cast<float>(etude::app::capped_s(now - count_band_ns)) /
                                     etude::app::ui::tok::kSheet;
        count_band_ns = now;
        count_band_shown = std::clamp(count_band_shown + (counting ? step : -step), 0.0f, 1.0f);
        if (counting) {
            // The UI's scale as the top row draws it, held while a resize goes on, and the
            // number below the toasts' slot (points to pixels), so the two never meet.
            const etude::RectF slot = ui.toast_slot();
            count_band = etude::place_count_band(
                falling,
                etude::ui_scale(info.width_px, info.height_px, info.px_per_mm, info.pixel_density,
                                  info.ui_grow),
                slot.empty() ? 0.0f : slot.bottom() * info.pixel_density);
        }
        if (!count_band.visible()) {
            count_band_shown = 0.0f;
        }
        scene.set_count_band(count_band.band.h, count_band_shown);
        info.count_band = count_band;
        info.count_band_shown = count_band_shown;
    }
    if (wanted_view != etude::ViewMode::Falling && sheet_offer.song && sheet_refused &&
        sheet_refusal_told != songs.load_serial()) {
        sheet_refusal_told = songs.load_serial();
        show_message("No sheet music for this song: " + std::string(sheet_offer.reason));
    }
    info.sheet = {sheet_offer.song, sheet_offer.known, sheet_offer.offered, sheet_offer.reason};
    info.sheet_corner = sheet_plan.shown ? sheet_plan.corner : etude::RectF{};
    info.sheet_names = sheet_plan.shown ? sheet_plan.side.names : etude::RectF{};
    info.sheet_colours = sheet_plan.shown ? sheet_plan.side.colours : etude::RectF{};
    // The sheet-music button stands at the top right of the score's room in every view, the
    // falling notes included; the combo number, the key label and the chord names keep clear of it.
    const etude::RectF score_room = etude::score_area(info.layout);
    const float side_cell_px = etude::app::ui::tok::kCell *
                               etude::ui_scale(info.width_px, info.height_px, info.px_per_mm,
                                                 info.pixel_density, info.ui_grow);
    info.sheet_button = songs.has_song() ? etude::sheet_side_buttons(score_room, side_cell_px,
                                                                       score_room.bottom())
                                                   .view
                                         : etude::RectF{};
    scene.set_side_button(info.sheet_button);
    info.playing =
        session.keys.held_count() > 0 || now - last_activity_ns < kPlayingHoldNs || songs.playing();
}

void WindowState::gather_song_notes(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    etude::app::UiFrameInfo& info = ctx.info;
    // The song's notes on screen, in their hands' colours; the hands not practised faint.
    song_bars.clear();
    grade_popups.clear();
    hint_hands.fill(0);
    wrong_flash.fill(0.0f);
    press_look.fill(etude::app::kPressFine);
    replay_marks.clear();
    if (replay.viewing() && songs.has_song()) {
        gather_replay_notes();
    } else if (songs.has_song()) {
        for (int key = 0; key < 128; ++key) {
            press_look[static_cast<std::size_t>(key)] =
                songs.press_pending(key)       ? etude::app::kPressPending
                : songs.press_looks_wrong(key) ? etude::app::kPressWrong
                                               : etude::app::kPressFine;
        }
        songs.visible_notes(model.look.visible_seconds, visible_notes);
        const auto& notes = songs.song().song.notes;
        const etude::HandFilter practised = songs.applied_hands();
        for (const etude::VisibleNote& v : visible_notes) {
            const etude::Note& n = notes[v.index];
            if (!v.playable || n.drum) {
                continue;
            }
            const bool mine =
                !n.accompaniment &&
                (practised == etude::HandFilter::Both ||
                 (practised == etude::HandFilter::Left && n.hand == etude::Hand::Left) ||
                 (practised == etude::HandFilter::Right && n.hand == etude::Hand::Right));
            const int grade = songs.grade_of(v.index);
            // Lit while the key held for it is down, by the grading's idea of the hit.
            const bool struck =
                mine && songs.note_struck(v.pitch, session.keys.is_down(v.pitch), n.start_s, grade);
            song_bars.push_back({static_cast<std::uint8_t>(v.pitch), n.velocity, n.hand,
                                 static_cast<float>(v.bottom), static_cast<float>(v.top), mine,
                                 static_cast<std::int8_t>(grade),
                                 static_cast<std::int8_t>(v.folded ? v.fold_octaves : 0), struck,
                                 static_cast<std::int8_t>(drill ? n.finger : 0)});
        }
        for (const etude::app::WrongMark& w : songs.recent_wrongs()) {
            etude::add_wrong_flash(wrong_flash, w.key, now - w.at_ns,
                                     !model.look.reduced_effects);
        }
        if (songs.mode() == etude::PracticeMode::Flow) {
            for (const etude::app::GradeMark& g : songs.recent_grades()) {
                grade_popups.push_back({static_cast<std::uint8_t>(g.key & 127), g.grade,
                                        static_cast<float>(now - g.at_ns) /
                                            static_cast<float>(etude::app::kGradeShowNs)});
            }
        }
        for (const auto& k : songs.waiting_keys()) {
            hint_hands[static_cast<std::size_t>(k.key & 127)] =
                static_cast<std::uint8_t>(static_cast<int>(k.hand) + 1);
        }
    }
    info.status_label = session.label;
    if (model.app.input_mode == etude::InputMode::Microphone) {
        mic_label = "Microphone";
        info.status_label = mic_label;
    } else if (model.app.input_mode == etude::InputMode::Both) {
        mic_label.assign(session.label);
        mic_label += " + microphone";
        info.status_label = mic_label;
    }
}

void WindowState::follow_key_view(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    etude::app::UiFrameInfo& info = ctx.info;
    const int out_w = ctx.out_w;
    // The window on the keyboard. A resize, a new key size or a different keyboard jumps
    // straight there (the layout must be right in the very frame); a new song's range
    // slides there. The notes being played never move it.
    etude::KeyViewParams view_params;
    view_params.keyboard = session.layout.range();
    view_params.width_px = info.width_px;
    view_params.px_per_mm = info.px_per_mm;
    view_params.size = model.look.key_size;
    view_params.song = songs.key_range();
    if (!songs.has_song()) {
        // Free play: a key held off the view brings it there (compute_key_view).
        held_notes.assign(held.begin(), held.end());
        view_params.held = held_notes;
        view_params.current = view_range;
    }
    const etude::KeyViewTarget view_target = etude::compute_key_view(view_params);
    view_range = view_target.range;
    const ViewInputs inputs{out_w, model.look.key_size, session.layout.range().lowest,
                            session.layout.range().highest, info.px_per_mm};
    if (!view_follower.has_position() || !(inputs == view_inputs)) {
        view_follower.snap(session.layout.range(), view_target.range);
    } else {
        view_follower.advance(session.layout.range(), view_target.range,
                              etude::app::capped_s(now - view_ns));
    }
    view_inputs = inputs;
    view_ns = now;
    view_frame = view_follower.frame();
    if (!view_layout || view_layout->range().lowest != view_frame.layout_range.lowest ||
        view_layout->range().highest != view_frame.layout_range.highest ||
        view_layout->blocks() != model.look.block_layout) {
        view_layout.emplace(view_frame.layout_range, model.look.block_layout);
    }

    const bool show_typing = model.look.show_typing_keys;
    if (show_typing) {
        for (int note = 0; note < 128; ++note) {
            typing_labels[static_cast<std::size_t>(note)] =
                virtual_input.typing().label_for_note(note);
        }
    }
    // The settings sheet covers everything under the top row: no key presses through it.
    virtual_input.set_geometry(*view_layout, view_frame, info.layout.key_area,
                               ui.panel_open() ? 0.0f : -1.0f, info.px_per_mm);
}

void WindowState::fill_status(FrameCtx& ctx) {
    etude::app::UiFrameInfo& info = ctx.info;
    if (opt.debug_effects) {
        std::snprintf(effects_line, sizeof effects_line, "effects: %s",
                      std::string(etude::effects_level_name(scene.effects_level())).c_str());
    }
    // The notes after the keyboard's name, each with the rank a narrow row drops it by
    // (status_parts.hpp). The song's line is one group of parts, or the words of an
    // opening song in its place.
    using etude::app::StatusRank;
    info.status_parts = {};
    info.status_parts.add(sections.view().plan_status, StatusRank::Plan, 0);
    if (opening_status.empty()) {
        songs.add_status_parts(info.status_parts, 1);
    } else {
        info.status_parts.add(opening_status, StatusRank::Title, 1);
    }
    info.status_parts.add(dropped_line, StatusRank::Dropped, 2);
    info.status_parts.add(effects_line, StatusRank::Effects, 3);
    info.status_parts.add(out_of_range_line, StatusRank::OutOfRange, 4);
    // The keyboard prompt is for free play: a loaded song, and the countdown ring that
    // sits in the middle of the note area, take its place, and so do the device check and the
    // crash notice (the cards over the notes, which nothing shows through).
    ctx.keyboard_prompt = midi->ready() && session.open_inputs == 0 && !songs.has_song() &&
                          !songs.counting_down() && !device_check.open() && crash.view() == nullptr &&
                          model.app.input_mode == etude::InputMode::Midi;
}

void WindowState::draw_scene(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    const bool live = ctx.live;
    const etude::SheetPlan& sheet_plan = ctx.sheet_plan;
    const bool keyboard_prompt = ctx.keyboard_prompt;
    const bool show_typing = model.look.show_typing_keys;
    etude::app::FrameInput frame{
        *view_layout, view_frame,      session.keys,
        bars,         last_press,      last_velocity,
        now,          keyboard_prompt, show_typing ? &typing_labels : nullptr};
    frame.song_bars = song_bars;
    frame.chords = chord_feed.chords();
    frame.chords_serial = chord_feed.serial();
    // A replay has a clock of its own: the song's notes, chords and key stand where it is.
    const double shown_s = replay.viewing() ? static_cast<double>(replay.position_ms()) / 1000.0
                                            : songs.position_s();
    frame.chords_time_s = shown_s;
    frame.chords_transposition = songs.transposition();
    if (songs.has_song()) {
        frame.song = &songs.song().song;
        frame.song_now_s = replay.viewing() ? shown_s : songs.display_position_s();
    }
    frame.replay_marks = replay_marks;
    frame.replay_keys = replay.viewing() ? &replay.lit_keys() : nullptr;
    frame.grades = grade_popups;
    // The combo on screen (#88): its count, and the milestone this frame crossed. A break
    // is the count falling, which the number fades out on its own.
    frame.combo = opt.combo_fixed >= 0 ? opt.combo_fixed : songs.game_feel().combo();
    frame.combo_milestone = songs.take_combo_milestone().kind != etude::ComboEventKind::None;
    frame.combo_epoch = songs.load_serial();
    songs.take_combo_break();
    unsure_draw.clear();
    std::erase_if(unsure_marks, [now](const etude::app::UnsureMark& m) {
        return now - m.at_ns > etude::kUnsureHoldNs + etude::kUnsureFadeNs;
    });
    for (const etude::app::UnsureMark& m : unsure_marks) {
        unsure_draw.push_back({static_cast<std::uint8_t>(etude::unsure_marker_note(
                                   m.guess, m.last_heard, session.layout.range())),
                               etude::unsure_marker_alpha(now - m.at_ns)});
    }
    frame.unsure = unsure_draw;
    frame.hint_hands = songs.waiting() ? &hint_hands : nullptr;
    // A song open hides the player's rising blocks unless the look asks for them (faint).
    frame.trail_alpha = etude::rising_alpha(songs.has_song(), model.look.rising_in_song);
    frame.wrong_flash = &wrong_flash;
    frame.press_look = &press_look;
    if (songs.has_song()) {
        // The key at the playhead, as played (the transposition moves it).
        frame.key_label.key = etude::key_at_seconds(songs.song().song.keys, shown_s);
        frame.key_label.transpose = songs.transposition().semitones();
    }
    log_mark(etude::app::FramePhase::Update, live);
    // No budget for names while a script takes screenshots or the icon audit measures
    // pixels: those frames must be the exact ones.
    etude::app::Scene::begin_frame(!opt.script.active() && !opt.audit.active());
    scene.draw(frame);
    sheet_view.draw(sheet_plan);
    log_mark(etude::app::FramePhase::Scene, live);
}

void WindowState::draw_ui(FrameCtx& ctx) {
    const std::int64_t now = ctx.now;
    const bool live = ctx.live;
    etude::app::UiFrameInfo& info = ctx.info;
    if (!pending_toast.empty()) {
        ui.show_toast(pending_toast.text, pending_toast.warning, pending_toast.name,
                      pending_toast.lines());
        pending_toast.clear();
    }
    info.key_label = scene.key_label_rect();
    info.combo = scene.combo_rect();
    info.chord_labels = scene.chord_label_rects();
    info.results = results_card ? &*results_card : nullptr;
    info.run = results_card ? nullptr : &shown_run();
    info.order_demo = opt.order_demo;
    info.device_check = device_check.open() ? &device_check.check() : nullptr;
    info.octave_offset = model.look.octave_offset();
    info.crash = crash.view();
    fill_replay_info(info);
    const etude::app::ProfileInfo profiles = profile_info(ui.profile_open());
    info.profiles = &profiles;
    ui.draw(model, transport, sections, mic, info);
    if (icon_service.take_medals()) {
        ui.open_medals(-1);
    }
    const ProfileChoice profile_pick = ui.take_profile_choice();
    if (profile_pick.action == ProfileAction::Progress) {
        ui.open_progress(false);
    }
    profile_choice(profile_pick);
    answer_device_check(ui.take_device_check_choice());
    crash.answer(ui.take_crash_choice(), [](const std::string& url) { return SDL_OpenURL(url.c_str()); });
    track_crash_state();
    card_action(ui.take_results_choice());
    if (std::exchange(drill_card_asked, false) && drill) {
        ui.open_learn_panel(drill->chord ? PanelTab::LearnChords : PanelTab::LearnScales);
    }
    run_action(ui.take_run_choice());
    review_action(ui.take_review_choice());
    replay_action(ui.take_replay_choice());
    if (const auto chosen = library_view.take_open(); chosen && *chosen < library.size()) {
        // A song chosen in the library is read off the frame thread like any other; the
        // library stays up (the top row says "Opening ..." if it takes a while) and
        // closes when the song is in. It opens paused: loading clears the resume the
        // library's opening asked for.
        request_song(library.path_at(*chosen), library.tune_at(*chosen),
                     library_view.take_section_open());
        pending_bars = library_view.take_bars_open();
        library_opening = true;
    }
    // The library's preview with its sound on plays on the piano the player hears.
    for (const auto& [key, velocity] : library_view.preview_sound()) {
        if (velocity > 0) {
            active_sink().note_on(0, key, velocity);
        } else {
            active_sink().note_off(0, key);
        }
    }
    library_view.clear_preview_sound();
    log_mark(etude::app::FramePhase::Ui, live);
    {
        const etude::app::FrameDetailScope saving(etude::app::FrameDetail::Save);
        const std::uint64_t save_allocs = etude::app::allocations_counted();
        show_message(persist.update(to_save(), now), true);
        if (const std::uint64_t made = etude::app::allocations_counted() - save_allocs;
            opt.count_allocs && made > 0) {
            std::printf("frame %lld: settings save made %llu allocations\n",
                        static_cast<long long>(frame_index), static_cast<unsigned long long>(made));
        }
    }
}

void WindowState::after_draw(bool live) {
    // A live redraw from inside the OS's resize loop is not one of the loop's frames: the
    // scripted steps, shots and audits keyed to frame numbers wait for the next real one.
    if (!live) {
        if (opt.script.after_draw(renderer, frame_index) ||
            opt.audit.after_draw(renderer, frame_index)) {
            running = false;
        }
        ++frame_index;
    }
}

void WindowState::present(bool live) {
    log_mark(etude::app::FramePhase::Tail, live);
    // Headless presents nothing, which cannot fail (only the developer's pretend failures count).
    bool shown = true;
    if (opt.headless) {
        gfx::flush(renderer);
    } else {
        shown = gfx::present(renderer);
    }
    if (sim_present_failures > 0) {
        --sim_present_failures;
        shown = false;
    }
    // A present that keeps failing is a lost device, unless the window is hidden or minimised
    // (which may refuse one).
    recovery.note_present(shown, opt.headless || (SDL_GetWindowFlags(window) &
                                                  (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0);
    if (opt.startup_log && !live && frame_index == 1) {
        std::printf("first frame after %.1f ms (%zu songs listed)\n",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              g_process_start)
                        .count(),
                    library.size());
        std::printf("history at the first frame: open() took %.3f ms on the frame "
                    "thread, %s, polls took at most %.0f us\n",
                    history_open_ms, history.loading() ? "still loading" : "loaded",
                    history_poll_max_us);
        std::fflush(stdout);
    }
    if (shown && !live && !opt.headless) {
        // A freshly installed version tells the update helper it is ready (once): its first frame is
        // on screen and its history has opened and migrated.
        about_port->startup_progress(true, history_open_for(history.state()));
    }
    log_mark(etude::app::FramePhase::Present, live);
}

void WindowState::govern_frame(std::int64_t work_start_ns, bool live) {
    const std::int64_t presented_ns = midi_clock_ns();
    if (latency_log) {
        latency.presented(presented_ns);
    }
    mic.log_presented(presented_ns, true);
    if (!live) {
        const std::int64_t gap = idle.first_after_wait() ? 0 : presented_ns - last_present_ns;
        const std::int64_t cost =
            etude::frame_cost_ns(presented_ns - work_start_ns, gap, frame_ns);
        governor.set_shader_level(scene.has_shader_effect());
        scene.set_effects_level(gap == 0 ? governor.add_frame_after_gap(cost)
                                         : governor.add_frame(cost, gap));
        idle.end_frame(presented_ns);
    }
    last_present_ns = presented_ns;
    if (log_frames && !live) {
        frame_log_end_ns = etude::app::frame_clock_ns();
        frame_log.finish(frame_log_end_ns, frame_index);
        if (opt.count_allocs && frame_index == kAllocBaseFrame) {
            alloc_base_ours = etude::app::allocations_counted();
            alloc_base_system = etude::app::system_allocations_counted();
            alloc_base_frame = frame_index;
        }
    }
}

} // namespace etude::app
