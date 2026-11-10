// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/look.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "etude/palette.hpp"
#include "etude/pitch.hpp"
#include "etude/saved_palettes.hpp"

namespace etude {
namespace {

constexpr float kBlackKeyDarken = 0.35f;
// High contrast darkens less, so a black-key note can still reach `kHighContrastMinimum`.
constexpr float kBlackKeyDarkenHighContrast = 0.2f;

float clamp_or(float v, float lo, float hi, float fallback) {
    if (std::isnan(v)) {
        return fallback;
    }
    return std::clamp(v, lo, hi);
}

template <class E> E enum_or(E v, E last, E fallback) {
    return static_cast<std::uint8_t>(v) <= static_cast<std::uint8_t>(last) ? v : fallback;
}

RGBA velocity_color(const LookSettings& look, std::uint8_t velocity) {
    const auto& v = look.velocity_colors;
    const float t = static_cast<float>(std::min<int>(velocity, 127)) / 127.0f;
    return t < 0.5f ? lerp(v[0], v[1], t * 2.0f) : lerp(v[1], v[2], (t - 0.5f) * 2.0f);
}

RGBA octave_color(const LookSettings& look, int note) {
    const int index = std::clamp(octave(note), 0, static_cast<int>(kOctaveColorCount) - 1);
    return look.octave_colors[static_cast<std::size_t>(index)];
}

RGBA scheme_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand) {
    switch (look.color_scheme) {
    case ColorScheme::ByPitchClass:
        return look.pitch_colors[static_cast<std::size_t>(pitch_class(note))];
    case ColorScheme::ByOctave:
        return octave_color(look, note);
    case ColorScheme::ByVelocity:
        return velocity_color(look, velocity);
    case ColorScheme::Single:
        return look.single_color;
    case ColorScheme::ByHand:
        break;
    }
    switch (hand) {
    case Hand::Left:
        return look.left_color;
    case Hand::Right:
        return look.right_color;
    case Hand::Unknown:
        break;
    }
    return look.unknown_color;
}

constexpr std::array<std::string_view, 12> kLetterSharp{"C",  "C#", "D",  "D#", "E",  "F",
                                                        "F#", "G",  "G#", "A",  "A#", "B"};
constexpr std::array<std::string_view, 12> kLetterFlat{"C",  "Db", "D",  "Eb", "E",  "F",
                                                       "Gb", "G",  "Ab", "A",  "Bb", "B"};
constexpr std::array<std::string_view, 12> kSolfegeSharp{"Do",  "Do#", "Re",   "Re#", "Mi",  "Fa",
                                                         "Fa#", "Sol", "Sol#", "La",  "La#", "Si"};
constexpr std::array<std::string_view, 12> kSolfegeFlat{"Do",   "Reb", "Re",  "Mib", "Mi",  "Fa",
                                                        "Solb", "Sol", "Lab", "La",  "Sib", "Si"};

} // namespace

std::array<RGBA, 12> default_pitch_colors() {
    // Hues 30 degrees apart starting at red, with lightness alternating so neighbors differ in
    // brightness as well as hue. Every color keeps dark or white label text readable.
    std::array<RGBA, 12> wheel{};
    for (std::size_t i = 0; i < wheel.size(); ++i) {
        const double lightness = i % 2 == 0 ? 0.70 : 0.82;
        wheel[i] = from_oklch(lightness, 0.15, 25.0 + 30.0 * static_cast<double>(i));
    }
    return wheel;
}

std::array<RGBA, kOctaveColorCount> default_octave_colors() {
    // One lightness, hues 32 degrees apart from blue at octave 0.
    std::array<RGBA, kOctaveColorCount> ramp{};
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        ramp[i] = from_oklch(0.78, 0.13, 250.0 + 32.0 * static_cast<double>(i + 1));
    }
    return ramp;
}

std::array<RGBA, kVelocityColorCount> default_velocity_colors() {
    // Soft to hard: blue, yellow, red.
    return {RGBA{76, 110, 245, 255}, RGBA{252, 196, 25, 255}, RGBA{255, 80, 80, 255}};
}

BackgroundStyle LookSettings::effective_background() const {
    return reduced_effects && background == BackgroundStyle::Motion ? BackgroundStyle::Gradient
                                                                    : background;
}

HitEffect LookSettings::effective_hit_effect() const {
    return reduced_effects ? HitEffect::None : hit_effect;
}

