// SPDX-License-Identifier: GPL-3.0-or-later
// What the Learn screen needs of the app: the sound of a drill, and the player to practise it in.
// The frame loop binds it (app/learn_sound.*), so the screen never sees the audio, the MIDI output or
// the song controller.
#pragma once

#include "../melody_book.hpp"

#include "etude/drill.hpp"
#include "etude/ear_training.hpp"
#include "etude/exercise_sound.hpp"

#include <cstddef>
#include <span>
#include <string>

namespace etude::app::ui {

class LearnPort {
  public:
    virtual ~LearnPort() = default;

    // Listen: plays the drill on the piano the player hears (the built-in piano, or the keyboard's
    // own when it is connected). Returns its length in seconds, or 0 when it cannot be played (the
    // keyboard is too small for it, or the scale has no arpeggio) with the reason in `why`; it stops
    // what an earlier call was playing.
    virtual double listen(const DrillSpec& spec, std::string& why) = 0;
    // When each note of the drill `listen` started sounds, in seconds from the sound's start (a
    // look-ahead `kSongSoundLeadNs` after the call), so the screen can light the keys as they sound.
    // Empty before the first call; the last drill's after a stop.
    virtual std::span<const ExerciseSpan> listening() const = 0;
    // Silences what `listen` is playing; harmless when nothing is.
    virtual void stop_sound() = 0;
    // Practice: asks the app to open the drill in the normal player, notes falling with their
    // fingers, graded like a song. True when it is accepted; else `why` says what cannot be played.
    // The screen closes itself once it is.
    virtual bool practise(const DrillSpec& spec, std::string& why) = 0;
    // Ear: sounds the question (a note, or two notes one after the other or together) on the same
    // piano, stopping what was playing. Returns its length in seconds, or 0 with the reason in `why`.
    virtual double ask_ear(const EarQuestion& question, std::string& why) = 0;
    // A key the player tapped on the Ear card, sounded for a moment over whatever plays.
    virtual void sound_key(int note) = 0;
    // Melodies: how many paths and pieces the manifest lists, from its text alone (no file is read),
    // for the gallery's tile.
    struct MelodyCounts {
        int paths = 0;
        int pieces = 0;
    };
    virtual MelodyCounts melody_counts() = 0;
    // The bundled pieces and the paths through them with the best scores the practice history holds,
    // read from the files the first time the Melodies page asks (a few small files, once) and kept
    // after that.
    virtual const MelodyBook& melodies() = 0;
    // Opens piece `piece` (a place in `melodies().pieces`) in the normal player, as Practice does a
    // drill. True when it is accepted; else `why` says what went wrong. The screen closes itself
    // once it is.
    virtual bool play_melody(std::size_t piece, std::string& why) = 0;
};

} // namespace etude::app::ui
