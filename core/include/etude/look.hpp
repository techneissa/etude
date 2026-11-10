// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "etude/color.hpp"
#include "etude/hand.hpp"
#include "etude/hit_effects.hpp"
#include "etude/keyboard.hpp"
#include "etude/learn_palette.hpp"

namespace etude {

// The "Look and feel" options as plain data. The settings panel writes it, the
// renderer reads it, and nothing in here touches SDL, files or clocks.

enum class NoteStyle : std::uint8_t { Solid, Outline, Glass, Neon, Candy, Soft, Pixel, Ink };
// Lines across the song's blocks at its beat grid: none, every beat, every half beat, every bar.
enum class BeatLines : std::uint8_t { Off, Beats, HalfBeats, Bars };
enum class ColorScheme : std::uint8_t { ByHand, ByPitchClass, ByOctave, ByVelocity, Single };
inline constexpr std::size_t kColorSchemeCount = 5;
// The curated sets of colours a scheme can take in one tap (their colours are in palette.hpp).
enum class Palette : std::uint8_t { Classic, Vivid, Pastel, Ocean, Sunset, Mono };
enum class NameMode : std::uint8_t { Off, Letters, Solfege };
// How black keys are spelled: by the key at the playhead (flat keys E♭ B♭ A♭, sharp keys F♯ C♯;
// C major and A minor, and the modes, by their signature), or always with sharps or always with
// flats. `with_spelling_for_key` settles ByKey into Sharps or Flats for the key being played; the
// naming functions below read a settled look (an unsettled ByKey spells with sharps).
enum class NameSpelling : std::uint8_t { ByKey, Sharps, Flats };
// How the sheet music's note names are said: letters or Do Re Mi (its own choice, apart from the
// blocks' and keys' NameMode).
enum class SheetNaming : std::uint8_t { Letters, Solfege };
// A chord on the sheet: its notes' names, or one chord symbol ("C/E") when it is a chord.
enum class SheetChords : std::uint8_t { NoteNames, Symbols };
enum class BackgroundStyle : std::uint8_t { Plain, Gradient, Motion };
// Octave numbers shown to the player: scientific has middle C (MIDI 60) as C4, Yamaha manuals
// (the PSR-E383's included) call it C3.
enum class OctaveNumbering : std::uint8_t { Scientific, Yamaha };
// How wide the keys are drawn (the sizes in millimetres are in
// keyboard_view.hpp). The two fit modes fill the window width; the fixed sizes keep a real key
// width and the view follows the music across the keyboard.
enum class KeySize : std::uint8_t { FitKeyboard, FitSong, Small, Medium, Large, ExtraLarge };
// Where Flow Mode's grade words (Perfect, Good, ...) sit in the note area: just above the keys, or
// at the top of the note area below the chord label.
enum class GradePosition : std::uint8_t { Bottom, Top };
// Whether a black key's falling block shows its note name. Auto draws it across the block where it
// fits inside it and overlaps no other name; Always draws it where it fits the block's height, at
// the smallest size, even past a thin block's sides and over another name; Off never draws it.
enum class BlackKeyNames : std::uint8_t { Auto, Always, Off };

// Background colors the palettes are built against. `kBackgroundColor` is the plain background and
// the bottom of the gradient, `kBackgroundGradientTop` its lighter top. Motion stays between them.
inline constexpr RGBA kBackgroundColor{18, 20, 26, 255};
inline constexpr RGBA kBackgroundGradientTop{34, 38, 52, 255};

// Documented ranges. `LookSettings::clamped()` pulls every value into them.
inline constexpr int kMinBlockGlow = 0;
inline constexpr int kMaxBlockGlow = 100;
inline constexpr int kDefaultBlockGlow = 30;
inline constexpr float kMinStyleIntensity = 0.0f;
inline constexpr float kMaxStyleIntensity = 1.0f;
inline constexpr float kMinChordLabelSize = 0.5f; // multiplier on the default label size
inline constexpr float kMaxChordLabelSize = 2.0f;
inline constexpr float kMinVisibleSeconds = 1.0f; // seconds of notes that fit above the keyboard
inline constexpr float kMaxVisibleSeconds = 12.0f;
inline constexpr float kMinKeyboardHeight = 0.10f; // fraction of the window height
inline constexpr float kMaxKeyboardHeight = 0.30f;
inline constexpr int kMinGradeOpacity = 0; // percent; 0 hides the grade words
inline constexpr int kMaxGradeOpacity = 100;

// WCAG ratio the high-contrast palette reaches against the backgrounds above, for white-key notes
// (`kHighContrastTarget`) and, after the black-key darkening, for black-key notes
// (`kHighContrastMinimum`). The lift keeps each colour's hue and chroma (`lift_to_contrast`).
// Label text on a block always reaches 4.5:1.
inline constexpr double kHighContrastTarget = 7.0;
inline constexpr double kHighContrastMinimum = 4.5;

// The default 12-color pitch-class wheel, C first: evenly spaced hues at one OKLCH lightness, so
// neighbors differ clearly and none is much brighter than another.
std::array<RGBA, 12> default_pitch_colors();

// The player's own saved palettes: two slots, each the twelve note colours at the time of saving
// (saved_palettes.hpp). An empty slot holds nothing; its colours stay the defaults, so two empty
// slots always compare equal.
inline constexpr int kSavedPalettes = 2;
struct SavedPalette {
    bool filled = false;
    std::array<RGBA, 12> colors = default_pitch_colors();
    friend bool operator==(const SavedPalette&, const SavedPalette&) = default;
};
// One colour per octave of the piano, octave 0 (A0, B0) to 8 (C8) in scientific numbering, hues
// 32 degrees apart. Notes below octave 0 take its colour, notes above 8 take octave 8's.
inline constexpr std::size_t kOctaveColorCount = 9;
std::array<RGBA, kOctaveColorCount> default_octave_colors();
// The velocity ramp's three stops: soft (velocity 0), medium (half way) and hard (127). The colour
// between two stops is their mix.
inline constexpr std::size_t kVelocityColorCount = 3;
std::array<RGBA, kVelocityColorCount> default_velocity_colors();

struct LookSettings {
    // Note style, with an intensity 0..1 (how strong the gloss, frost, glow or outline is).
    NoteStyle note_style = NoteStyle::Glass;
    float style_intensity = 0.6f;
    // The glow round every block, 0 (none) to 100 (intense), in every style.
    int block_glow = kDefaultBlockGlow;
    // Lines across the song's blocks at its beat grid, thinned when they would crowd.
    BeatLines beat_lines = BeatLines::Off;

