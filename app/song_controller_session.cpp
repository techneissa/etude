// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_controller.hpp"

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

SessionState SongController::session_state(const SessionState& base) const {
    SessionState s = base;
    write_session_state(s);
    return s;
}

void SongController::write_session_state(SessionState& s) const {
    s.speed_percent = transport_.speed_percent();
    s.practice_hands = hands_;
    s.piano_plays = to_piano_plays(sound_);
    s.mode = mode_;
    s.transpose_semitones = semitones_;
    s.count_in_bars = count_in_bars_;
    if (!loaded_ || (song_.library_path.empty() && song_.absolute_path.empty())) {
        s.song_path.clear();
        s.song_abs_path.clear();
        s.song_hash = 0;
        s.position_s = 0.0;
        s.loop_start_s = s.loop_end_s = 0.0;
        s.loop_enabled = false;
        return;
    }
    s.song_path = song_.library_path;
    s.song_abs_path = song_.absolute_path;
    s.song_hash = song_.hash;
    s.position_s = transport_.ended() ? 0.0 : transport_.position_s();
    s.loop_start_s = transport_.loop_enabled() ? transport_.loop_start_s() : 0.0;
    s.loop_end_s = transport_.loop_enabled() ? transport_.loop_end_s() : 0.0;
    s.loop_enabled = transport_.loop_enabled();
}

SongPractice SongController::practice() const {
    SongPractice p;
    practice_into(p);
    return p;
}

void SongController::practice_into(SongPractice& p) const {
    p.speed_percent = transport_.speed_percent();
    p.count_in_bars = count_in_bars_;
    p.hands = hands_;
    p.transpose_semitones = semitones_;
    p.split_point = split_point_;
    p.no_shift_offer = loaded_ && never_offer_;
    p.parts_set = loaded_ && !acc_parts_.empty();
    if (p.parts_set) {
        p.muted_parts = acc_muted_;
    } else {
        p.muted_parts.clear();
    }
    p.loop_start_s = 0.0;
    p.loop_end_s = 0.0;
    p.loop_enabled = false;
    if (loaded_ && transport_.loop_enabled()) {
        p.loop_start_s = transport_.loop_start_s();
        p.loop_end_s = transport_.loop_end_s();
        p.loop_enabled = true;
    }
    p.section_id = 0; // the sections controller's
    p.plan_item = 0;
}

void SongController::apply_loop(double start_s, double end_s, bool enabled, std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    const double duration = std::isfinite(song_.song.duration_s) ? song_.song.duration_s : 0.0;
    start_s = std::min(start_s, duration);
    end_s = std::min(end_s, duration);
    if (end_s - start_s < kMinLoopSeconds) {
        if (transport_.loop_enabled()) {
            clear_loop(now_ns);
        }
        return;
    }
    if (transport_.loop_enabled() && transport_.loop_start_s() == start_s &&
        transport_.loop_end_s() == end_s) {
        return;
    }
    if (!set_loop(start_s, end_s, now_ns)) {
        return;
    }
    if (!enabled) {
        pump(now_ns);
        transport_.set_loop_enabled(now_ns, false);
        pump(now_ns);
    }
}

void SongController::apply_practice(const SongPractice& practice, std::int64_t now_ns) {
    const SongPractice p = sanitized(practice);
    set_practice_hands(p.hands, now_ns);
    if (p.speed_percent != transport_.speed_percent()) {
        set_speed(p.speed_percent, now_ns);
    }
    set_count_in_bars(p.count_in_bars);
    if (loaded_ && p.no_shift_offer != never_offer_) {
        never_offer_ = p.no_shift_offer;
        offer_.set_never(never_offer_);
    }
    if (p.transpose_semitones != semitones_) {
        set_transpose(p.transpose_semitones, now_ns);
    }
    set_hand_split(p.split_point, now_ns);
    // A transposition the song was saved with is a choice already made, like one set by hand: it
    // settles the octave-shift offer, also when it arrives after the first frames. None is the
    // default, not a choice.
    if (p.transpose_semitones != 0) {
        offer_.player_changed();
        offer_settled_by_choice_ = true; // and again when the offer starts over for a new keyboard
    }
    if (p.parts_set) {
        set_accompaniment_muted(p.muted_parts, now_ns);
    }
    if (loaded_) {
        const bool had = transport_.loop_enabled();
        apply_loop(p.loop_start_s, p.loop_end_s, p.loop_enabled, now_ns);
        if (!had && transport_.loop_enabled()) {
            seek(transport_.loop_start_s(), now_ns); // starts at the loop, not before it
        }
    }
}

void SongController::apply_part_mutes(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    // Notes handed over were chosen under the old mutes: cancel them and choose again.
    if (sound_cursor_) {
        restart_sound(now_ns, transport_.position_s());
    }
    for (const AccompanimentPart& part : acc_parts_) {
        const bool muted =
            std::binary_search(acc_muted_.begin(), acc_muted_.end(), part.key);
        if (playback_.part_muted(part.index) != muted) {
            playback_.set_part_muted(part.index, muted, now_ns, scratch_);
        }
    }
    // With every listed part muted the button reads Off, so the whole accompaniment is: this also
    // silences parts past the list's cap (kMaxAccompanimentParts), which have no row of their own.
    const bool all_off = !acc_parts_.empty() && acc_muted_.size() == acc_parts_.size();
    if (playback_.accompaniment_muted() != all_off) {
        playback_.set_accompaniment_muted(all_off, now_ns, scratch_);
    }
    enqueue_reactive(scratch_, now_ns, false);
}

void SongController::set_accompaniment_muted(const std::vector<std::uint32_t>& keys,
                                             std::int64_t now_ns) {
    std::vector<std::uint32_t> wanted = muted_keys_in(acc_parts_, keys);
    if (wanted == acc_muted_) {
        return;
    }
    acc_muted_ = std::move(wanted);
    pump(now_ns);
    apply_part_mutes(now_ns);
}

void SongController::apply_session(const SessionState& state, std::int64_t now_ns) {
    set_mode(state.mode, now_ns);
    set_practice_hands(state.practice_hands, now_ns);
    set_song_sound(song_sound_from(state.piano_plays), now_ns);
    set_speed(state.speed_percent, now_ns);
    set_count_in_bars(state.count_in_bars);
    set_transpose(state.transpose_semitones, now_ns);
    if (!loaded_) {
        return;
    }
    if (state.has_loop()) {
        set_loop(state.loop_start_s, state.loop_end_s, now_ns);
        if (!state.loop_enabled) {
            pump(now_ns);
            transport_.set_loop_enabled(now_ns, false);
            pump(now_ns);
        }
    }
    seek(state.position_s, now_ns);
}

} // namespace etude::app
