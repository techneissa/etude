// SPDX-License-Identifier: GPL-3.0-or-later
// PanelTab: which settings page or screen the UI opens, and the --panel option's names for them.
#pragma once

#include <cstdint>

namespace etude::app {

// Which page of the settings is showing, or what of the section band is open.
enum class PanelTab : std::uint8_t {
    Notes,
    Colours, // Effects (the colours, then the hit effects)
    View,
    Practice,
    Sections, // not a page: the section band with its lane open
    Plan,     // not a page: the section band with the practice list open
    Editor,   // the section editor
    Suggest,  // the section editor with the suggested cuts shown
    Input,
    Configs,
    ConfigsCopy, // not a page: Configs with "Copy Dani's config?" asked (a developer's view)
    AddSongs, // the phone upload page
    About,    // the version, the links and the updates
    Library,  // not a page: the song library
    KeyRing,  // not a page: the key ring alone (a developer's view)
    Learn,    // not a page: the Learn screen's gallery
    LearnScales, // not a page: the Learn screen's scale card
    LearnChords, // not a page: the Learn screen's chord card
    LearnEar,    // not a page: the Learn screen's ear card
    LearnMelodies, // not a page: the Learn screen's Melodies page
    Profiles,      // not a page: the profile panel, "Who's playing"
    ProfilesEdit,  // ... with its Edit mode on
    ProfileNew,    // not a page: the new-profile sheet
    ProfileKeys,   // ... with the on-screen keyboard up and a name typed
    ProfileEditSheet,  // ... the edit sheet of the household's last profile, with its Delete
    ProfileEditFirst,  // ... the edit sheet of the first profile, whose Delete is off
    ProfileDelete,     // ... the confirm that deletes the last profile (an empty field)
    ProfileDeleteKeys, // ... with the keyboard up and the name half typed
    ProfileDeleteTyped, // ... with the name typed, so Delete is on
    Progress,      // not a page: the song library on its Progress page
    Household,     // ... on its Household view
    Medals,        // ... on its Medals view (the wall)
    MedalsLadder,  // ... with the ladder of the first medal held open
    MedalSheet,    // not a page: the medals on a screen of their own (a developer's view)
    MedalTitles,   // ... the eight title medals large and small
};

// Parses "notes", "colours", "view", "practice", "input", "configs" ("presets" too),
// "configs-copy", "add-songs", "editor",
// "suggest", "library" (the song library), "learn" (the Learn gallery), "learn-scales" (its scale
// card), "learn-chords" (its chord card) or "key-ring" (the key ring alone), "profiles", "profiles-edit", "profile-new" and
// "profile-keys" (the profile switcher's panel, its Edit mode, and its sheet without and with the
// keyboard), "profile-edit-sheet", "profile-edit-first" and "profile-delete", "profile-delete-keys" and
// "profile-delete-typed" (the edit sheet of the last and of the first profile, and the confirm that
// deletes the last: empty, with the keyboard, with the name typed), "progress", "household", "medals" and "medals-ladder" (the library on its Progress page, its Household
// view, its Medals wall, and the wall with a medal's ladder open), the section band's "sections" (the lane) and "plan"
// (the practice list), and the names of the pages folded into others ("quick", "look", "names" open
// Notes; "effects" Effects; "keyboard" and "microphone" Input; "metronome" Practice); false for
// anything else. For the --panel option.
bool parse_panel_tab(const char* text, PanelTab* out);
// The Learn screen's tabs ("learn", "learn-scales", "learn-chords"), for parse_panel_tab and the
// panel opener (host_learn.cpp).
bool is_learn_panel_tab(PanelTab tab);

} // namespace etude::app