    // Colors. Left/right/unknown are for ByHand, `pitch_colors` (C first) for ByPitchClass,
    // `octave_colors` for ByOctave, `velocity_colors` for ByVelocity and `single_color` for Single.
    // `high_contrast` lifts whatever the scheme gives, keeping its hue and chroma, until it
    // reaches `kHighContrastTarget` on the dark background.
    ColorScheme color_scheme = ColorScheme::ByHand;
    RGBA left_color{255, 169, 77, 255};     // warm orange
    RGBA right_color{90, 170, 255, 255};    // the app's accent blue
    RGBA unknown_color{169, 177, 192, 255}; // cool grey
    std::array<RGBA, 12> pitch_colors = default_pitch_colors();
    std::array<RGBA, kOctaveColorCount> octave_colors = default_octave_colors();
    std::array<RGBA, kVelocityColorCount> velocity_colors = default_velocity_colors();
    RGBA single_color{90, 170, 255, 255};
    // The palette each scheme's colours were last set from, in `ColorScheme` order: what a reset
    // goes back to. Whether the colours still are that palette is `matching_palette` (palette.hpp).
    std::array<Palette, kColorSchemeCount> palettes{};
    // The two saved palettes, kept with the look like the note colours (an older file has none: two
    // empty slots), and the slot the note colours came from, or -1 when they came from a
    // built-in palette (`palettes`, ByPitchClass): what Reset on a note colour goes back to.
    std::array<SavedPalette, kSavedPalettes> saved_palettes{};
    int note_slot = -1;
    bool high_contrast = false;

    // The shade of each Learn section (Settings > View): its tab, its cards' tops and the keys it
    // lights (learn_palette.hpp). Part of the look, so it is kept per profile and saved with it.
    LearnPicks learn_picks = kLearnDefaultPicks;

