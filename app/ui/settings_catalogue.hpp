// SPDX-License-Identifier: GPL-3.0-or-later
// Every setting the player can change, and the one place it lives: a settings page, or the bar
// (and the tempo popover) for the few that belong to the song's controls. No ImGui in here, so
// tests/settings_catalogue_test.cpp checks the rules:
// - each setting has exactly one home, and the catalogue lists each setting once;
// - each row that draws a setting says which setting it is (`drawn`), on a page, in the tempo
//   popover or on the bar's metronome button, so a sweep over the pages and the bar
//   (`etude --check-settings`) finds a setting drawn on two pages, on a page that is not its
//   home, on none, or a setting of the bar that the bar does not draw.
// A bar shortcut may open the page a setting lives on (the note style and colours shortcuts). A
// bar control that flips a catalogued setting (the pin) is declared with `bar_shortcut`: the same
// setting with the same behaviour, never a second copy of the page's row with its own behaviour.
#pragma once

#include "page_ids.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app::ui {

enum class Setting : std::uint8_t {
    // Notes
    NoteStyle,
    StyleIntensity,
    BlockGlow,
    BeatLines,
    BlockLayout,
    NameMode,
    NamesWhere,
    NamesAccidentals,
    BlackKeyNames,
    OctaveNumber,
    CMarkers,
    MiddleC,
    NamesOnSheet,
    SheetNaming,
    SheetChords,
    SheetNoteColours,
    KeyLabel,
    // Effects
    ColourScheme,
    Palette,
    SchemeColours,
    HighContrast,
    HitEffect,
    KeyFlash,
    HeldGlow,
    TrailFade,
    ComboNumber,
    RisingInSong,
    Background,
    ReducedEffects,
    // View
    ViewMode,
    KeySize,
    KeyboardHeight,
    NotesAhead,
    ChordName,
    ChordNameSize,
    GradeWords,
    GradePosition,
    GradeOpacity,
    BarStyle,
    KeepBarShown,
    BandDifficulty,
    Renderer, // a restart applies it
    ScreenSize, // the display's width in mm, for a panel that reports the wrong one
    LearnColours, // the shade of each Learn section: four rows of a palette and a reset
    // Practice
    Hands, // a song whose file does not name the hands: where the notes divide, kept per song
    HitWindow,
    WeightedScore,
    RunsKept, // how many of a song's newest runs keep their played notes for the replay
    ResumeAfter,
    SkipBack,
    SkipForward,
    MetronomeVolume,
    MetronomeSound,
    MetronomeAccent,
    MetronomeSubdivision,
    MetronomePulse,
    AccompanimentOn,     // the default for songs; each song's own mutes are on the bar
    AccompanimentVolume, // also in the accompaniment menu on the bar
    // Input
    InputSource,
    MicDevice,
    CalibrateNoise,
    LearnTuning,
    Tuning,
    KeyboardProfile,
    MidiInputPort,
    MidiOutputPort,
    DeviceCheck, // a row that runs the device check; nothing is saved by it
    TypingLetters,
    Latency,
    AbcChords,
    // Configs
    Configs,
    ResetAll,
    // About
    UpdateAutoCheck,
    IconColour, // the row of eight under the app icon
    // The song library's chips: the order it shows the songs in
    LibrarySort,
    LibraryGroup,
    LibraryFilter,
    LibraryFacets, // the level and hands filters, in the Filters popover
    LibraryView,
    GetSongsOnline, // in the library's Get songs section and its credits
    GroupVersions,  // in the library's view popover
    // On the bar and in the tempo popover only
    MetronomeOn,
    CountIn,
};

inline constexpr std::size_t kSettingCount = static_cast<std::size_t>(Setting::CountIn) + 1;

// Where a setting lives: a settings page, the bar and its tempo popover, or the song library.
struct Home {
    bool on_bar = false;
    Page page = Page::Notes; // when on neither the bar nor the library
    bool in_library = false;
};

struct CatalogueEntry {
    Setting id;
    std::string_view name; // for messages: the key it is saved under, or a short name
    Home home;
    // The bar also has a control that flips this same setting.
    bool bar_shortcut = false;
};

// One entry per setting, in `Setting` order.
std::span<const CatalogueEntry, kSettingCount> catalogue();
const CatalogueEntry& entry_of(Setting s);

// The settings whose home is `p`, in catalogue order.
std::vector<Setting> settings_on(Page p);

// A row drawn while the sweep ran: the setting, and the page being drawn, `on_bar` (the tempo
// popover or the bar's own button) or `in_library` (the library's chips).
struct Drawn {
    bool on_bar = false;
    Page page = Page::Notes; // when on neither the bar nor the library
    Setting setting = Setting::NoteStyle;
    bool in_library = false;
    int column = 0; // the page's column the row was drawn in (0 left, 1 right), see `column_of`
};

// The column of its page a setting must be drawn in, when the page fixes it: the Practice page's
// right column holds the metronome and the accompaniment, the sound that plays with the song.
// -1 for a setting whose column is not fixed.
int fixed_column(Setting s);

// The rules, over what a sweep drew. One line per problem; empty when all hold:
// - a setting drawn on two pages;
// - a setting drawn on a page that is not its home;
// - a setting with a page as home that no page drew;
// - a setting with a fixed column (`fixed_column`) drawn in another one;
// - a setting whose home is the bar drawn on a page, or that the bar never drew;
// - a setting with a page as home drawn on the bar without being a declared `bar_shortcut`;
// - a setting of the library drawn anywhere else, or that the library never drew, and a setting
//   drawn in the library whose home is not the library.
std::vector<std::string> check_drawn(std::span<const Drawn> drawn);

// Recording, switched on by the sweep only: the page's drawing code says which page it is
// drawing (`set_page`, and `clear_page` when it is done), the library screen that it is drawing
// (`set_library`), and each row says which setting it is (`drawn`; with neither set it is drawn
// on the bar). All are no-ops while no recording runs.
namespace catalogue_sweep {
bool recording();
void start();
std::vector<Drawn> stop();
void set_page(Page p);
void clear_page();
// The page's rows go on in its next column (`Columns::next`, whether or not the window is wide
// enough to draw two); `set_page` starts at the first.
void next_column();
void set_library(bool drawing);
void drawn(Setting s);
} // namespace catalogue_sweep

} // namespace etude::app::ui
