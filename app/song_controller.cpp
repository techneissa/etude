// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_controller.hpp"
#include "log_ring.hpp"

#include "hand_split_job.hpp"
#include "lent_song.hpp"
#include "practice_tables_job.hpp"

#include "etude/look_ahead.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace etude::app {

namespace {

void report_stuck(const Playback& p) {
    log_printf( "etude: a song Playback was dropped with %zu notes sounding\n",
                 p.sounding_count());
}

} // namespace

const char* song_sound_name(SongSound sound) {
    switch (sound) {
    case SongSound::Mute:
        return "Mute";
    case SongSound::OtherHand:
        return "Other hand";
    case SongSound::BothHands:
        return "Both hands";
    }
    return "Mute";
}

PianoPlays to_piano_plays(SongSound sound) {
    switch (sound) {
    case SongSound::Mute:
        return PianoPlays::Nothing;
    case SongSound::OtherHand:
        return PianoPlays::OtherHand;
    case SongSound::BothHands:
        return PianoPlays::Both;
    }
    return PianoPlays::Nothing;
}

SongSound song_sound_from(PianoPlays plays) {
    switch (plays) {
    case PianoPlays::Nothing:
        return SongSound::Mute;
    case PianoPlays::OtherHand:
    case PianoPlays::Left:
    case PianoPlays::Right:
        return SongSound::OtherHand;
    case PianoPlays::Both:
        return SongSound::BothHands;
    }
    return SongSound::Mute;
}

const char* hands_name(HandFilter hands) {
    switch (hands) {
    case HandFilter::Both:
        return "Both hands";
    case HandFilter::Left:
        return "Left hand";
    case HandFilter::Right:
        return "Right hand";
    }
    return "Both hands";
}

const char* mode_name(PracticeMode mode) {
    switch (mode) {
    case PracticeMode::Wait:
        return "Wait";
    case PracticeMode::Flow:
        return "Flow";
    case PracticeMode::FreePlay:
        return "Free play";
    }
    return "Flow";
}

SongController::SongController()
    : split_job_(std::make_unique<HandSplitJob>()), lent_(std::make_shared<LentSong>()) {
    hint_keys_.reserve(128); // a chord holds each key once
    playback_.set_teardown_check(&report_stuck);
    playback_.set_click_sound(click_sound_for(options_.click_voice, options_.click_volume));
    playback_.follow(&source_);
    flow_.follow(&source_);
    wait_.follow(&source_);
    audition_pb_.set_teardown_check(&report_stuck);
    audition_pb_.follow(&source_);
}

SongController::~SongController() {
    audition_pb_.follow(nullptr);
    // The app calls shutdown() first; without it Playback's teardown check reports the bug.
    playback_.follow(nullptr);
    flow_.follow(nullptr);
    wait_.follow(nullptr);
}

void SongController::follow_wait(std::int64_t now_ns) {
    const WaitDirective d = wait_.update(now_ns);
    if (!loaded_ || mode_ != PracticeMode::Wait) {
        return;
    }
    if (d.hold_now) {
        transport_.hold(now_ns);
    }
    if (d.release) {
        transport_.release(now_ns);
    }
    if (d.arm_hold) {
        transport_.hold_at(now_ns, d.hold_at_s);
    }
    pump(now_ns);
}

// Notes a change of speed, transposition or look-ahead during the run (set_speed, set_transpose,
// the auto speed-up, the keyboard range and set_options all end up here, whoever made it). Hands and mode end the run
// instead (finish_run), so a run only ever has one of each.
void SongController::watch_run_setup() {
    if (run_active_ && (transport_.speed_percent() != run_speed_percent_ ||
                        semitones_ != run_semitones_ ||
                        options_.lookahead_s != run_lookahead_s_)) {
        run_setup_changed_ = true;
    }
}

void SongController::cancel_countdown() {
    countdown_end_ns_.reset();
    countdown_ticks_left_ = 0;
}

void SongController::start_transport(std::int64_t now_ns, bool count_in) {
    transport_.set_count_in_bars(count_in ? count_in_bars_ : 0);
    transport_.play(now_ns);
}

// The targets and chords a swap replaced go to the worker to be freed: a heap vector for each of a
// long song's notes is milliseconds on the frame thread.
void SongController::retire_tables() {
    if (!flow_.has_retired() && !wait_.has_retired()) {
        return;
    }
    if (tables_job_ == nullptr) {
        tables_job_ = std::make_unique<PracticeTablesJob>();
    }
    tables_job_->discard(flow_.take_retired(), wait_.take_retired());
}