    // Note names.
    NameMode name_mode = NameMode::Letters;
    bool names_show_octave = false;
    NameSpelling names_spelling = NameSpelling::ByKey;
    bool names_on_blocks = true;
    bool names_on_keys = false;
    // Sheet music's reading helps (off by default): each note's name above the top staff's notes
    // and below the others', in letters or Do Re Mi by `sheet_naming` (octave numbers and sharps or
    // flats as above), a recognised chord as one chord symbol by `sheet_chords`; and each note
    // head outlined in its note's colour (`pitch_colors`, as Colour by: Note shows them) until the
    // note is played.
    bool names_on_sheet = false;
    SheetNaming sheet_naming = SheetNaming::Letters;
    SheetChords sheet_chords = SheetChords::NoteNames;
    bool sheet_note_colours = false;
    // Names on the blocks of black keys, which are narrower than the name (see BlackKeyNames).
    BlackKeyNames black_key_names = BlackKeyNames::Auto;
    // How wide the falling blocks are: their key's width, or even lanes that never overlap.
    BlockLayout block_layout = BlockLayout::KeyWidth;
    // Every C key carries its name and octave, middle C emphasized. Independent of the switches
    // above, so it works with names Off.
    bool c_markers = true;
    // Applies to every octave number shown: names with the octave, C markers, status text.
    OctaveNumbering octave_numbering = OctaveNumbering::Scientific;

    // Effects.
    bool key_flash = true;
    bool held_glow = true;
    // What a key press shows at the hit line, beneath the blocks (hit_effects.hpp).
    HitEffect hit_effect = HitEffect::Glow;
    bool trail_fade = true;
    // The combo on screen (#88): the number with a fire icon in the note area's top-right corner
    // from 10 notes in a row. The player's choice, kept by `reset_look`.
    bool combo_number = true;
    // The player's own notes rise as faint blocks while a song is open too. Off: during a song
    // only the keys and the falling notes show what was played, at the keyboard line
    // (hit_line.hpp); in free play the blocks rise either way.
    bool rising_in_song = false;
    BackgroundStyle background = BackgroundStyle::Gradient;
    // Forces the background motion and the hit effect off in the effective accessors.
    bool reduced_effects = false;

    // Chord label and the view.
    KeySize key_size = KeySize::FitKeyboard;
    // While playing with the computer keyboard, show its letters on the piano keys.
    bool show_typing_keys = false;
    bool chord_label = true;
    float chord_label_size = 1.0f;
    // The song's key ("G major") shown faintly at the bottom left of the note area (key_label.hpp).
    bool show_key = true;
    float visible_seconds = 4.0f;
    float keyboard_height = 0.18f;

    // Flow Mode's grade words. `grade_words` is the switch, `grade_opacity` the strength in
    // percent; 0 hides them just like the switch, so the two never disagree (see
    // `set_grade_words` and `set_grade_opacity`).
    bool grade_words = true;
    GradePosition grade_position = GradePosition::Bottom;
    int grade_opacity = kMaxGradeOpacity;

    // Whether the words show at all, and how opaque (0..1).
    bool grade_words_shown() const { return grade_words && grade_opacity > kMinGradeOpacity; }
    float grade_alpha() const {
        return grade_words_shown() ? static_cast<float>(grade_opacity) / 100.0f : 0.0f;
    }

    // Where names are drawn: only when the mode is not Off and that place is switched on. The two
    // places are independent (blocks only, keys only, both, or neither).
    bool shows_block_names() const { return name_mode != NameMode::Off && names_on_blocks; }
    bool shows_key_names() const { return name_mode != NameMode::Off && names_on_keys; }

    // Added to the scientific octave number: 0, or -1 for Yamaha numbering.
    int octave_offset() const { return octave_numbering == OctaveNumbering::Yamaha ? -1 : 0; }

    // What the renderer should actually draw. Everything but these two is read directly.
    BackgroundStyle effective_background() const;
    HitEffect effective_hit_effect() const;

