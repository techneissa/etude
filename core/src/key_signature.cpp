// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/key_signature.hpp"

#include "etude/accidental_text.hpp"
#include "etude/scales.hpp"
#include "etude/song.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace etude {

namespace {

constexpr int kMaxFifths = 7;

// Semitones from a mode's tonic down to the major key that shares its signature.
constexpr int kModeSemitonesAboveMajor[kKeyModeCount] = {0, 9, 2, 4, 5, 7, 11};
// The line-of-fifths shift of each mode, see mode_line_shift.
constexpr int kModeShift[kKeyModeCount] = {0, 3, 2, 4, -1, 1, 5};
// Signature of the major key on each pitch class, fewest accidentals; F# (6) and Gb (-6) tie.
constexpr int kMajorFifths[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};

int mod12(int v) { return ((v % 12) + 12) % 12; }

// The signature with the fewest accidentals among those equal to `f` modulo 12; of two with six,
// the flats when asked or when the tonic is a flat black key (the scales' rule, scales.hpp).
int nearest_signature(int f, int tonic_pc, bool prefer_flats) {
    const int sharp_side = mod12(f); // 0..11
    const int flat_side = sharp_side - 12;
    if (sharp_side == 6) {
        return prefer_flats || tie_prefers_flats(tonic_pc) ? flat_side : sharp_side;
    }
    return std::abs(sharp_side) <= std::abs(flat_side) ? sharp_side : flat_side;
}

int pitch_class_of_line(int line) { return mod12(line * 7); }

const char* const kModeWords[kKeyModeCount] = {"major",  "minor",      "dorian", "phrygian",
                                               "lydian", "mixolydian", "locrian"};

// The tonic's letter and accidental from its place on the line of fifths (F -1, C 0 ... B 5): the
// line runs F C G D A E B over and over, a sharp more each lap going right, a flat going left.
// False past one accidental, where a double sharp or flat would be needed.
bool spell_line(int line, char& letter, int& accidental) {
    constexpr char kLetters[7] = {'F', 'C', 'G', 'D', 'A', 'E', 'B'};
    const int shifted = line + 1;
    accidental = shifted >= 0 ? shifted / 7 : -((-shifted + 6) / 7);
    if (accidental < -1 || accidental > 1) {
        return false;
    }
    letter = kLetters[((shifted % 7) + 7) % 7];
    return true;
}

// The same pitch class spelled plainly, for a tonic the line cannot spell.
void spell_pitch_class(int pc, bool flats, char& letter, int& accidental) {
    constexpr char kSharpLetters[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
    constexpr char kFlatLetters[12] = {'C', 'D', 'D', 'E', 'E', 'F', 'G', 'G', 'A', 'A', 'B', 'B'};
    constexpr bool kBlack[12] = {false, true, false, true, false, false,
                                 true,  false, true,  false, true,  false};
    pc = mod12(pc);
    letter = flats ? kFlatLetters[pc] : kSharpLetters[pc];
    accidental = kBlack[pc] ? (flats ? -1 : 1) : 0;
}

const char* solfege_of(char letter) {
    switch (letter) {
    case 'C':
        return "Do";
    case 'D':
        return "Re";
    case 'E':
        return "Mi";
    case 'F':
        return "Fa";
    case 'G':
        return "Sol";
    case 'A':
        return "La";
    default:
        return "Si";
    }
}

// Krumhansl and Kessler's probe-tone profiles, C major and C minor.
constexpr double kMajorProfile[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                      2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinorProfile[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                      2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

// Pearson correlation of `w` with `profile` turned so its first value lands on `tonic`.
double correlation(const PitchClassWeights& w, const double (&profile)[12], int tonic,
                   double w_mean, double w_norm) {
    double p_mean = 0.0;
    for (double v : profile) {
        p_mean += v;
    }
    p_mean /= 12.0;
    double dot = 0.0;
    double p_norm = 0.0;
    for (int pc = 0; pc < 12; ++pc) {
        const double p = profile[mod12(pc - tonic)] - p_mean;
        dot += (w[static_cast<std::size_t>(pc)] - w_mean) * p;
        p_norm += p * p;
    }
    const double denom = w_norm * std::sqrt(p_norm);
    return denom > 0.0 ? dot / denom : 0.0;
}

} // namespace

int mode_line_shift(KeyMode mode) {
    const auto i = static_cast<std::size_t>(mode);
    return i < kKeyModeCount ? kModeShift[i] : 0;
}

KeyMode mode_from_line_shift(int shift) {
    for (int m = 0; m < kKeyModeCount; ++m) {
        if (kModeShift[m] == shift) {
            return static_cast<KeyMode>(m);
        }
    }
    return KeyMode::Major;
}

const char* mode_word(KeyMode mode) {
    const auto i = static_cast<std::size_t>(mode);
    return i < kKeyModeCount ? kModeWords[i] : kModeWords[0];
}

bool key_mode_from_name(std::string_view name, KeyMode& mode) {
    std::string lower;
    for (char c : name) {
        lower += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    struct Entry {
        const char* word;
        KeyMode mode;
    };
    constexpr Entry kNames[] = {
        {"major", KeyMode::Major},     {"ionian", KeyMode::Major},
        {"minor", KeyMode::Minor},     {"aeolian", KeyMode::Minor},
        {"dorian", KeyMode::Dorian},   {"phrygian", KeyMode::Phrygian},
        {"lydian", KeyMode::Lydian},   {"mixolydian", KeyMode::Mixolydian},
        {"locrian", KeyMode::Locrian},
    };
    for (const Entry& e : kNames) {
        if (lower == e.word) {
            mode = e.mode;
            return true;
        }
    }
    return false;
}

KeyChange key_from_signature(std::int64_t tick, int fifths, KeyMode mode) {
    KeyChange k;
    k.tick = tick;
    fifths = std::clamp(fifths, -kMaxFifths, kMaxFifths);
    k.fifths = static_cast<std::int8_t>(fifths);
    k.mode = mode;
    k.tonic = static_cast<std::uint8_t>(pitch_class_of_line(fifths + mode_line_shift(mode)));
    return k;
}

KeyChange key_from_pitch_class(std::int64_t tick, int tonic_pc, KeyMode mode, bool prefer_flats) {
    const int major_pc = mod12(tonic_pc - kModeSemitonesAboveMajor[static_cast<std::size_t>(mode)]);
    int fifths = kMajorFifths[major_pc];
    if (major_pc == 6 && (prefer_flats || tie_prefers_flats(tonic_pc))) {
        fifths = -6;
    }
    KeyChange k = key_from_signature(tick, fifths, mode);
    k.tonic = static_cast<std::uint8_t>(mod12(tonic_pc));
    return k;
}

KeyChange transposed_key(const KeyChange& key, int semitones, bool prefer_flats) {
    if (semitones == 0) {
        return key;
    }
    KeyChange k = key;
    k.tonic = static_cast<std::uint8_t>(mod12(key.tonic + semitones));
    k.fifths = static_cast<std::int8_t>(
        nearest_signature(key.fifths + 7 * semitones, k.tonic, prefer_flats));
    return k;
}

const KeyChange* key_at_tick(const std::vector<KeyChange>& keys, std::int64_t tick) {
    if (keys.empty()) {
        return nullptr;
    }
    auto it = std::upper_bound(keys.begin(), keys.end(), tick,
                               [](std::int64_t t, const KeyChange& k) { return t < k.tick; });
    return it == keys.begin() ? &keys.front() : &*(it - 1);
}

int key_index_at_seconds(const std::vector<KeyChange>& keys, double seconds) {
    if (keys.empty()) {
        return -1;
    }
    auto it = std::upper_bound(keys.begin(), keys.end(), seconds,
                               [](double s, const KeyChange& k) { return s < k.seconds; });
    return it == keys.begin() ? 0 : static_cast<int>(it - keys.begin()) - 1;
}

const KeyChange* key_at_seconds(const std::vector<KeyChange>& keys, double seconds) {
    const int i = key_index_at_seconds(keys, seconds);
    return i < 0 ? nullptr : &keys[static_cast<std::size_t>(i)];
}

KeyLabelParts key_label_parts(const KeyChange& key, bool solfege, int transpose_semitones,
                              bool prefer_flats) {
    const KeyChange k = transposed_key(key, transpose_semitones, prefer_flats);
    char letter = 'C';
    int accidental = 0;
    if (!spell_line(k.fifths + mode_line_shift(k.mode), letter, accidental)) {
        spell_pitch_class(k.tonic, k.fifths < 0, letter, accidental);
    }
    KeyLabelParts out;
    out.tonic = solfege ? std::string(solfege_of(letter)) : std::string(1, letter);
    out.accidental = accidental > 0   ? AccidentalKind::Sharp
                     : accidental < 0 ? AccidentalKind::Flat
                                      : AccidentalKind::None;
    out.mode = mode_word(k.mode);
    out.signature_count = std::abs(static_cast<int>(k.fifths));
    out.signature_kind = k.fifths > 0   ? AccidentalKind::Sharp
                         : k.fifths < 0 ? AccidentalKind::Flat
                                        : AccidentalKind::None;
    return out;
}

bool key_spells_flats(const KeyChange* key, int transpose_semitones) {
    return key != nullptr && transposed_key(*key, transpose_semitones, false).fifths < 0;
}

std::string key_label_text(const KeyLabelParts& parts) {
    std::string s = parts.tonic;
    if (parts.accidental == AccidentalKind::Sharp) {
        s += '#';
    } else if (parts.accidental == AccidentalKind::Flat) {
        s += 'b';
    }
    s += ' ';
    s += parts.mode;
    if (parts.signature_count > 0) {
        s += ' ';
        s += std::to_string(parts.signature_count);
        s += parts.signature_kind == AccidentalKind::Sharp ? '#' : 'b';
    }
    return s;
}

KeyEstimate estimate_key(const PitchClassWeights& weights) {
    KeyEstimate e;
    double sum = 0.0;
    int sounding = 0;
    for (double v : weights) {
        if (!(v >= 0.0)) {
            return e;
        }
        sum += v;
        sounding += v > 0.0 ? 1 : 0;
    }
    if (!(sum > 0.0) || sounding < kMinPitchClassesForKey) {
        return e; // one or two pitch classes name no key
    }
    const double mean = sum / 12.0;
    double norm2 = 0.0;
    for (double v : weights) {
        norm2 += (v - mean) * (v - mean);
    }
    if (!(norm2 > 0.0)) {
        return e; // every pitch class equal: no key leads
    }
    const double norm = std::sqrt(norm2);

    double best = -2.0;
    double second = -2.0;
    int best_tonic = 0;
    KeyMode best_mode = KeyMode::Major;
    for (int mode = 0; mode < 2; ++mode) {
        const double(&profile)[12] = mode == 0 ? kMajorProfile : kMinorProfile;
        for (int tonic = 0; tonic < 12; ++tonic) {
            const double r = correlation(weights, profile, tonic, mean, norm);
            if (mode == 0 && tonic == 0) {
                e.c_major_correlation = static_cast<float>(r);
            }
            if (r > best) {
                second = best;
                best = r;
                best_tonic = tonic;
                best_mode = mode == 0 ? KeyMode::Major : KeyMode::Minor;
            } else if (r > second) {
                second = r;
            }
        }
    }
    e.valid = true;
    e.tonic = static_cast<std::uint8_t>(best_tonic);
    e.mode = best_mode;
    e.correlation = static_cast<float>(best);
    e.margin = static_cast<float>(best - second);
    return e;
}

PitchClassWeights pitch_class_weights(const Song& song) {
    PitchClassWeights w{};
    for (const Note& n : song.notes) {
        if (!n.drum) {
            w[n.pitch % 12] += static_cast<double>(std::max<std::int64_t>(n.end_tick - n.start_tick, 1));
        }
    }
    return w;
}

KeyEstimate estimate_song_key(const Song& song) {
    int pitched = 0;
    for (const Note& n : song.notes) {
        pitched += n.drum ? 0 : 1;
    }
    if (pitched < kMinNotesForKey) {
        return {};
    }
    return estimate_key(pitch_class_weights(song));
}

void complete_keys(Song& song) {
    std::stable_sort(song.keys.begin(), song.keys.end(),
                     [](const KeyChange& a, const KeyChange& b) { return a.tick < b.tick; });
    std::vector<KeyChange> merged;
    merged.reserve(song.keys.size());
    for (const KeyChange& k : song.keys) {
        if (!merged.empty() && merged.back().tick == k.tick) {
            merged.back() = k; // two events at one tick: the later one is in force
        } else {
            merged.push_back(k);
        }
    }
    song.keys.clear();
    for (const KeyChange& k : merged) {
        if (song.keys.empty() || song.keys.back().tonic != k.tonic ||
            song.keys.back().mode != k.mode || song.keys.back().fifths != k.fifths) {
            song.keys.push_back(k);
        }
    }

    const bool none = song.keys.empty();
    const bool placeholder = song.keys.size() == 1 && song.keys[0].fifths == 0 &&
                             song.keys[0].mode == KeyMode::Major && !song.keys[0].estimated;
    if (none || placeholder) {
        const KeyEstimate e = estimate_song_key(song);
        const bool is_c_major = e.tonic == 0 && e.mode == KeyMode::Major;
        const bool notes_disagree =
            e.valid && !is_c_major && e.correlation - e.c_major_correlation >= kPlaceholderMargin;
        if (e.valid && (none || notes_disagree)) {
            KeyChange k = key_from_pitch_class(0, e.tonic, e.mode);
            k.estimated = true;
            k.confidence = std::clamp(e.correlation, 0.0f, 1.0f);
            song.keys.assign(1, k);
        }
    }
    for (KeyChange& k : song.keys) {
        k.seconds = song.tempo.seconds_at(k.tick);
    }
}

} // namespace etude