void SongController::menu_opened(std::int64_t now_ns) {
    if (menu_open_) {
        return;
    }
    menu_open_ = true;
    resume_after_menu_ = loaded_ && (transport_.playing() || counting_down());
    cancel_countdown();
    if (loaded_) {
        pump(now_ns);
        transport_.pause(now_ns);
        pump(now_ns);
    }
}

void SongController::menu_closed(std::int64_t now_ns) {
    if (!menu_open_) {
        return;
    }
    menu_open_ = false;
    stop_audition(now_ns);
    if (resume_after_menu_ && loaded_) {
        // One countdown: the count-in's clicks and numbers when it is on, else the seconds.
        if (count_in_wanted()) {
            pump(now_ns);
            start_transport(now_ns, true);
            pump(now_ns);
            follow_wait(now_ns);
        } else {
            start_countdown(now_ns);
        }
    }
    resume_after_menu_ = false;
}

void SongController::lost_focus(std::int64_t now_ns) {
    pause(now_ns);
    silence(now_ns);
}

void SongController::silence(std::int64_t now_ns) {
    pump(now_ns);
    stop_audition(now_ns);
    // The song may go on playing (another output took over): what was handed over for the old one
    // is cancelled and the plan starts again from the position.
    if (sound_cursor_) {
        restart_sound(now_ns, transport_.position_s());
    }
    playback_.silence(now_ns, scratch_);
    enqueue_reactive(scratch_, now_ns, true);
}

void SongController::shutdown(std::int64_t now_ns, std::vector<SongSoundEvent>& out) {
    pump(now_ns);
    stop_audition(now_ns);
    finish_run(now_ns);
    cancel_countdown();
    if (loaded_ && transport_.playing()) {
        transport_.pause(now_ns);
        pump(now_ns);
    }
    playback_.silence(now_ns, scratch_);
    enqueue_reactive(scratch_, now_ns, true);
    sound_cursor_.reset();
    audition_pb_.silence(now_ns, scratch_);
    enqueue_plain(scratch_);
    out.insert(out.end(), outbox_.begin(), outbox_.end());
    outbox_.clear();
    last_due_ns_ = INT64_MIN;
}

void SongController::update(std::int64_t now_ns, std::span<const MidiEvent> player_events) {
    if (last_now_ns_ == 0) {
        last_now_ns_ = now_ns;
    }
    const bool was_playing = transport_.playing();
    take_in(player_events); // before the clock's ops, which may be a wrap or a held song's seek
    pump(now_ns);
    poll_hand_split(now_ns);
    refresh_hands_scope(now_ns);
    update_tables(now_ns);

    if (countdown_end_ns_) {
        const std::int64_t end = *countdown_end_ns_;
        while (countdown_ticks_left_ > 0 &&
               now_ns >= end - static_cast<std::int64_t>(countdown_ticks_left_) * 1'000'000'000) {
            --countdown_ticks_left_;
            // A tick is a soft click of the metronome's voice, due now.
            TransportOp tick;
            tick.kind = TransportOp::Kind::Click;
            tick.at_ns = now_ns;
            tick.to_s = transport_.position_s();
            tick.beat = 2;
            tick.sub = 1;
            playback_.apply(tick, scratch_);
            enqueue_plain(scratch_);
        }
        if (now_ns >= end) {
            cancel_countdown();
            if (loaded_) {
                start_transport(now_ns, false);
                pump(now_ns);
                follow_wait(now_ns);
            }
        }
    }

    if (loaded_) {
        refresh_shift_fits();
        offer_.judge_song(fits_, semitones_);
        flow_.update(now_ns);
        follow_wait(now_ns);
        retire_tables();
        watch_run_setup();
        collect_grades(now_ns);
        collect_wrongs(now_ns);

        std::vector<PassResult> passes;
        wait_.take_passes(passes); // the other mode's passes are dropped
        if (mode_ == PracticeMode::Wait) {
            for (const PassResult& p : passes) {
                transport_.report_pass(now_ns, p.clean);
                loop_passes_.push_back({p.clean, now_ns, p.graded, p.missed, p.wrong, p.perfect,
                                        p.good, p.early, p.late, p.played, p.longest_combo});
            }
            passes_.insert(passes_.end(), passes.begin(), passes.end());
        }
        passes.clear();
        flow_.take_passes(passes);
        if (mode_ == PracticeMode::Flow) {
            for (const PassResult& p : passes) {
                transport_.report_pass(now_ns, p.clean);
                loop_passes_.push_back({p.clean, now_ns, p.graded, p.missed, p.wrong, p.perfect,
                                        p.good, p.early, p.late, p.played, p.longest_combo});
            }
            passes_.insert(passes_.end(), passes.begin(), passes.end());
        }
        pump(now_ns);
        pump_audition(now_ns);

        if (run_active_ && was_playing) {
            run_playing_ns_ += std::max<std::int64_t>(0, now_ns - last_now_ns_);
        }
        if (transport_.ended()) {
            finish_run(now_ns);
        }
    } else {
        collect_grades(now_ns);
        collect_wrongs(now_ns);
    }
    last_now_ns_ = now_ns;
    taken_in_ = 0; // the frame's list is done
}