    // True when every value is inside its documented range (and no enum holds a stray number).
    bool is_valid() const;
    // Brings every out-of-range value back: numbers to the nearest end of their range (NaN to the
    // default), a stray enum value to its default.
    LookSettings clamped() const;
    void validate() { *this = clamped(); }

    friend bool operator==(const LookSettings&, const LookSettings&) = default;
};

// The default look with the player's own preferences kept: the octave numbering, the key size,
// the typing-key letters, the grade words, the black key names, the block layout, the beat
// lines, whether the notes rise during a song, the combo number and the two saved palettes.
// "Reset all" applies it.
// User slots save and load the whole look, these included.
LookSettings reset_look(const LookSettings& current);

// The grade-word switch as the player sets it. On shows the words: a stored opacity of 0 comes
// back as `restore` (the last non-zero one the caller knows, 100 when it knows none). Off hides
// them and keeps the opacity, so turning them on again restores it.
void set_grade_words(LookSettings& look, bool on, int restore = kMaxGradeOpacity);
// The opacity slider as the player sets it (percent, clamped). Above 0 it switches the words on;
// 0 switches them off and puts `restore` back as the stored opacity, so the switch can bring the
// last non-zero strength back.
void set_grade_opacity(LookSettings& look, int percent, int restore = kMaxGradeOpacity);

// The sheet music's note names as one choice of three, as the settings page shows them: 0 none
// (`names_on_sheet` off), 1 letters, 2 solfege. Off keeps the chosen naming, so turning names on
// again brings it back.
int sheet_names_choice(const LookSettings& look);
void set_sheet_names_choice(LookSettings& look, int choice);

// Block color for a note. White keys get the scheme's color; black keys get the same color
// darkened, in every scheme. `velocity` is 0..127 and only ByVelocity reads it. The alpha is the
// scheme color's own, and the style (gloss, frost, glow) is the renderer's job.
RGBA note_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand);
// The contrast High contrast lifts a white-key and a black-key note to; without High contrast both
// are 0. A caller that colours many notes asks once and passes the result to the overload below.
struct ContrastTargets {
    double white = 0.0;
    double black = 0.0;
};
ContrastTargets high_contrast_targets(const LookSettings& look);
RGBA note_color(const LookSettings& look, ContrastTargets targets, int note, std::uint8_t velocity,
                Hand hand);
// Black or white, whichever reads better on `block`. At least 4.5:1 on any color.
RGBA label_color_for(RGBA block);
// The color a pressed key lights in. It is `note_color` itself, so a key and the note block above
// it always match, under every scheme and for black keys too. A key pressed with no note to play
// (free play, or a wrong key in practice) passes `Hand::Unknown` and the velocity it was hit with.
inline RGBA key_light_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand) {
    return note_color(look, note, velocity, hand);
}

// The octave number the player sees for `note`: the scientific octave plus
// `LookSettings::octave_offset()`. Note 60 is 4 (scientific) or 3 (Yamaha); notes 0..11 are -1
// (scientific) or -2 (Yamaha).
int octave_number(int note, const LookSettings& look);

// A note's name in the three pieces the renderer draws separately: the letter or syllable ("C",
// "Sol"), the accidental ("#" or "b", empty on a white key, drawn small and raised beside the
// letter) and the octave number ("4", "-1", empty unless `names_show_octave` is set). All empty
// when names are Off or the note is outside 0..127.
struct NameParts {
    std::string letter;
    std::string accidental;
    std::string octave;
};
NameParts note_name_parts(int note, const LookSettings& look);
// Every letter, syllable and octave number `note_name_parts` can give under any naming settings
// (letters or solfege, sharps or flats, either octave numbering), each once: what a caller that
// measures name pieces ahead of time has to cover. The accidental is not among them (it is drawn,
// never set in text).
std::vector<std::string> possible_name_pieces();

// "C", "C#" or "Db", "Do", "Do#" or "Reb" (fixed-do), with the octave ("C#4") when
// `names_show_octave` is set: the three parts of `note_name_parts` end to end. Empty when names are
// Off or the note is outside 0..127.
std::string note_label(int note, const LookSettings& look);

