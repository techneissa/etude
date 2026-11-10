// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hand_split.hpp"

#include <algorithm>

namespace etude {

HandSplit capture_hands(const Song& song) {
    HandSplit out;
    out.fingerprint = song_fingerprint(song);
    out.note_hands.reserve(song.notes.size());
    out.note_accompaniment.reserve(song.notes.size());
    for (const Note& n : song.notes) {
        out.note_hands.push_back(n.hand);
        out.note_accompaniment.push_back(n.accompaniment);
    }
    out.part_hands.reserve(song.parts.size());
    out.part_accompaniment.reserve(song.parts.size());
    for (const Part& part : song.parts) {
        out.part_hands.push_back(part.hand);
        out.part_accompaniment.push_back(part.accompaniment);
    }
    return out;
}

bool apply_hand_split(Song& song, const HandSplit& split, bool verified) {
    if (split.note_hands.size() != song.notes.size() ||
        split.note_accompaniment.size() != song.notes.size() ||
        split.part_hands.size() != song.parts.size() ||
        split.part_accompaniment.size() != song.parts.size() ||
        (!verified && split.fingerprint != song_fingerprint(song))) {
        return false;
    }
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        song.notes[i].hand = split.note_hands[i];
        song.notes[i].accompaniment = split.note_accompaniment[i];
    }
    for (std::size_t i = 0; i < song.parts.size(); ++i) {
        Part& part = song.parts[i];
        part.accompaniment = split.part_accompaniment[i];
        if (!part.drum) {
            part.hand = split.part_hands[i];
        }
    }
    return true;
}


Song with_split_hands(const Song& song, const HandSplitOptions& options) {
    Song out = song;
    const bool applied = apply_hand_split(out, compute_hand_split(song, options), true);
    (void)applied; // made from this very song
    return out;
}

HandAgreement compare_hands(const Song& truth, std::span<const Hand> assigned) {
    HandAgreement out;
    std::size_t n = std::min(truth.notes.size(), assigned.size());
    for (std::size_t i = 0; i < n; ++i) {
        Hand want = truth.notes[i].hand;
        if (truth.notes[i].drum || want == Hand::Unknown) {
            continue;
        }
        ++out.compared;
        out.matching += assigned[i] == want ? 1 : 0;
    }
    return out;
}

} // namespace etude
