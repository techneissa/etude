// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// A person who practises on this device. Everybody gets the default profile without setting
// anything up; a household adds the others. What belongs to a profile and what stays with the
// device is profile_settings.hpp's rule; the runs and a song's practice choices are keyed by the
// profile's id in the history.

inline constexpr std::int64_t kDefaultProfileId = 1;
inline constexpr std::string_view kDefaultProfileName = "Player";
inline constexpr std::size_t kMaxProfileNameBytes = 32;
inline constexpr std::size_t kMaxProfiles = 16; // a big household

// The colours a profile can be given, as 0xRRGGBB: far apart in hue, so two people at one keyboard
// tell their profiles apart, then white and black. Any 24-bit colour is kept (a profile stores its
// value, so a palette that grows later changes none); these are what a new profile picks from, in
// order, and the sheet offers. The first eight are the palette's first version and keep their place.
inline constexpr std::array<std::uint32_t, 14> kProfileColours{
    0x4C9BE8, 0xE8794C, 0x5CBF72, 0xD95F9E, 0xE0B83C, 0x8E6CD9, 0x3FB8B0, 0xD9534F,
    0xA6CE3F, 0x5E73E6, 0xBD5AD6, 0xB5835A, 0xFFFFFF, 0x000000,
};

struct Profile {
    std::int64_t id = kDefaultProfileId;
    std::string name{kDefaultProfileName}; // <= kMaxProfileNameBytes of UTF-8, never empty
    std::uint32_t colour = kProfileColours[0]; // 0xRRGGBB
    std::int64_t created_utc_s = 0;

    bool is_default() const { return id == kDefaultProfileId; }

    friend bool operator==(const Profile&, const Profile&) = default;
};

// A name as a person typed it, made fit to keep: blanks trimmed at both ends, control characters
// and runs of blanks inside turned into one space, cut to kMaxProfileNameBytes at a character
// boundary. Empty when nothing is left or the text is not UTF-8.
std::string clean_profile_name(std::string_view text);

// The same profile with its name cleaned (the default profile's name for an empty one) and its
// colour in 0x000000 .. 0xFFFFFF.
Profile sanitized(const Profile& profile);

// Whether what was typed to confirm a deletion is the profile's name: exactly, case and all, with the
// blanks (spaces) at either end of what was typed left out. Bytes are compared, so a multi-byte name
// matches only itself and half a character never matches. An empty name matches nothing.
bool confirms_profile_name(std::string_view typed, std::string_view name);

enum class ProfileResult : std::uint8_t {
    Ok,
    EmptyName,     // nothing left of the name
    NameTaken,     // another profile has the name (letters of the ASCII range compare without case)
    TooMany,       // kMaxProfiles reached
    NoSuchProfile, // an id the set does not hold
    NotReady,      // the history that keeps the profiles is not loaded yet (the app's store says it)
    LastProfile,   // delete: the only profile left stays, so the app always has one in use
    FirstProfile,  // delete: the first (default) profile stays while others exist; rename it instead
    NotSaved,      // delete: the history cannot be changed now (another etude holds it), so it is refused
};

// When a profile was last in use, for choosing who takes over from one that is deleted: the later of
// its newest run and the moment it was last switched away from this session (0: neither).
struct ProfileUse {
    std::int64_t id = 0;
    std::int64_t last_used_utc_s = 0;
};

// The profiles of a device, in the order they were made. The default profile is always there. Pure
// data: the history keeps it on disk and the app holds one copy.
class ProfileSet {
  public:
    ProfileSet(); // just the default profile
    // From stored rows: each sanitized, a repeated id or name dropped (the first stays), the default
    // profile added when the rows lack it, and no more than kMaxProfiles kept (the default first).
    explicit ProfileSet(std::span<const Profile> rows);

    std::span<const Profile> all() const { return profiles_; }
    std::size_t size() const { return profiles_.size(); }
    const Profile* find(std::int64_t id) const;
    bool contains(std::int64_t id) const { return find(id) != nullptr; }
    const Profile& default_profile() const { return *find(kDefaultProfileId); }
    // `id` when the set holds it, else the default profile's.
    std::int64_t resolve(std::int64_t id) const { return contains(id) ? id : kDefaultProfileId; }

    // Whether `name` (cleaned) would be accepted for the profile `except_id` (0: a new one):
    // EmptyName, NameTaken, or Ok. What a name field asks before it enables Save.
    ProfileResult check_name(std::string_view name, std::int64_t except_id = 0) const;

    // The id the next profile gets: above every id held, so an id is never reused in a session.
    std::int64_t next_id() const;
    // The first palette colour no profile uses; when all are used, the one used least (the first
    // of those).
    std::uint32_t unused_colour() const;

    // Adds a profile with the next id and the first unused colour (`made` gets it when not null).
    ProfileResult add(std::string_view name, std::int64_t now_utc_s, Profile* made = nullptr);
    ProfileResult rename(std::int64_t id, std::string_view name);
    ProfileResult recolour(std::int64_t id, std::uint32_t colour);

    // Whether the profile may be deleted: Ok, NoSuchProfile, LastProfile (it is the only one) or
    // FirstProfile (the default profile, which every start has, stays; the rest can go).
    ProfileResult check_delete(std::int64_t id) const;
    // Removes it when check_delete says Ok; the set is unchanged otherwise.
    ProfileResult remove(std::int64_t id);
    // The profile that takes over from `leaving`: the most recently used of the others, where "used"
    // is the later of its `uses` time (newest run or last left) and when it was made; the earlier one
    // of two used at the same moment. `leaving` itself when it is the only one.
    std::int64_t successor(std::int64_t leaving, std::span<const ProfileUse> uses) const;

    friend bool operator==(const ProfileSet&, const ProfileSet&) = default;

  private:
    bool name_taken(std::string_view name, std::int64_t except_id) const;
    std::vector<Profile> profiles_;
};

} // namespace etude