// The name on a C key: letter "C" ("Do" in solfege mode) and the octave number, always, whatever
// the name mode and `names_show_octave`: "C4" or "C3" for middle C. Empty when `c_markers` is off,
// the note is not a C or is outside 0..127.
std::string c_marker_label(int note, const LookSettings& look);
// `note_label` less the octave number: the letter or syllable and, on a black key, its accidental
// ("C", "C#", "Db", "Do", "Sol#"). The accidental is never dropped, because "C" on a C# key names
// the wrong key. Empty under the same conditions as `note_label`.
std::string note_short_label(int note, const LookSettings& look);
// The key middle C (MIDI 60), emphasized among the C markers whatever the numbering says.
bool is_middle_c(int note);

// `look` with a ByKey spelling settled: Flats when `key_flats`, else Sharps. A look that chose
// Sharps or Flats is returned as it is. The names are made from the settled look, so a key change
// that flips `key_flats` is the only moment they are spelled again (key_spells_flats).
LookSettings with_spelling_for_key(LookSettings look, bool key_flats);

// Three user slots that save the current look under a name and load it back. The built-in
// presets above stay functions; these are the player's own.
inline constexpr int kUserPresetSlots = 3;
inline constexpr std::size_t kMaxPresetNameBytes = 48; // UTF-8 bytes, cut at a character boundary

struct UserPresetSlot {
    bool used = false;
    std::string name;  // empty when the slot is unused, never empty when it is used
    LookSettings look; // the defaults when the slot is unused

    friend bool operator==(const UserPresetSlot&, const UserPresetSlot&) = default;
};

struct UserPresets {
    std::array<UserPresetSlot, kUserPresetSlots> slots;

    // `slot` is 0..2; every function refuses anything else (false, or an empty name).
    bool is_used(int slot) const;
    std::string_view slot_name(int slot) const;
    // Stores `look` (clamped) under `name`. The name is trimmed, loses control characters and is
    // capped at `kMaxPresetNameBytes`; one that ends up empty or is not valid UTF-8 becomes
    // "Preset <n>". Replaces whatever the slot held.
    bool save_slot(int slot, std::string_view name, const LookSettings& look);
    // Copies the slot's look into `*out` (when not null). False, and `*out` untouched, for an
    // empty slot.
    bool load_slot(int slot, LookSettings* out) const;
    bool clear_slot(int slot);

    friend bool operator==(const UserPresets&, const UserPresets&) = default;
};

// Everything the look screen saves between sessions.
struct LookDocument {
    LookSettings look;
    UserPresets user_presets;

    friend bool operator==(const LookDocument&, const LookDocument&) = default;
};

// Text form: a versioned `key = value` list, one per line. Unknown keys and bad values are
// skipped on the way back in.
inline constexpr int kLookFormatVersion = 1;
// Text of the look alone.
std::string serialize(const LookSettings& look);
// Text of the look and the three user slots: `slot_1_used = true`, `slot_1_name = ...`, then the
// slot's settings under the same names with a `slot_1_` prefix. An empty slot is one
// `slot_N_used = false` line. Names are written as raw UTF-8.
std::string serialize_document(const LookDocument& doc);

struct ParseIssue {
    enum class Kind : std::uint8_t {
        BadLine,      // no `key = value` shape
        UnknownKey,   // the key is not a setting; value ignored
        BadValue,     // the value does not parse; setting keeps its default
        OutOfRange,   // a number outside its range; the nearest end is used
        NewerVersion, // the file says it is a newer format than this build knows
    };
    Kind kind = Kind::BadLine;
    int line = 0; // 1-based
    std::string key;
};

struct ParseReport {
    int version = 0; // 0 when the text has no version line
    int applied = 0; // settings read from the text
    std::vector<ParseIssue> issues;
    bool clean() const { return issues.empty(); }
};

// Reads `serialize()` output. Starts from the defaults, so a missing key keeps its default. Never
// throws, and always returns valid settings. A later line for the same key wins.
LookSettings parse_look(std::string_view text, ParseReport* report = nullptr);
// The same for the whole document. A slot the text marks unused is empty, whatever else its lines
// say; a used slot with no valid name gets "Preset <n>". A damaged line costs that one setting.
LookDocument parse_look_document(std::string_view text, ParseReport* report = nullptr);

} // namespace etude
