// SPDX-License-Identifier: GPL-3.0-or-later
// The run's review and replay in the window (#78): the "How it went" page's watch and practise
// choices, the replay view's bar and keys, its own clock and sound, and the song's notes as the
// replay shows them. The session itself is replay_session.hpp; the song's transport stays as it was.
#include "window_state.hpp"

#include "replay_demo.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace etude::app {

namespace {

// A replay opens this far before a bar it is asked to show, so the notes fall in.
constexpr std::int32_t kBarLeadMs = etude::kMistakeLeadMs;

} // namespace

// The summary page's chips and the replay's practise tool: the worst bars (`measure` 0) as a run
// with a check-in after each, or one bar looped.
void WindowState::practise_review(int measure) {
    const etude::RunReplay* data = replay.data();
    if (data == nullptr || !songs.has_song()) {
        return;
    }
    const std::int64_t now = midi_clock_ns();
    const etude::Song& song = songs.song().song;
    const etude::RunSummary& summary = data->summary;
    etude::BarPractice practice;
    if (measure > 0) {
        const std::array<int, 1> one{measure};
        practice = etude::plan_bar_practice(one, song.measures, song.duration_s,
                                              etude::BarPracticeKind::Together, false);
    } else {
        practice = etude::plan_summary_practice(summary, song.measures, song.duration_s,
                                                  etude::BarPracticeKind::OneByOne);
    }
    const etude::TroubleBars run_bars = etude::trouble_bars(
        std::span<const etude::CardBar>(summary.bars.data(),
                                          static_cast<std::size_t>(summary.bar_count)),
        practice);
    if (sections.practise_bars(practice, now, measure > 0 ? nullptr : &run_bars)) {
        replay.close_view();
        results_card.reset();
        replay.forget_review();
        ui.close_summary();
    } else {
        show_message(sections.take_message());
    }
}

// Opens the replay: from its start (`measure` 0) or a little before a bar, and plays.
void WindowState::watch_review(int measure) {
    std::int32_t at = 0;
    if (measure > 0 && songs.has_song()) {
        for (const etude::Measure& m : songs.song().song.measures) {
            if (m.number == measure) {
                at = std::max<std::int32_t>(0, static_cast<std::int32_t>(m.start_s * 1000.0) -
                                                   kBarLeadMs);
                break;
            }
        }
    }
    if (!replay.open_view(at)) {
        show_message("Nothing to watch: this run's notes were not kept");
        return;
    }
    replay.play_pause();
    replay_ns = midi_clock_ns();
}

// The pin chip: keep this run's replay, or let go of it. A run with no notes kept says why it
// cannot.
void WindowState::pin_review() {
    if (!replay.pin_available()) {
        show_message("Nothing to pin: no notes were kept for this run");
        return;
    }
    replay.toggle_pin(history);
}

void WindowState::review_action(const etude::app::ReviewChoice& choice) {
    switch (choice.action) {
    case ReviewAction::Watch:
        watch_review(choice.measure);
        break;
    case ReviewAction::Practise:
        practise_review(choice.measure);
        break;
    case ReviewAction::Pin:
        pin_review();
        break;
    case ReviewAction::ChartView:
        model.app.runs_chart_view = etude::next_runs_chart_view(model.app.runs_chart_view);
        break;
    case ReviewAction::Done: // the page answers Done through the card
    case ReviewAction::None:
        break;
    }
}

void WindowState::replay_action(const etude::app::ReplayChoice& choice) {
    switch (choice.action) {
    case ReplayAction::Close:
        replay.close_view();
        break;
    case ReplayAction::PlayPause:
        replay.play_pause();
        break;
    case ReplayAction::Restart:
        replay.restart();
        break;
    case ReplayAction::SeekBack:
        replay.skip(-kReplaySkipSeconds);
        break;
    case ReplayAction::SeekForward:
        replay.skip(kReplaySkipSeconds);
        break;
    case ReplayAction::Seek:
        replay.seek_fraction(choice.fraction);
        break;
    case ReplayAction::Sound:
        replay.toggle_sound();
        break;
    case ReplayAction::Speed:
        replay.next_speed();
        break;
    case ReplayAction::PrevMistake:
        replay.jump_mistake(-1);
        break;
    case ReplayAction::NextMistake:
        replay.jump_mistake(1);
        break;
    case ReplayAction::Summary:
        replay.close_view();
        ui.open_summary();
        break;
    case ReplayAction::Practise:
        practise_review(0);
        break;
    case ReplayAction::Pin:
        pin_review();
        break;
    case ReplayAction::None:
        break;
    }
}

// The keys of the replay view: Space plays and pauses, Left and Right skip, Home goes to the start.
// Everything else is left alone.
void WindowState::replay_key(const SDL_KeyboardEvent& key) {
    switch (key.scancode) {
    case SDL_SCANCODE_SPACE:
        replay.play_pause();
        break;
    case SDL_SCANCODE_LEFT:
        replay.skip(-kReplaySkipSeconds);
        break;
    case SDL_SCANCODE_RIGHT:
        replay.skip(kReplaySkipSeconds);
        break;
    case SDL_SCANCODE_HOME:
        replay.restart();
        break;
    default:
        break;
    }
}

