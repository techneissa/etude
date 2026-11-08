// SPDX-License-Identifier: GPL-3.0-or-later
// The toast, the practice history, the results card and the trouble-bar run's check-in.
#include "window_state.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <span>
#include <string>
#include <utility>

namespace etude::app {

void WindowBase::show_message(std::string text, bool warning) {
    if (!text.empty()) {
        pending_toast.set(std::move(text), warning);
    }
}

void WindowBase::poll_history() {
    const auto started = std::chrono::steady_clock::now();
    const etude::app::HistoryPoll polled = history.poll();
    if (!polled.message.empty()) {
        show_message(polled.message, true);
    }
    if (polled.loaded) {
        sections.history_changed();
    }
    for (const etude::app::ProfileDeleted& gone : polled.deleted) {
        profile_deleted(gone);
    }
    if (opt.profiles_demo && !profiles_demo_done && !history.loading() &&
        (!opt.summary_demo || songs.has_song())) {
        demo_profiles();
    }
    // The profile the settings named is gone: the default one is in use, with its own settings.
    if (polled.profile_changed || (polled.loaded && model.app.active_profile != history.active_profile())) {
        apply_active_profile();
    }
    replay.poll(history, songs.has_song() ? songs.song().hash : 0);
    // "Replays kept per song" as the player set it; the next run saved prunes to it (one atomic
    // store, nothing heavy).
    if (history.events_kept_per_song() != model.app.run_events_kept) {
        history.set_events_kept_per_song(model.app.run_events_kept);
    }
    using Note = etude::app::SectionsController::HistoryNote;
    const auto note = history.state() == etude::app::HistoryState::Waiting
                          ? (history.waiting_unwritable() ? Note::Unwritable : Note::Busy)
                      : history.loading() ? etude::app::SectionsController::HistoryNote::Loading
                                          : etude::app::SectionsController::HistoryNote::None;
    sections.set_history_note(note);
    // The save is waiting: say so once, as it starts (the practice list's hand path card
    // keeps saying it while it lasts).
    if ((note == Note::Busy || note == Note::Unwritable) && last_history_note != note) {
        show_message(note == Note::Busy ? etude::app::SectionsController::kHistoryBusyText
                                        : etude::app::SectionsController::kHistoryUnwritableText);
    }
    last_history_note = note;
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started)
            .count();
    if (opt.startup_log) {
        if (polled.loaded) {
            const etude::app::HistoryStats stats = history.stats();
            std::printf("history: %d runs read in %.1f ms on the history thread, added "
                        "to the list on frame %ld (that frame's history work %.0f us; "
                        "the polls before it took at most %.0f us)\n",
                        stats.loaded_runs, stats.load_ms, frame_index, us, history_poll_max_us);
        } else {
            history_poll_max_us = std::max(history_poll_max_us, us);
        }
    }
}

CardWatch WindowBase::watch_now() {
    CardWatch w;
    w.load_serial = songs.load_serial();
    w.mode = songs.mode();
    w.hands = songs.practice_hands();
    w.speed_percent = songs.speed_percent();
    w.loop = songs.loop_enabled();
    w.loop_start_s = songs.loop_start_s();
    w.loop_end_s = songs.loop_end_s();
    w.position_s = songs.position_s();
    return w;
}

// What a tap or a key on the results card asks for: Replay (the same loop, the same hands),
// Left, Right or Both (those hands, and go again), the chosen trouble bars one by one or
// together, or Done. Keys and taps come through here, so they always do the same.
void WindowBase::card_action(const etude::CardChoice& choice) {
    if (!results_card || !etude::card_offers(*results_card, choice)) {
        return;
    }
    const std::int64_t now = midi_clock_ns();
    switch (choice.action) {
    case etude::CardAction::Replay:
        results_card.reset();
        songs.replay(now);
        break;
    case etude::CardAction::Hands:
        results_card.reset();
        songs.set_practice_hands(choice.hands, now);
        songs.replay(now);
        break;
    case etude::CardAction::OneByOne:
    case etude::CardAction::Together: {
        std::array<int, etude::kMaxCardBars> chosen{};
        std::size_t count = 0;
        for (int i = 0; i < results_card->bar_count; ++i) {
            if ((choice.bars & (1u << i)) != 0) {
                chosen[count++] = results_card->bars[static_cast<std::size_t>(i)].measure;
            }
        }
        // One by one in the order the player put the bars in, when they did.
        const bool own_order =
            choice.action == etude::CardAction::OneByOne && choice.order_count > 0;
        if (own_order) {
            count = 0;
            for (int i = 0; i < choice.order_count && i < etude::kMaxCardBars; ++i) {
                const std::size_t at = choice.order[static_cast<std::size_t>(i)];
                if (at < static_cast<std::size_t>(results_card->bar_count)) {
                    chosen[count++] = results_card->bars[at].measure;
                }
            }
        }
        const etude::Song& played = songs.song().song;
        const etude::BarPractice practice = etude::plan_bar_practice(
            std::span<const int>(chosen.data(), count), played.measures, played.duration_s,
            choice.action == etude::CardAction::OneByOne ? etude::BarPracticeKind::OneByOne
                                                           : etude::BarPracticeKind::Together,
            own_order);
        // One by one is a run: a check-in after each bar, skip and end, a summary.
        const etude::TroubleBars run_bars = etude::trouble_bars(*results_card, practice);
        const bool as_run = choice.action == etude::CardAction::OneByOne;
        if (sections.practise_bars(practice, now, as_run ? &run_bars : nullptr)) {
            results_card.reset();
        } else {
            show_message(sections.take_message());
        }
        break;
    }
    case etude::CardAction::Faster:
        if (drill) {
            etude::DrillSpec next = *drill;
            next.bpm = etude::drill_tempo(next.bpm + etude::kDrillBpmStep);
            model.app.learn_tempo = next.bpm; // the Learn card shows the tempo last played
            results_card.reset();
            start_drill(next);
        }
        break;
    case etude::CardAction::ToCard:
        results_card.reset();
        songs.pause(now);
        drill_card_asked = true;
        break;
    case etude::CardAction::Done:
        results_card.reset();
        break;
    case etude::CardAction::None:
        break;
    }
}

