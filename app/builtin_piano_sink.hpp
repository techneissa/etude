// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "synth_sink.hpp"
#include "etude/playback.hpp"
#include "etude/sound_sink.hpp"

#include <cstddef>

namespace etude::app {

// The built-in piano as a core SoundSink, so taps and typing sound when no piano is connected.
// The built-in piano has one channel, so the channel is ignored.
class BuiltinPianoSink final : public etude::SoundSink {
  public:
    explicit BuiltinPianoSink(app::SynthSink& synth) : synth_(synth) {}

    void note_on(int, int note, int velocity) override { synth_.note_on(note, velocity); }
    void note_off(int, int note) override { synth_.note_off(note); }
    void sustain(int, bool down) override { synth_.sustain(down); }
    void all_off() override { synth_.all_notes_off(); }

  private:
    app::SynthSink& synth_;
};

// The song's sound (SongController) on the built-in piano, when no piano is connected. The synth
// starts each note on the sample its due time falls on and keeps the record of the song's notes
// itself, so a stop affects exactly those, not the player's own. One channel: the channel is
// ignored, program changes and bends too.
class BuiltinSongSink {
  public:
    explicit BuiltinSongSink(app::SynthSink& synth) : synth_(synth) {}

    void submit(const etude::SongSoundEvent& event) { synth_.song_event(event); }
    // Every song note off, the pedal up, and nothing left scheduled.
    void all_off() { synth_.song_event({etude::SongSoundEvent::Kind::StopAll, {}}); }

  private:
    app::SynthSink& synth_;
};

} // namespace etude::app
