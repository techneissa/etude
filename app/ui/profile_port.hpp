// SPDX-License-Identifier: GPL-3.0-or-later
// What the profile switcher (profile_ui.hpp) is told each frame, and what it hands back: the app
// owns the profiles (the history store) and does the switching; the UI only draws and asks.
#pragma once

#include "etude/look.hpp"
#include "etude/profile.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace etude::app {

// A profile's own look as its stored settings hold it (the config a copy takes): read only, the UI
// never writes another profile's settings.
struct ProfileLook {
    std::int64_t id = 0;
    LookSettings look;
};

struct ProfileInfo {
    const ProfileSet* set = nullptr; // every profile, in the order they were made; null: none shown
    std::span<const ProfileLook> looks; // the looks of the profiles but the one in use
    std::span<const int> streaks;    // the days in a row each practised, in `set` order
    std::span<const int> freezes;    // the streak freezes each has banked, in `set` order
    std::span<const int> titles;     // each one's overall title, 1 .. 8 (medals.hpp), in `set` order; 0: not worked out yet
    std::int64_t active = kDefaultProfileId;
    bool ready = false;              // the history that keeps the profiles is loaded: they can change
    int goal_minutes = 15;           // the active profile's daily goal (0: none)
    bool progress = false;           // the panel has its "Progress and household" row
};

enum class ProfileAction : std::uint8_t {
    None,
    Switch, // `id` becomes the profile in use
    Add,    // a new profile with `name` and `colour`, made the profile in use; `goal_minutes` for it
    Edit,   // profile `id` gets `name` and `colour` (and `goal_minutes` when it is the one in use)
    Delete,   // profile `id` and everything of its own is deleted (the sheet's confirm was tapped)
    Progress, // the panel's "Progress and household" row: the library opens on the Progress page
};

struct ProfileChoice {
    ProfileAction action = ProfileAction::None;
    std::int64_t id = 0;
    std::string name;
    std::uint32_t colour = 0; // 0xRRGGBB
    int goal_minutes = -1;    // -1: leave the goal as it is
};

} // namespace etude::app