LookSettings LookSettings::clamped() const {
    const LookSettings d;
    LookSettings s = *this;
    s.note_style = enum_or(s.note_style, NoteStyle::Ink, d.note_style);
    s.block_glow = std::clamp(s.block_glow, kMinBlockGlow, kMaxBlockGlow);
    s.beat_lines = enum_or(s.beat_lines, BeatLines::Bars, d.beat_lines);
    s.style_intensity =
        clamp_or(s.style_intensity, kMinStyleIntensity, kMaxStyleIntensity, d.style_intensity);
    s.color_scheme = enum_or(s.color_scheme, ColorScheme::Single, d.color_scheme);
    s.name_mode = enum_or(s.name_mode, NameMode::Solfege, d.name_mode);
    s.names_spelling = enum_or(s.names_spelling, NameSpelling::Flats, d.names_spelling);
    s.sheet_naming = enum_or(s.sheet_naming, SheetNaming::Solfege, d.sheet_naming);
    s.sheet_chords = enum_or(s.sheet_chords, SheetChords::Symbols, d.sheet_chords);
    s.background = enum_or(s.background, BackgroundStyle::Motion, d.background);
    s.octave_numbering = enum_or(s.octave_numbering, OctaveNumbering::Yamaha, d.octave_numbering);
    s.key_size = enum_or(s.key_size, KeySize::ExtraLarge, d.key_size);
    s.chord_label_size =
        clamp_or(s.chord_label_size, kMinChordLabelSize, kMaxChordLabelSize, d.chord_label_size);
    s.visible_seconds =
        clamp_or(s.visible_seconds, kMinVisibleSeconds, kMaxVisibleSeconds, d.visible_seconds);
    s.keyboard_height =
        clamp_or(s.keyboard_height, kMinKeyboardHeight, kMaxKeyboardHeight, d.keyboard_height);
    s.grade_position = enum_or(s.grade_position, GradePosition::Top, d.grade_position);
    s.black_key_names = enum_or(s.black_key_names, BlackKeyNames::Off, d.black_key_names);
    s.block_layout = enum_or(s.block_layout, BlockLayout::EvenLanes, d.block_layout);
    s.hit_effect = enum_or(s.hit_effect, HitEffect::Sparks, d.hit_effect);
    s.grade_opacity = std::clamp(s.grade_opacity, kMinGradeOpacity, kMaxGradeOpacity);
    for (Palette& p : s.palettes) {
        p = enum_or(p, Palette::Mono, Palette::Classic);
    }
    settle_saved_palettes(s);
    for (std::size_t i = 0; i < s.learn_picks.size(); ++i) {
        s.learn_picks[i] = enum_or(s.learn_picks[i], LearnShade::Pink, d.learn_picks[i]);
    }
    return s;
}

int sheet_names_choice(const LookSettings& look) {
    if (!look.names_on_sheet) {
        return 0;
    }
    return look.sheet_naming == SheetNaming::Letters ? 1 : 2;
}

void set_sheet_names_choice(LookSettings& look, int choice) {
    look.names_on_sheet = choice != 0;
    if (choice == 1) {
        look.sheet_naming = SheetNaming::Letters;
    } else if (choice == 2) {
        look.sheet_naming = SheetNaming::Solfege;
    }
}

void set_grade_words(LookSettings& look, bool on, int restore) {
    if (!on) {
        look.grade_words = false;
        return;
    }
    look.grade_words = true;
    if (look.grade_opacity <= kMinGradeOpacity) {
        look.grade_opacity = std::clamp(restore, kMinGradeOpacity + 1, kMaxGradeOpacity);
    }
}

void set_grade_opacity(LookSettings& look, int percent, int restore) {
    percent = std::clamp(percent, kMinGradeOpacity, kMaxGradeOpacity);
    if (percent > kMinGradeOpacity) {
        look.grade_words = true;
        look.grade_opacity = percent;
    } else {
        look.grade_words = false;
        look.grade_opacity = std::clamp(restore, kMinGradeOpacity + 1, kMaxGradeOpacity);
    }
}

bool LookSettings::is_valid() const { return clamped() == *this; }

LookSettings reset_look(const LookSettings& current) {
    LookSettings out;
    out.octave_numbering = current.octave_numbering;
    out.key_size = current.key_size;
    out.show_typing_keys = current.show_typing_keys;
    out.grade_words = current.grade_words;
    out.grade_position = current.grade_position;
    out.grade_opacity = current.grade_opacity;
    out.black_key_names = current.black_key_names;
    out.block_layout = current.block_layout;
    out.beat_lines = current.beat_lines;
    out.rising_in_song = current.rising_in_song;
    out.combo_number = current.combo_number;
    out.saved_palettes = current.saved_palettes;
    return out;
}

