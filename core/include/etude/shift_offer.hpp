// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/octave_shift.hpp"
#include "etude/shift_fit.hpp"

#include <optional>

namespace etude {

// The octave-shift offer: whether the card that proposes a shift is up, and which one. Two things
// raise it, both pure and checked here, and they answer differently:
//
//   the song     the song has notes outside the keyboard and a whole-octave move leaves fewer
//                (suggest_octave_shift), judged each time the song or the keyboard is new. The
//                offer names a total transposition (-12..+12): accepting it is one set_transpose.
//   the keys     the OctaveShiftDetector says the keyboard's own octave button shifted what the
//                player sends (`ShiftResult::offset_semitones`: add it to an arriving pitch for the
//                key meant). The song stays as it is: the offer names how far the keyboard's notes
//                are shifted in all (semitones, `current - offset`), and accepting it moves the
//                app's model of the keyboard (its KeyRange) by that much for the session, so the
//                screen keyboard, the keys expected and the folding follow what the keyboard sends.
//                A shift that would take the range past MIDI's 0..127 raises nothing.
//
// Dismiss hides the card until the song or the keyboard changes; "don't ask again" keeps it away
// for this song for good (the owner saves that choice with the song); a transposition the player
// sets by hand also ends the offer, since they took charge. Accept ends it too.
enum class OfferSource : std::uint8_t { Song, Keys };

inline constexpr int kMaxKeyboardShift = 36; // the detector's reach: three octaves either way

// `range` moved by `semitones` (what a keyboard sends when its octave button is on), none when a
// key would leave MIDI's 0..127.
std::optional<KeyRange> shifted_range(KeyRange range, int semitones);

struct ShiftOffer {
    // The song's offer: the total transposition proposed, and the one in force when it was raised.
    // The keys' offer: the total shift of the keyboard's notes proposed, and the one in force.
    int target = 0;
    int from = 0;
    OfferSource source = OfferSource::Song;
};

class ShiftOfferState {
  public:
    // A new song or keyboard: the offer and its dismissal are forgotten. `never_ask` is the saved
    // "don't ask again" for this song.
    void begin(bool never_ask);

    // The song's fits and the transposition in force, judged again every frame (cheap, no
    // allocation): raises the song's offer, changes it when the transposition moved under it, drops
    // it when it no longer helps. Leaves a card the keys raised alone.
    void judge_song(const ShiftFits& fits, int current);
    // The detector's latest result, with the shift of the keyboard's notes in force and the range
    // the app takes the keyboard to send now. A Shift raises the keys' offer; NoShift withdraws it;
    // NotSure changes nothing.
    void judge_keys(const ShiftResult& result, int current_shift, KeyRange keyboard);

    // The offer on the card now, none when no card shows.
    const std::optional<ShiftOffer>& offer() const { return offer_; }

    void accept() { settle(); }
    void dismiss() { settle(); }
    // Dismiss, and never raise it for this song again.
    void never() {
        never_ = true;
        settle();
    }
    // The player set a transposition by hand.
    void player_changed() { settle(); }

    bool never_ask() const { return never_; }
    // The saved choice arriving after the song opened (or going), without ending a dismissal.
    void set_never(bool never) {
        never_ = never;
        if (never) {
            offer_.reset();
        }
    }

  private:
    void settle() {
        offer_.reset();
        settled_ = true;
    }
    bool quiet() const { return never_ || settled_; }

    std::optional<ShiftOffer> offer_;
    bool settled_ = false;
    bool never_ = false;
};

} // namespace etude
