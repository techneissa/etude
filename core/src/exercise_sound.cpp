// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/exercise_sound.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

std::int64_t at_ns(std::int64_t start_ns, double seconds) {
    return start_ns + static_cast<std::int64_t>(std::llround(seconds * 1e9));
}

} // namespace

std::vector<SongSoundEvent> exercise_sound_events(const Song& song, std::int64_t start_ns) {
    std::vector<SongSoundEvent> events;
    if (song.notes.size() * 2 > kMaxExerciseMessages) {
        return events;
    }
    events.reserve(song.notes.size() * 2);
    for (const Note& n : song.notes) {
        if (n.drum) {
            continue;
        }
        SongSoundEvent on;
        on.message.kind = PlaybackMessage::Kind::NoteOn;
        on.message.channel = 0;
        on.message.data = n.pitch;
        on.message.value = n.velocity;
        on.message.at_ns = at_ns(start_ns, n.start_s);
        on.message.song_s = n.start_s;
        SongSoundEvent off = on;
        off.message.kind = PlaybackMessage::Kind::NoteOff;
        off.message.value = 0;
        off.message.at_ns = at_ns(start_ns, n.start_s + std::max(n.duration_s, kMinNoteSeconds));
        off.message.song_s = n.start_s + std::max(n.duration_s, kMinNoteSeconds);
        events.push_back(on);
        events.push_back(off);
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const SongSoundEvent& a, const SongSoundEvent& b) {
                         if (a.message.at_ns != b.message.at_ns) {
                             return a.message.at_ns < b.message.at_ns;
                         }
                         return ends_note(a.message) && starts_note(b.message);
                     });
    return events;
}

double exercise_length_s(const Song& song) {
    double first = 0.0;
    double last = 0.0;
    bool any = false;
    for (const Note& n : song.notes) {
        if (n.drum) {
            continue;
        }
        const double end = n.start_s + std::max(n.duration_s, kMinNoteSeconds);
        first = any ? std::min(first, n.start_s) : n.start_s;
        last = any ? std::max(last, end) : end;
        any = true;
    }
    return any ? last - first : 0.0;
}

std::vector<ExerciseSpan> exercise_spans(const Song& song) {
    std::vector<ExerciseSpan> spans;
    spans.reserve(song.notes.size());
    int lowest[3] = {127, 127, 127}; // by Hand
    for (const Note& n : song.notes) {
        if (n.drum) {
            continue;
        }
        const std::size_t h = static_cast<std::size_t>(n.hand);
        lowest[h] = std::min(lowest[h], static_cast<int>(n.pitch));
        ExerciseSpan s;
        s.pitch = n.pitch;
        s.hand = n.hand;
        s.finger = n.finger;
        s.start_s = n.start_s;
        s.end_s = n.start_s + std::max(n.duration_s, kMinNoteSeconds);
        spans.push_back(s);
    }
    for (ExerciseSpan& s : spans) {
        s.semitone = static_cast<int>(s.pitch) - lowest[static_cast<std::size_t>(s.hand)];
    }
    return spans;
}

std::size_t spans_sounding_at(std::span<const ExerciseSpan> spans, double t,
                              std::span<std::uint16_t> out) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < spans.size() && n < out.size(); ++i) {
        if (spans[i].sounding_at(t)) {
            out[n++] = static_cast<std::uint16_t>(i);
        }
    }
    return n;
}

} // namespace etude
