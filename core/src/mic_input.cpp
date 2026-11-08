// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/mic_input.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

float unsure_marker_alpha(std::int64_t age_ns) {
    if (age_ns < 0) {
        return 1.0f;
    }
    if (age_ns <= kUnsureHoldNs) {
        return 1.0f;
    }
    const std::int64_t into_fade = age_ns - kUnsureHoldNs;
    if (into_fade >= kUnsureFadeNs) {
        return 0.0f;
    }
    const float t = static_cast<float>(into_fade) / static_cast<float>(kUnsureFadeNs);
    return (1.0f - t) * (1.0f - t);
}

int unsure_marker_note(int guess, int last_heard, const KeyRange& range) {
    int note = guess >= 0 ? guess : last_heard;
    if (note < 0) {
        note = (range.lowest + range.highest) / 2;
    }
    return std::clamp(note, range.lowest, range.highest);
}

float mic_meter_fraction(float rms) {
    if (!(rms > 0.0f)) {
        return 0.0f;
    }
    const float db = 20.0f * std::log10(rms);
    return std::clamp((db + 60.0f) / 54.0f, 0.0f, 1.0f);
}

int MicNoteBridge::press(int note, std::int64_t time_ns, bool timed_release, MicOut* out, int n) {
    if (note < 0 || note > 127) {
        return n;
    }
    const auto i = static_cast<std::size_t>(note);
    const std::int64_t after_midi = time_ns - midi_press_ns_[i];
    if (after_midi <= kMicMergeNs && after_midi >= -kMicMergeBeforeNs) {
        // The keyboard already played it. This wins even when the microphone still holds the note
        // from an earlier strike: a release and a new press from the microphone would end the
        // keyboard's key and strike it a second time. The keyboard owns the note from here.
        merged_[i] = true;
        down_[i] = false;
        release_at_ns_[i] = kNever;
        return n;
    }
    if (down_[i]) {
        out[n++] = {MicOutKind::NoteOff, note, time_ns};
    }
    out[n++] = {MicOutKind::NoteOn, note, time_ns};
    down_[i] = true;
    mic_press_ns_[i] = time_ns;
    merged_[i] = false;
    release_at_ns_[i] = timed_release ? time_ns + kMicExpectedHoldNs : kNever;
    return n;
}

int MicNoteBridge::apply(const MicEventIn& in, MicOut* out) {
    const PitchEvent& e = in.event;
    int n = 0;
    switch (e.type) {
    case PitchEventType::NoteOn:
        if (learning_ && e.confidence >= kLearnMinConfidence) {
            ++confident_notes_;
        }
        return press(e.note, in.onset_ns, false, out, n);
    case PitchEventType::NoteOff: {
        if (e.note < 0 || e.note > 127) {
            return 0;
        }
        const auto i = static_cast<std::size_t>(e.note);
        if (merged_[i]) {
            merged_[i] = false;
            return 0;
        }
        if (!down_[i]) {
            return 0;
        }
        down_[i] = false;
        release_at_ns_[i] = kNever;
        if (keyboard_owns(i)) {
            return 0; // the keyboard pressed it since: this release is not the keyboard's
        }
        out[n++] = {MicOutKind::NoteOff, e.note, in.sample_ns};
        return n;
    }
    case PitchEventType::NotSure:
        out[n++] = {MicOutKind::Unsure, e.guess_note, in.onset_ns};
        return n;
    case PitchEventType::Expected:
        break;
    }
    // Expected-note mode.
    const int count = std::clamp(in.expected_count, 0, kMaxChordNotes);
    if (count > 1) {
        bool all_known = true;
        for (int k = 0; k < count; ++k) {
            const bool present = (e.chord_present >> k) & 1u;
            const bool absent = (e.chord_absent >> k) & 1u;
            if (present) {
                n = press(in.expected[static_cast<std::size_t>(k)], in.onset_ns, true, out, n);
            } else if (!absent) {
                all_known = false;
            }
        }
        if (!all_known) {
            out[n++] = {MicOutKind::Unsure, e.guess_note, in.onset_ns};
        }
        return n;
    }
    switch (e.verdict) {
    case ExpectedVerdict::Expected:
        return press(e.note, in.onset_ns, true, out, n);
    case ExpectedVerdict::NeighborBelow:
    case ExpectedVerdict::NeighborAbove:
    case ExpectedVerdict::OctaveBelow:
    case ExpectedVerdict::OctaveAbove:
        if (tuning_settled()) {
            return press(e.note, in.onset_ns, true, out, n);
        }
        // The heard note only marks the register: it is not a key until the tuning is trusted.
        out[n++] = {MicOutKind::Unsure, e.note, in.onset_ns};
        return n;
    case ExpectedVerdict::NotSure:
        break;
    }
    out[n++] = {MicOutKind::Unsure, e.guess_note, in.onset_ns};
    return n;
}

int MicNoteBridge::release_due(std::int64_t now_ns, MicOut* out, int capacity) {
    int n = 0;
    for (int note = 0; note < 128 && n < capacity; ++note) {
        const auto i = static_cast<std::size_t>(note);
        if (down_[i] && release_at_ns_[i] != kNever && release_at_ns_[i] <= now_ns) {
            if (!keyboard_owns(i)) {
                out[n++] = {MicOutKind::NoteOff, note, release_at_ns_[i]};
            }
            down_[i] = false;
            release_at_ns_[i] = kNever;
        }
    }
    return n;
}

bool MicNoteBridge::keyboard_owns(std::size_t i) const {
    return midi_press_ns_[i] != kNever && midi_press_ns_[i] >= mic_press_ns_[i];
}

void MicNoteBridge::reset_tuning(bool learning) {
    learning_ = learning;
    confident_notes_ = 0;
}

void MicNoteBridge::set_learning_tuning(bool on) {
    if (on && !learning_) {
        confident_notes_ = 0;
    }
    learning_ = on;
}

void MicNoteBridge::note_midi_press(int note, std::int64_t time_ns) {
    if (note >= 0 && note < 128) {
        midi_press_ns_[static_cast<std::size_t>(note)] = time_ns;
    }
}

int MicNoteBridge::release_all(std::int64_t now_ns, MicOut* out, int capacity) {
    int n = 0;
    for (int note = 0; note < 128 && n < capacity; ++note) {
        const auto i = static_cast<std::size_t>(note);
        if (down_[i] && !keyboard_owns(i)) {
            out[n++] = {MicOutKind::NoteOff, note, now_ns};
        }
        down_[i] = false;
        merged_[i] = false;
        release_at_ns_[i] = kNever;
    }
    return n;
}

} // namespace etude
