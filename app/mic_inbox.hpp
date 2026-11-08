// SPDX-License-Identifier: GPL-3.0-or-later
// The microphone's detected notes as key events the frame can take at any moment. It reads the
// analysis thread's pitch events when asked (the thread's own queue holds them until then),
// turns them into MidiEvents (tagged InputSource::Microphone, stamped at the onset) and "not
// sure" markers, and holds only the releases of keys the microphone was holding when it closed.
// No SDL and no device here, so a test drives it with a fake source. Main thread only.
//
// Taking is the only thing that reads the analysis thread's queue, and it is safe to do at any
// point of a frame (it touches nothing but this object and the lists it is given), so the host
// takes from it whenever it gathers input: before every action on the song, not only at the
// frame's drain. That is what puts a press stamped before a seek, a loop wrap or a song switch
// in the song's inbox before the change is applied.
#pragma once

#include "etude/mic_input.hpp"
#include "etude/midi_event.hpp"

#include <cstdint>
#include <vector>

namespace etude::app {

// A "not sure" event to show as a soft "?": over the guessed note's key, else the last note
// heard (see unsure_marker_note).
struct UnsureMark {
    int guess = -1;
    int last_heard = -1;
    std::int64_t at_ns = 0;    // when it was shown, for the fade
    std::int64_t onset_ns = 0; // the onset it belongs to, for grading
};

class MicInbox {
  public:
    // Turns the pitch events `pop` yields (`bool pop(MicEventIn&)`, until it returns false) into
    // key events and appends them, and the "not sure" markers, to `events` and `marks`; then the
    // timed releases due by `now_ns`. Events come out in the order the detector decided them.
    template <class Pop>
    void take(Pop&& pop, std::int64_t now_ns, std::vector<MidiEvent>& events,
              std::vector<UnsureMark>& marks) {
        flush(events);
        MicOut outs[MicNoteBridge::kMaxOut];
        for (MicEventIn in; pop(in);) {
            const int n = bridge_.apply(in, outs);
            for (int i = 0; i < n; ++i) {
                emit(outs[i], now_ns, events, marks);
            }
        }
        const int n = bridge_.release_due(now_ns, outs, MicNoteBridge::kMaxOut);
        for (int i = 0; i < n; ++i) {
            emit(outs[i], now_ns, events, marks);
        }
    }

    // The microphone stops (the input switched away, the device changed): a NoteOff stamped
    // `now_ns` for every key it holds, handed out by the next take() or flush(). Nothing is
    // dropped, so no key is left down.
    void release_all(std::int64_t now_ns);
    // Appends the releases release_all() made; for a take with no source to read.
    void flush(std::vector<MidiEvent>& events);

    MicNoteBridge& bridge() { return bridge_; }
    // The last note the microphone confirmed, -1 for none.
    int last_note() const { return last_note_; }
    std::uint64_t unsure_seen() const { return unsure_seen_; }
    // Onsets of the notes taken since --mic-log last counted them (MicSource::log_presented).
    std::vector<std::int64_t>& pending_onsets() { return pending_onsets_; }

  private:
    void emit(const MicOut& o, std::int64_t now_ns, std::vector<MidiEvent>& events,
              std::vector<UnsureMark>& marks);

    MicNoteBridge bridge_;
    std::vector<MidiEvent> released_;
    int last_note_ = -1;
    std::vector<std::int64_t> pending_onsets_;
    std::uint64_t unsure_seen_ = 0;
};

} // namespace etude::app
