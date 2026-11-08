// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/drill.hpp"

#include "etude/content_hash.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace etude {

namespace {

// The semitones above the tonic a scale has: for the triad's third and fifth.
bool has(unsigned mask, int semitones) { return (mask >> semitones & 1u) != 0; }

std::string replace_all(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

// The exercise's own title, shortened for a card and a history line.
std::string short_title(std::string title, Direction direction) {
    title = replace_all(std::move(title), ", right hand", ", RH");
    title = replace_all(std::move(title), ", left hand", ", LH");
    title = replace_all(std::move(title), ", hands together", ", both hands");
    title = replace_all(std::move(title), " octaves", " oct");
    title = replace_all(std::move(title), " octave", " oct");
    if (direction == Direction::Down) {
        title += ", down";
    } else if (direction == Direction::UpDown) {
        title += ", up and down";
    }
    return title;
}

std::uint64_t drill_hash(const DrillSpec& spec) {
    char key[96];
    const int n = std::snprintf(key, sizeof key, "drill:%d:%d:%d:%d:%d:%d:%d:%d:%d",
                                static_cast<int>(spec.kind), spec.root_pc,
                                static_cast<int>(spec.scale),
                                spec.chord ? static_cast<int>(*spec.chord) : -1, spec.inversion,
                                spec.octaves, static_cast<int>(spec.direction),
                                static_cast<int>(spec.hands), spec.bpm);
    return content_hash(std::string_view(key, static_cast<std::size_t>(std::max(n, 0))));
}

} // namespace

std::optional<ChordQuality> scale_triad(ScaleType type) {
    unsigned mask = 0;
    for (const int s : scale_intervals(type)) {
        mask |= 1u << s;
    }
    const bool minor3 = has(mask, 3);
    const bool major3 = has(mask, 4);
    if (minor3 == major3) {
        return std::nullopt; // no third, or both (the chromatic)
    }
    // The perfect fifth where the scale has it, else the one the third asks for: sharp over a major
    // third (augmented), flat over a minor one (diminished).
    if (has(mask, 7)) {
        return major3 ? ChordQuality::Major : ChordQuality::Minor;
    }
    if (major3 && has(mask, 8)) {
        return ChordQuality::Augmented;
    }
    if (minor3 && has(mask, 6)) {
        return ChordQuality::Diminished;
    }
    return std::nullopt;
}

int drill_tempo(int bpm) {
    const int clamped = std::clamp(bpm, kDrillMinBpm, kDrillMaxBpm);
    const int steps = (clamped - kDrillMinBpm + kDrillBpmStep / 2) / kDrillBpmStep;
    return std::min(kDrillMaxBpm, kDrillMinBpm + steps * kDrillBpmStep);
}

Drill make_drill(const DrillSpec& spec, KeyRange range) {
    Drill d;
    DrillSpec s = spec;
    s.bpm = drill_tempo(s.bpm);
    s.octaves = std::clamp(s.octaves, 1, 2);
    ExerciseSettings settings;
    settings.range = range;
    settings.bpm = static_cast<double>(s.bpm);
    ExerciseResult r;
    if (s.kind == DrillKind::Scale) {
        ScaleSpec scale;
        scale.root_pc = s.root_pc;
        scale.type = s.scale;
        scale.octaves = s.octaves;
        scale.direction = s.direction;
        scale.hands = s.hands;
        r = make_scale_exercise(scale, settings);
    } else {
        ArpeggioSpec arpeggio;
        arpeggio.root_pc = s.root_pc;
        const std::optional<ChordQuality> quality = s.chord ? s.chord : scale_triad(s.scale);
        if (!quality) {
            d.message = std::string("The ") + scale_type_name(s.scale) +
                        " scale has no triad to play as an arpeggio.";
            return d;
        }
        arpeggio.quality = *quality;
        arpeggio.inversion = s.chord ? s.inversion : 0;
        arpeggio.octaves = s.octaves;
        arpeggio.direction = s.direction;
        arpeggio.hands = s.hands;
        r = make_arpeggio_exercise(arpeggio, settings);
    }
    if (!r.ok) {
        d.message = r.message.empty() ? "This does not fit the keyboard." : r.message;
        return d;
    }
    d.ok = true;
    d.title = short_title(std::move(r.title), s.direction);
    d.hash = drill_hash(s);
    d.song = std::move(r.song);
    return d;
}

} // namespace etude
