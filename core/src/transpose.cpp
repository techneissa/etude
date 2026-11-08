// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/transpose.hpp"

#include <algorithm>

namespace etude {
namespace {

constexpr int kOctave = 12;
constexpr int kMidiLowest = 0;
constexpr int kMidiHighest = 127;

} // namespace

Transposition::Transposition(int semitones, KeyRange keyboard)
    : semitones_(std::clamp(semitones, -kMaxTransposeSemitones, kMaxTransposeSemitones)),
      keyboard_(keyboard) {}

TransposedPitch Transposition::map(int song_pitch) const {
    // A note number MIDI can't send is never an answer, whatever range was given.
    const int lowest = std::max(keyboard_.lowest, kMidiLowest);
    const int highest = std::min(keyboard_.highest, kMidiHighest);
    int pitch = song_pitch + semitones_;
    int octaves = 0;
    if (pitch < lowest) {
        octaves = (lowest - pitch + kOctave - 1) / kOctave;
    } else if (pitch > highest) {
        octaves = -((pitch - highest + kOctave - 1) / kOctave);
    }
    pitch += octaves * kOctave;
    if (pitch < lowest || pitch > highest) {
        // The fewest octaves that clear one edge overshoot the other: no key has this pitch class.
        return {-1, false, 0, false};
    }
    return {pitch, octaves != 0, octaves, true};
}

TransposedPitch Transposition::map_note(int song_pitch, bool drum) const {
    return drum ? TransposedPitch{song_pitch, false, 0, true} : map(song_pitch);
}

std::optional<KeyRange> drawn_key_range(std::span<const Note> notes, const Transposition& t) {
    int lo = 128;
    int hi = -1;
    for (const Note& n : notes) {
        if (n.drum || n.accompaniment) {
            continue;
        }
        const TransposedPitch p = t.map(n.pitch);
        if (p.playable) {
            lo = std::min(lo, p.pitch);
            hi = std::max(hi, p.pitch);
        }
    }
    if (hi < 0) {
        return std::nullopt;
    }
    return KeyRange{lo, hi};
}

} // namespace etude