ContrastTargets high_contrast_targets(const LookSettings& look) {
    if (!look.high_contrast) {
        return {};
    }
    return {kHighContrastTarget, kHighContrastMinimum};
}

RGBA note_color(const LookSettings& look, ContrastTargets targets, int note, std::uint8_t velocity,
                Hand hand) {
    RGBA c = scheme_color(look, note, velocity, hand);
    const bool black = is_black_key(note);
    if (look.high_contrast) {
        c = lift_to_contrast(c, kBackgroundGradientTop, targets.white);
    }
    if (black) {
        c = darken(c, look.high_contrast ? kBlackKeyDarkenHighContrast : kBlackKeyDarken);
        if (look.high_contrast) {
            c = lift_to_contrast(c, kBackgroundGradientTop, targets.black);
        }
    }
    return c;
}

RGBA note_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand) {
    return note_color(look, high_contrast_targets(look), note, velocity, hand);
}

RGBA label_color_for(RGBA block) {
    constexpr RGBA kBlack{0, 0, 0, 255};
    constexpr RGBA kWhite{255, 255, 255, 255};
    return contrast_ratio(kBlack, block) >= contrast_ratio(kWhite, block) ? kBlack : kWhite;
}

NameParts note_name_parts(int note, const LookSettings& look) {
    NameParts out;
    if (look.name_mode == NameMode::Off || !is_valid_note(note)) {
        return out;
    }
    const auto pc = static_cast<std::size_t>(pitch_class(note));
    const bool flats = look.names_spelling == NameSpelling::Flats;
    std::string_view name;
    if (look.name_mode == NameMode::Solfege) {
        name = flats ? kSolfegeFlat[pc] : kSolfegeSharp[pc];
    } else {
        name = flats ? kLetterFlat[pc] : kLetterSharp[pc];
    }
    // The letter or syllable comes first (capital, then lower case: "Sol"); a '#' or a lower-case
    // 'b' after it is the accidental.
    std::size_t keep = 1;
    while (keep < name.size() && std::islower(static_cast<unsigned char>(name[keep])) != 0 &&
           name[keep] != 'b') {
        ++keep;
    }
    out.letter = std::string(name.substr(0, keep));
    out.accidental = std::string(name.substr(keep));
    if (look.names_show_octave) {
        out.octave = std::to_string(octave_number(note, look));
    }
    return out;
}

std::vector<std::string> possible_name_pieces() {
    std::vector<std::string> pieces;
    const auto add = [&pieces](const std::string& piece) {
        if (!piece.empty() && std::find(pieces.begin(), pieces.end(), piece) == pieces.end()) {
            pieces.push_back(piece);
        }
    };
    LookSettings look;
    look.names_show_octave = true;
    for (const NameMode mode : {NameMode::Letters, NameMode::Solfege}) {
        for (const NameSpelling spelling : {NameSpelling::Sharps, NameSpelling::Flats}) {
            for (const OctaveNumbering numbering :
                 {OctaveNumbering::Scientific, OctaveNumbering::Yamaha}) {
                look.name_mode = mode;
                look.names_spelling = spelling;
                look.octave_numbering = numbering;
                for (int note = 0; note < 128; ++note) {
                    const NameParts p = note_name_parts(note, look);
                    add(p.letter);
                    add(p.octave);
                }
            }
        }
    }
    return pieces;
}

std::string note_label(int note, const LookSettings& look) {
    const NameParts p = note_name_parts(note, look);
    return p.letter + p.accidental + p.octave;
}

std::string note_short_label(int note, const LookSettings& look) {
    const NameParts p = note_name_parts(note, look);
    return p.letter + p.accidental;
}

int octave_number(int note, const LookSettings& look) {
    return octave(note) + look.octave_offset();
}

std::string c_marker_label(int note, const LookSettings& look) {
    if (!look.c_markers || !is_valid_note(note) || pitch_class(note) != 0) {
        return {};
    }
    return std::string(look.name_mode == NameMode::Solfege ? "Do" : "C") +
           std::to_string(octave_number(note, look));
}

LookSettings with_spelling_for_key(LookSettings look, bool key_flats) {
    if (look.names_spelling == NameSpelling::ByKey) {
        look.names_spelling = key_flats ? NameSpelling::Flats : NameSpelling::Sharps;
    }
    return look;
}

bool is_middle_c(int note) { return note == kMiddleC; }

} // namespace etude
