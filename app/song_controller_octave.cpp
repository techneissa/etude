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

void SongController::input_unsure(std::int64_t onset_ns) {
    if (loaded_) {
        flow_.apply_unsure(onset_ns);
    }
}

void SongController::feed_shift_detector(std::span<const MidiEvent> player_events) {
    // Only a keyboard's own octave buttons shift what arrives: the typing keys and the on-screen
    // keys are the player's choice, and the microphone hears the pitch it hears.
    bool seen = false;
    std::array<int, kOctaveShiftMaxExpected> expected{};
    for (const MidiEvent& ev : player_events) {
        if (ev.type != MidiEventType::NoteOn || ev.source != InputSource::Midi) {
            continue;
        }
        const int n = expected_around(expected);
        shift_detector_.observe(ev.data1,
                                std::span<const int>(expected.data(), static_cast<std::size_t>(n)));
        seen = true;
    }
    if (seen) {
        offer_.judge_keys(shift_detector_.result(), keyboard_shift_, keyboard_);
    }
}

int SongController::expected_around(std::span<int> out) const {
    if (!loaded_ || out.empty() || !(transport_.playing() || transport_.held())) {
        return 0;
    }
    // The chord Wait Mode holds at, else the clock; a press is a little early or late.
    const double centre = mode_ == PracticeMode::Wait && wait_.waiting_for_s() >= 0.0
                              ? wait_.waiting_for_s()
                              : transport_.position_s();
    constexpr double kAround = 0.4;
    const auto& notes = song_.song.notes;
    auto it = std::lower_bound(notes.begin(), notes.end(), centre - kAround,
                               [](const Note& n, double s) { return n.start_s < s; });
    std::size_t count = 0;
    for (; it != notes.end() && it->start_s <= centre + kAround && count < out.size(); ++it) {
        if (it->drum || it->accompaniment ||
            (applied_hands_ == HandFilter::Left && it->hand != Hand::Left) ||
            (applied_hands_ == HandFilter::Right && it->hand != Hand::Right)) {
            continue;
        }
        const TransposedPitch key = source_.value().expected_pitch(*it);
        if (!key.playable ||
            std::find(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count), key.pitch) !=
                out.begin() + static_cast<std::ptrdiff_t>(count)) {
            continue;
        }
        out[count++] = key.pitch;
    }
    return static_cast<int>(count);
}

void SongController::accept_shift_offer(std::int64_t now_ns) {
    if (!offer_.offer()) {
        return;
    }
    const ShiftOffer offer = *offer_.offer();
    offer_.accept();
    shift_detector_.reset(); // the old notes must not argue for the same shift again
    if (offer.source == OfferSource::Keys) {
        // The song stays as it is; the keyboard's range moves (the app does it,
        // take_keyboard_shift).
        keyboard_shift_asked_ = offer.target;
        return;
    }
    set_transpose(offer.target, now_ns);
}

bool SongController::take_keyboard_shift(int* semitones) {
    if (!keyboard_shift_asked_) {
        return false;
    }
    *semitones = *keyboard_shift_asked_;
    keyboard_shift_asked_.reset();
    return true;
}

void SongController::dismiss_shift_offer() { offer_.dismiss(); }

void SongController::never_offer_shift() {
    offer_.never();
    never_offer_ = loaded_;
}

std::span<const int> SongController::expected_keys() const {
    if (!loaded_ || mode_ != PracticeMode::Wait || wait_.waiting_for_s() < 0.0 ||
        !(transport_.playing() || transport_.held())) {
        return {};
    }
    return wait_.waiting_keys();
}

std::span<const SongController::HintKey> SongController::waiting_keys() const {
    std::vector<HintKey>& out = hint_keys_;
    out.clear();
    if (!waiting()) {
        return out;
    }
    const double at = wait_.waiting_for_s();
    const auto& notes = song_.song.notes;
    auto it = std::lower_bound(notes.begin(), notes.end(), at - 2.0 * kChordSeconds,
                               [](const Note& n, double s) { return n.start_s < s; });
    for (const int key : wait_.waiting_keys()) {
        HintKey h{key, Hand::Unknown};
        bool found = false;
        for (auto n = it; n != notes.end() && n->start_s <= at + 2.0 * kChordSeconds; ++n) {
            if (n->drum || n->accompaniment || source_.value().expected_pitch(*n).pitch != key) {
                continue;
            }
            if (!found) {
                h.hand = n->hand;
                found = true;
            } else if (h.hand != n->hand) {
                h.hand = Hand::Unknown;
            }
        }
        out.push_back(h);
    }
    return out;
}

} // namespace etude::app
