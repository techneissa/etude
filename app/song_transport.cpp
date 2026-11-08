// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_transport.hpp"

#include <algorithm>

namespace etude::app {

namespace {

ui::SongSound to_ui(SongSound s) {
    switch (s) {
    case SongSound::Mute:
        return ui::SongSound::Mute;
    case SongSound::OtherHand:
        return ui::SongSound::OtherHand;
    case SongSound::BothHands:
        return ui::SongSound::BothHands;
    }
    return ui::SongSound::Mute;
}

SongSound from_ui(ui::SongSound s) {
    switch (s) {
    case ui::SongSound::Mute:
        return SongSound::Mute;
    case ui::SongSound::OtherHand:
        return SongSound::OtherHand;
    case ui::SongSound::BothHands:
        return SongSound::BothHands;
    }
    return SongSound::Mute;
}

} // namespace

SongTransport::SongTransport(SongController& songs) : songs_(songs) {}

void SongTransport::update(std::int64_t now_ns) {
    view_.has_song = songs_.has_song();
    const HandFilter hands = songs_.practice_hands();
    if (!view_.has_song) {
        view_ = ui::SongView{};
        view_.left_hand = hands != HandFilter::Right;
        view_.right_hand = hands != HandFilter::Left;
        bar_starts_.clear();
        bars_for_ = nullptr;
        countdown_total_s_ = 0;
        return;
    }
    const LoadedSong& loaded = songs_.song();
    if (bars_for_ != &loaded || bars_hash_ != loaded.hash || bar_starts_.empty()) {
        bar_starts_.clear();
        for (const Measure& m : loaded.song.measures) {
            bar_starts_.push_back(m.start_s);
        }
        bars_for_ = &loaded;
        bars_hash_ = loaded.hash;
    }
    view_.title = loaded.title;
    view_.position_s = songs_.position_s();
    view_.duration_s = songs_.duration_s();
    view_.playing = songs_.playing();
    view_.loop_enabled = songs_.loop_enabled();
    view_.loop_start_s = songs_.loop_start_s();
    view_.loop_end_s = songs_.loop_end_s();
    view_.bar_starts_s = bar_starts_;
    view_.bar_difficulty = {};
    if (loaded.difficulty != nullptr && loaded.difficulty->bars.size() == bar_starts_.size()) {
        view_.bar_difficulty = loaded.difficulty->bars;
    }
    view_.sound = to_ui(songs_.song_sound());
    // The accompaniment's parts for the menu, rebuilt only when the song or a mute changes.
    {
        const auto& parts = songs_.accompaniment_parts();
        const auto& muted = songs_.accompaniment_muted();
        if (accompaniment_for_ != &loaded || accompaniment_hash_ != loaded.hash ||
            accompaniment_muted_ != muted || accompaniment_.size() != parts.size()) {
            accompaniment_.clear();
            for (const AccompanimentPart& part : parts) {
                accompaniment_.push_back(
                    {part.name, std::binary_search(muted.begin(), muted.end(), part.key)});
            }
            accompaniment_muted_ = muted;
            accompaniment_for_ = &loaded;
            accompaniment_hash_ = loaded.hash;
        }
        view_.has_accompaniment = !parts.empty();
        view_.accompaniment_voice =
            !parts.empty() && std::all_of(parts.begin(), parts.end(),
                                          [](const AccompanimentPart& p) { return p.voice; });
        const AccompanimentState state = songs_.accompaniment_state();
        view_.accompaniment = state == AccompanimentState::On
                                  ? ui::AccompState::On
                                  : (state == AccompanimentState::Off ? ui::AccompState::Off
                                                                      : ui::AccompState::Partial);
        view_.accompaniment_parts = accompaniment_;
    }
    view_.mode = songs_.mode() == PracticeMode::Wait ? ui::PlayMode::Wait : ui::PlayMode::Flow;
    view_.transpose = songs_.transpose();
    view_.shift_fits = songs_.shift_fits();
    const std::optional<ShiftOffer>& offer = songs_.shift_offer();
    view_.shift_offer = offer.has_value();
    view_.shift_offer_target = offer ? offer->target : 0;
    view_.shift_offer_from = offer ? offer->from : 0;
    view_.shift_offer_from_keys = offer && offer->source == OfferSource::Keys;
    view_.left_hand = hands != HandFilter::Right;
    view_.right_hand = hands != HandFilter::Left;
    view_.left_available = songs_.hands_available().left;
    view_.right_available = songs_.hands_available().right;
    view_.hands_in_section = songs_.hands_scope_is_loop();
    view_.split_available = songs_.hand_split_available();
    view_.split_fixed = songs_.hand_split() != kAutoSplit;
    view_.split_key =
        std::clamp(songs_.hand_split_point() + songs_.transposition().semitones(), 0, 127);
    view_.split_by_staves = songs_.hands_from_staves();
    view_.split_flats = key_spells_flats(key_at_seconds(loaded.song.keys, songs_.position_s()),
                                         songs_.transposition().semitones());
    view_.song_serial = songs_.load_serial();
    view_.skip_back_s = songs_.skip_back_seconds();
    view_.skip_forward_s = songs_.skip_forward_seconds();
    view_.speed_percent = songs_.speed_percent();
    view_.tempo_bpm = songs_.tempo_bpm();
    view_.main_bpm = songs_.main_tempo_bpm();
    view_.beat_dotted = songs_.tempo_beat_dotted();
    view_.count_in_bars = songs_.count_in_bars();
    view_.count_in_beat = songs_.count_in_label();
    view_.count_in_beats = songs_.count_in_beats();
    view_.count_in_bar_beats = songs_.count_in_bar_beats();
    view_.count_in_progress = songs_.count_in_progress();
    const SongController::BeatPulse pulse = songs_.beat_pulse();
    view_.beat_active = pulse.active;
    view_.beat_age_s = pulse.age_s;
    view_.beat_accent = pulse.accent;

    const int whole = songs_.countdown();
    if (whole <= 0) {
        countdown_total_s_ = 0;
        view_.countdown_left_s = 0.0;
        return;
    }
    if (countdown_total_s_ == 0) {
        countdown_total_s_ = whole;
        countdown_start_ns_ = now_ns;
    }
    // Smooth, but never more than a second away from the controller's own count.
    const double left = static_cast<double>(countdown_total_s_) -
                        static_cast<double>(now_ns - countdown_start_ns_) / 1e9;
    view_.countdown_left_s =
        std::clamp(left, static_cast<double>(whole - 1) + 1e-3, static_cast<double>(whole));
    view_.countdown_total_s = countdown_total_s_;
}

void SongTransport::toggle_play(std::int64_t now_ns) {
    songs_.toggle_play(now_ns);
    update(now_ns);
}

void SongTransport::replay(std::int64_t now_ns) {
    songs_.replay(now_ns);
    update(now_ns);
}

void SongTransport::skip_back(std::int64_t now_ns) {
    songs_.skip_back(now_ns);
    update(now_ns);
}

void SongTransport::skip_forward(std::int64_t now_ns) {
    songs_.skip_forward(now_ns);
    update(now_ns);
}

void SongTransport::seek(double song_s, std::int64_t now_ns) {
    songs_.seek(song_s, now_ns);
    update(now_ns);
}

void SongTransport::set_hands(bool left, bool right, std::int64_t now_ns) {
    if (!left && !right) {
        return;
    }
    songs_.set_practice_hands(
        left && right ? HandFilter::Both : (left ? HandFilter::Left : HandFilter::Right), now_ns);
    update(now_ns);
}

void SongTransport::set_split_key(int key, std::int64_t now_ns) {
    if (songs_.hand_split_available()) {
        songs_.set_hand_split(std::clamp(key - songs_.transposition().semitones(), 1, 127), now_ns);
        update(now_ns);
    }
}

void SongTransport::reset_split(std::int64_t now_ns) {
    if (songs_.hand_split_available()) {
        songs_.set_hand_split(kAutoSplit, now_ns);
        update(now_ns);
    }
}

void SongTransport::set_speed(int percent, std::int64_t now_ns) {
    songs_.set_speed(percent, now_ns);
    update(now_ns);
}

void SongTransport::set_target_bpm(double bpm, std::int64_t now_ns) {
    songs_.set_target_bpm(bpm, now_ns);
    update(now_ns);
}

void SongTransport::set_count_in_bars(int bars, std::int64_t now_ns) {
    songs_.set_count_in_bars(bars);
    update(now_ns);
}

void SongTransport::set_sound(ui::SongSound sound, std::int64_t now_ns) {
    songs_.set_song_sound(from_ui(sound), now_ns);
    update(now_ns);
}

void SongTransport::clear_loop(std::int64_t now_ns) {
    songs_.clear_loop(now_ns);
    update(now_ns);
}

void SongTransport::tap_accompaniment(std::int64_t now_ns) {
    songs_.set_accompaniment_muted(
        after_button_tap(songs_.accompaniment_parts(), songs_.accompaniment_muted()), now_ns);
    update(now_ns);
}

void SongTransport::toggle_accompaniment_part(std::size_t index, std::int64_t now_ns) {
    const auto& parts = songs_.accompaniment_parts();
    if (index >= parts.size()) {
        return;
    }
    songs_.set_accompaniment_muted(after_part_toggle(songs_.accompaniment_muted(), parts[index].key),
                                   now_ns);
    update(now_ns);
}

void SongTransport::set_transpose(int semitones, std::int64_t now_ns) {
    songs_.player_set_transpose(semitones, now_ns);
    update(now_ns);
}

void SongTransport::accept_shift_offer(std::int64_t now_ns) {
    songs_.accept_shift_offer(now_ns);
    update(now_ns);
}

void SongTransport::dismiss_shift_offer(std::int64_t now_ns) {
    songs_.dismiss_shift_offer();
    update(now_ns);
}

void SongTransport::never_offer_shift(std::int64_t now_ns) {
    songs_.never_offer_shift();
    update(now_ns);
}

void SongTransport::set_mode(ui::PlayMode mode, std::int64_t now_ns) {
    songs_.set_mode(mode == ui::PlayMode::Wait ? PracticeMode::Wait : PracticeMode::Flow, now_ns);
    update(now_ns);
}

} // namespace etude::app