void SongController::take_in(std::span<const MidiEvent> player_events) {
    if (player_events.size() < taken_in_) {
        taken_in_ = 0; // a new frame's list
    }
    if (loaded_ && taken_in_ < player_events.size()) {
        const std::span<const MidiEvent> fresh = player_events.subspan(taken_in_);
        for (const MidiEvent& ev : fresh) {
            flow_.apply(ev);
            wait_.apply(ev);
        }
        // The octave detector reads each event once, here, not from the list update() is given:
        // the host's list grows when a pull adds to it during the frame (and may move), so no
        // span of it is kept past a pump.
        feed_shift_detector(fresh);
    }
    taken_in_ = player_events.size();
}

void SongController::take_sound_events(std::vector<SongSoundEvent>& out) {
    out.insert(out.end(), outbox_.begin(), outbox_.end());
    outbox_.clear();
}

std::vector<FinishedRun> SongController::take_finished_runs() {
    std::vector<FinishedRun> out;
    out.swap(finished_);
    return out;
}

int SongController::bar() const {
    if (!loaded_ || song_.song.measures.empty()) {
        return 0;
    }
    const auto& m = song_.song.measures;
    const double pos = transport_.position_s();
    auto it = std::upper_bound(m.begin(), m.end(), pos + 1e-9,
                               [](double s, const Measure& x) { return s < x.start_s; });
    return it == m.begin() ? m.front().number : std::prev(it)->number;
}

int SongController::written_bar() const {
    if (!loaded_ || song_.song.measures.empty()) {
        return 0;
    }
    const auto& m = song_.song.measures;
    const double pos = transport_.position_s();
    auto it = std::upper_bound(m.begin(), m.end(), pos + 1e-9,
                               [](double s, const Measure& x) { return s < x.start_s; });
    return it == m.begin() ? m.front().written_number() : std::prev(it)->written_number();
}

int SongController::bar_count() const {
    return loaded_ && !song_.song.measures.empty() ? song_.song.measures.back().number : 0;
}

void SongController::visible_notes(double ahead_s, std::vector<VisibleNote>& out) const {
    out.clear();
    if (!loaded_) {
        return;
    }
    NoteWindowOptions o;
    o.ahead_s = ahead_s;
    o.hand = HandFilter::Both;
    o.transposition = source_.value();
    window_.query(display_position_s(), o, out);
}

void SongController::visible_notes_at(double position_s, double ahead_s,
                                      const Transposition& transposition,
                                      std::vector<VisibleNote>& out) const {
    out.clear();
    if (!loaded_) {
        return;
    }
    NoteWindowOptions o;
    o.ahead_s = ahead_s;
    o.hand = HandFilter::Both;
    o.transposition = transposition;
    window_.query(position_s, o, out);
}

std::optional<KeyRange> SongController::key_range() const {
    if (!loaded_) {
        return std::nullopt;
    }
    // Worked out once per song and per transposition (a new keyboard is a new one), never per
    // frame: the key view rests on it while the song plays.
    if (!key_range_valid_ || key_range_serial_ != load_serial_ ||
        key_range_version_ != source_.version()) {
        key_range_ = drawn_key_range(song_.song.notes, source_.value());
        key_range_serial_ = load_serial_;
        key_range_version_ = source_.version();
        key_range_valid_ = true;
    }
    return key_range_;
}

bool SongController::waiting() const {
    return loaded_ && mode_ == PracticeMode::Wait && transport_.held() &&
           wait_.waiting_for_s() >= 0.0;
}

void SongController::refresh_shift_fits() {
    if (fits_valid_ && fits_serial_ == load_serial_) {
        return;
    }
    // A new song or keyboard: its fits, a detector on this keyboard that has seen nothing, and an
    // offer that starts over (with the song's saved "never").
    fits_ = etude::shift_fits(song_.song.notes, keyboard_);
    fits_serial_ = load_serial_;
    fits_valid_ = true;
    shift_detector_.set_range(keyboard_);
    offer_.begin(never_offer_);
    if (offer_settled_by_choice_) {
        offer_.player_changed();
    }
}

} // namespace etude::app
