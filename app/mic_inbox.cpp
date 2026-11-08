// SPDX-License-Identifier: GPL-3.0-or-later
#include "mic_inbox.hpp"

namespace etude::app {

void MicInbox::release_all(std::int64_t now_ns) {
    MicOut outs[128]; // one per key
    const int n = bridge_.release_all(now_ns, outs, 128);
    std::vector<UnsureMark> none;
    for (int i = 0; i < n; ++i) {
        emit(outs[i], now_ns, released_, none);
    }
}

void MicInbox::flush(std::vector<MidiEvent>& events) {
    events.insert(events.end(), released_.begin(), released_.end());
    released_.clear();
}

void MicInbox::emit(const MicOut& o, std::int64_t now_ns, std::vector<MidiEvent>& events,
                    std::vector<UnsureMark>& marks) {
    MidiEvent ev;
    ev.time_ns = o.time_ns;
    ev.source = InputSource::Microphone;
    switch (o.kind) {
    case MicOutKind::NoteOn:
        ev.type = MidiEventType::NoteOn;
        ev.data1 = static_cast<std::uint8_t>(o.note);
        ev.data2 = static_cast<std::uint8_t>(kMicVelocity);
        last_note_ = o.note;
        pending_onsets_.push_back(o.time_ns);
        events.push_back(ev);
        break;
    case MicOutKind::NoteOff:
        ev.type = MidiEventType::NoteOff;
        ev.data1 = static_cast<std::uint8_t>(o.note);
        events.push_back(ev);
        break;
    case MicOutKind::Unsure:
        ++unsure_seen_;
        marks.push_back({o.note, last_note_, now_ns, o.time_ns});
        break;
    }
}

} // namespace etude::app
