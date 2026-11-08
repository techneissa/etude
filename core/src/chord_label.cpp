// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_label.hpp"

#include "etude/accidental_text.hpp"

#include <algorithm>
#include <cstddef>

namespace etude {
namespace {

int wrap_pc(int pc) { return ((pc % 12) + 12) % 12; }

// A stretch's chord played `semitones` higher, spelled from the key at its start played that high.
// With no shift the analysis's own spelling stands (it chose the root of a diminished seventh).
ChordNameParts parts_of(const HarmonyStretch& s, const std::vector<KeyChange>& keys, int semitones,
                        bool prefer_flats) {
    if (semitones == 0) {
        return chord_name_parts(s.chord, s.spelling);
    }
    Chord chord = s.chord;
    chord.root_pc = wrap_pc(chord.root_pc + semitones);
    chord.bass_pc = wrap_pc(chord.bass_pc + semitones);
    const KeyChange* key = key_at_tick(keys, s.start_tick);
    // No key at all reads as C major, as the key label does.
    const KeyChange c_major = key_from_signature(0, 0, KeyMode::Major);
    const KeyChange played =
        transposed_key(key != nullptr ? *key : c_major, semitones, prefer_flats);
    return chord_name_parts(chord, spell_chord(chord, &played));
}

} // namespace

std::string ChordNameParts::display() const { return accidental_display(plain()); }

ChordNameParts chord_name_parts(const Chord& chord, const ChordSpelling& spelling) {
    ChordNameParts p;
    if (!chord.found) {
        return p;
    }
    p.root = line_name(spelling.root_line);
    p.quality = chord_suffix(chord.quality);
    if (chord.bass_pc != chord.root_pc) {
        p.bass = line_name(spelling.bass_line);
    }
    return p;
}

HandChordTimeline hand_chord_timeline(const std::vector<HarmonyStretch>& stretches,
                                      const std::vector<KeyChange>& keys, int semitones,
                                      bool prefer_flats) {
    HandChordTimeline out;
    // The stretch whose chord is on show; -1 for none. Unclear keeps the one before.
    int shown = -1;
    // The labels of the stretches as they stood before the transposition: two stretches whose
    // chord and keys are the same are one label.
    int last_low = -1;
    int last_high = -1;
    ChordNameParts last_name;
    bool last_on = false;
    for (std::size_t i = 0; i < stretches.size(); ++i) {
        const HarmonyStretch& s = stretches[i];
        if (s.state == HarmonyState::Chord) {
            shown = static_cast<int>(i);
        } else if (s.state == HarmonyState::NoChord) {
            shown = -1;
        } else if (shown >= 0 &&
                   s.start_s >= stretches[static_cast<std::size_t>(shown)].hold_end_s - 1e-9) {
            shown = -1; // a rest or a lone note keeps the name only a little way past the chord
        }
        if (shown < 0) {
            last_on = false;
            continue;
        }
        const HarmonyStretch& e = stretches[static_cast<std::size_t>(shown)];
        const ChordNameParts name = parts_of(e, keys, semitones, prefer_flats);
        // An unclear stretch holds the name only to the chord's hold: its end is cut there.
        const double end_s =
            s.state == HarmonyState::Chord ? s.end_s : std::min(s.end_s, e.hold_end_s);
        if (last_on && !out.empty() && out.back().end_s >= s.start_s - 1e-9 && name == last_name &&
            e.low_pitch == last_low && e.high_pitch == last_high) {
            out.back().end_s = end_s;
            continue;
        }
        HandChordLabel label;
        label.start_s = s.start_s;
        label.end_s = end_s;
        label.name = name;
        label.low_pitch = e.low_pitch;
        label.high_pitch = e.high_pitch;
        out.push_back(std::move(label));
        last_on = true;
        last_name = name;
        last_low = e.low_pitch;
        last_high = e.high_pitch;
    }
    return out;
}

HandChords hand_chords_of(const HarmonyTimeline& harmony, const std::vector<KeyChange>& keys,
                          int semitones, bool prefer_flats) {
    HandChords out;
    out.left = hand_chord_timeline(harmony.left, keys, semitones, prefer_flats);
    out.right = hand_chord_timeline(harmony.right, keys, semitones, prefer_flats);
    return out;
}

HandChords build_hand_chords(const Song& song, int semitones, bool prefer_flats) {
    return hand_chords_of(build_hand_harmony(song), song.keys, semitones, prefer_flats);
}

std::optional<ChordKeys> chord_label_keys(const HandChordLabel& label, const Transposition& shown,
                                          int first_visible, int last_visible) {
    if (label.low_pitch < 0 || label.high_pitch < label.low_pitch || first_visible > last_visible) {
        return std::nullopt;
    }
    const TransposedPitch a = shown.map(label.low_pitch);
    const TransposedPitch b = shown.map(label.high_pitch);
    if (!a.playable || !b.playable) {
        return std::nullopt;
    }
    ChordKeys keys{std::min(a.pitch, b.pitch), std::max(a.pitch, b.pitch)};
    if (keys.high < first_visible || keys.low > last_visible) {
        return std::nullopt;
    }
    keys.low = std::max(keys.low, first_visible);
    keys.high = std::min(keys.high, last_visible);
    return keys;
}

int chord_label_at(const HandChordTimeline& timeline, double seconds) {
    auto it = std::upper_bound(timeline.begin(), timeline.end(), seconds,
                               [](double t, const HandChordLabel& l) { return t < l.start_s; });
    if (it == timeline.begin()) {
        return -1;
    }
    --it;
    return seconds < it->end_s ? static_cast<int>(it - timeline.begin()) : -1;
}

} // namespace etude
