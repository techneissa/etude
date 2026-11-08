// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/shift_offer.hpp"

#include <algorithm>

namespace etude {

void ShiftOfferState::begin(bool never_ask) {
    offer_.reset();
    settled_ = false;
    never_ = never_ask;
}

void ShiftOfferState::judge_song(const ShiftFits& fits, int current) {
    if (quiet() || (offer_ && offer_->source == OfferSource::Keys)) {
        return;
    }
    const std::optional<int> target = suggest_octave_shift(fits, current);
    if (!target) {
        offer_.reset();
    } else {
        offer_ = ShiftOffer{*target, current, OfferSource::Song};
    }
}

std::optional<KeyRange> shifted_range(KeyRange range, int semitones) {
    const int lowest = range.lowest + semitones;
    const int highest = range.highest + semitones;
    if (lowest < 0 || highest > 127) {
        return std::nullopt;
    }
    return KeyRange{lowest, highest};
}

void ShiftOfferState::judge_keys(const ShiftResult& result, int current_shift, KeyRange keyboard) {
    if (quiet()) {
        return;
    }
    if (result.status == ShiftStatus::NoShift) {
        if (offer_ && offer_->source == OfferSource::Keys) {
            offer_.reset();
        }
        return;
    }
    if (result.status != ShiftStatus::Shift) {
        return;
    }
    const int target = current_shift - result.offset_semitones;
    if (target == current_shift || target < -kMaxKeyboardShift || target > kMaxKeyboardShift ||
        !shifted_range(keyboard, -result.offset_semitones)) {
        return;
    }
    offer_ = ShiftOffer{target, current_shift, OfferSource::Keys};
}

} // namespace etude