// The replay's clock and sound, once a frame; the review is let go with its card.
void WindowState::update_replay(std::int64_t now) {
    const double dt = replay_ns == 0 ? 0.0 : etude::app::capped_s(now - replay_ns);
    replay_ns = now;
    if (!results_card && !replay.viewing()) {
        replay.forget_review();
    }
    replay.update(dt);
    if (replay.take_pin_refused()) {
        show_message("Could not change the pin: the notes of this run are gone", true);
    }
    for (const etude::ReplaySoundEvent& e : replay.take_sound()) {
        if (e.velocity > 0) {
            active_sink().note_on(0, e.key, e.velocity);
        } else {
            active_sink().note_off(0, e.key);
        }
    }
}

// What the UI shows of the review and the replay this frame.
void WindowState::fill_replay_info(etude::app::UiFrameInfo& info) {
    review_info = {};
    review_info.offered = results_card.has_value() && replay.review() != ReplaySession::Review::None;
    review_info.pin = {replay.pin_available(), replay.pinned(), replay.pin_pending()};
    if (replay.review() == ReplaySession::Review::Ready && replay.data() != nullptr) {
        review_info.summary = &replay.data()->summary;
        review_info.watchable = replay.watchable();
        if (results_card.has_value()) {
            fill_board_info(review_info.board, results_card->serial, replay.record());
        }
    }
    info.review = &review_info;
    info.replay = nullptr;
    if (!replay.viewing() || !songs.has_song()) {
        return;
    }
    const etude::ReplayTimeline& timeline = replay.timeline();
    replay_info = {};
    replay_info.timeline = &timeline;
    replay_info.summary = &replay.data()->summary;
    replay_info.measures = songs.song().song.measures;
    replay_info.song_duration_s = songs.song().song.duration_s;
    replay_info.position_ms = replay.position_ms();
    replay_info.duration_ms = timeline.duration_ms;
    replay_info.playing = replay.playing();
    replay_info.sound_on = replay.sound_on();
    replay_info.speed_percent = replay.speed_percent();
    replay_info.prev_mistake = etude::mistake_position(timeline, replay.position_ms(), -1).has_value();
    replay_info.next_mistake = etude::mistake_position(timeline, replay.position_ms(), 1).has_value();
    replay_info.summary_offered = results_card.has_value();
    replay_info.practise_offered = replay.data()->summary.bar_count > 0;
    replay_info.pin = review_info.pin;
    info.replay = &replay_info;
}

// The song's notes as the replay shows them: where the replay is, in the hands the run practised,
// with the played bars over them.
void WindowState::gather_replay_notes() {
    const etude::ReplayTimeline& timeline = replay.timeline();
    const etude::Transposition shifted(timeline.setup.transpose_semitones,
                                         songs.transposition().keyboard());
    const double ahead = model.look.visible_seconds;
    songs.visible_notes_at(static_cast<double>(replay.position_ms()) / 1000.0, ahead, shifted,
                           visible_notes);
    const auto& notes = songs.song().song.notes;
    const etude::HandFilter practised = timeline.setup.hands;
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
        song_bars.push_back({static_cast<std::uint8_t>(v.pitch), n.velocity, n.hand,
                             static_cast<float>(v.bottom), static_cast<float>(v.top), mine,
                             static_cast<std::int8_t>(-1),
                             static_cast<std::int8_t>(v.folded ? v.fold_octaves : 0), false});
    }
    replay.marks(ahead, kReplayBehindS, replay_marks);
    // A wrong key lights as live play shows one: its red flash, held while the block is drawn.
    const etude::ReplayKeys::Lit& lit = replay.lit_keys();
    for (std::size_t key = 0; key < lit.size(); ++key) {
        if (lit[key].on && lit[key].wrong) {
            wrong_flash[key] = 1.0f;
        }
    }
}

// The developer's invented run, its replay and what the flags ask to see of it (--results-demo gets
// the replay for its own card; --summary-demo and --replay-demo make a card of the run's own).
void WindowState::show_replay_demo() {
    const bool wanted = opt.results_demo || opt.summary_demo || opt.replay_demo_s >= 0.0;
    if (!wanted || replay_demo_done || !songs.has_song() || frame_index < 5 || songs.playing() ||
        (opt.results_demo && !results_card)) {
        return;
    }
    if (opt.summary_board && history.loading()) {
        return; // the card's run goes in the history, which has to be read first
    }
    etude::app::DemoRun demo = make_demo_run(songs.song().song, songs.transposition(),
                                               songs.song().hash, opt.summary_short ? 5 : 600);
    if (opt.summary_board) {
        seed_card_run(demo.record);
    }
    if (!opt.results_demo) {
        results_card = etude::make_results_card(
            demo.record, etude::AttemptChange{}, true, songs.hands_available(), false,
            songs.song().song.measures, songs.song().difficulty.get(), model.app.weighted_score);
        results_card->serial = ++results_serial;
        card_watch = watch_now();
    }
    // An invented run has no row in the database (id 0): it cannot be pinned, and no real run is
    // touched.
    replay.adopt(demo.record, demo.replay, 0);
    if (opt.summary_demo) {
        ui.open_summary(opt.summary_board, opt.summary_runs);
    }
    if (opt.replay_demo_s >= 0.0) {
        replay.open_view(static_cast<std::int32_t>(opt.replay_demo_s * 1000.0));
        replay_ns = midi_clock_ns();
    }
    replay_demo_done = true;
}

} // namespace etude::app