const etude::TroubleRun& WindowBase::shown_run() {
    return demo_run.phase() != etude::RunPhase::Idle ? demo_run : sections.run();
}

// A tap or a key on the check-in or the summary: Next bar, Again, Skip, End, or Done.
void WindowBase::run_action(etude::RunCardAction action) {
    if (action == etude::RunCardAction::None) {
        return;
    }
    const std::int64_t now = midi_clock_ns();
    if (action == etude::RunCardAction::Done) {
        demo_run.reset();
        sections.run_dismiss();
        return;
    }
    etude::RunCommand command{};
    if (etude::run_card_command(action, &command)) {
        sections.run_command(command, now);
        show_message(sections.take_message());
    }
}

// Saves finished runs and shows their results.
void WindowBase::save_runs() {
    const etude::app::FrameDetailScope saving(etude::app::FrameDetail::Save);
    for (etude::app::FinishedRun& f : songs.take_finished_runs()) {
        const std::time_t wall = std::time(nullptr);
        etude::RunRecord r = f.record;
        r.end_utc_s = static_cast<std::int64_t>(wall);
        r.start_utc_s = r.end_utc_s - (f.ended_ns - f.started_ns) / 1'000'000'000;
        r.local_day = etude::local_day_from_utc(
            r.start_utc_s, local_utc_offset_s(static_cast<std::time_t>(r.start_utc_s)));
        r = etude::sanitized(r);
        history.set_events_kept_per_song(model.app.run_events_kept);
        if (f.profile_id != history.active_profile()) {
            // Played under a profile that was switched away from: saved for its player, and no card
            // or comparison drawn for the one playing now.
            history.add_run(r, f.song_path, std::move(f.events), f.profile_id);
            continue;
        }
        const etude::AttemptChange change = etude::compare_with_last_attempt(history.runs(), r);
        // Saved on the history thread, off the frame.
        history.add_run(r, f.song_path, std::move(f.events), f.profile_id);
        sections.history_changed();
        const std::string line = etude::app::results_line(r, change, !history.loading());
        results_card = etude::make_results_card(
            r, change, !history.loading(), songs.hands_available(), sections.view().plan_running,
            songs.song().song.measures, songs.song().difficulty.get(), model.app.weighted_score);
        results_card->serial = ++results_serial;
        if (drill) {
            results_card->drill_bpm = drill->bpm;
            results_card->drill_title = songs.song().title;
        }
        card_watch = watch_now();
        // The run's review (its summary and its replay) is built on the history thread, after the
        // save, and comes back in a few frames.
        replay.ask(history, r, songs.note_table(), songs.measure_table());
        if (history.loading()) {
            // The comparison with earlier runs waits for the history.
            show_message(etude::app::SectionsController::kHistoryLoadingText);
        }
        std::printf("%s (graded %d, hit %d, wrong %d)\n", line.c_str(), r.all.graded, r.all.hit,
                    r.wrong);
    }
    if (songs.take_ungraded_run()) {
        results_card = etude::make_ungraded_card(
            songs.mode() == etude::PracticeMode::Wait ? etude::RunMode::Wait
                                                        : etude::RunMode::Flow,
            songs.practice_hands(), songs.hands_available(), sections.view().plan_running);
        results_card->serial = ++results_serial;
        if (drill) {
            results_card->drill_bpm = drill->bpm;
            results_card->drill_title = songs.song().title;
        }
        card_watch = watch_now();
        replay.forget_review();
    }
    // After the runs are taken: a profile switched since ends the run in progress under the old one.
    songs.set_profile(history.active_profile(), midi_clock_ns());
    for (const std::string& error : history.take_errors()) {
        show_message(error, true);
    }
}

} // namespace etude::app
