// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace etude {

// The player's own groups of songs ("My groups"): named, ordered, and holding songs by content
// hash, so a song keeps its groups when its file is renamed or moved, and one song can be in
// several. Downloaded songs will land in a group of their catalogue's (kind Catalogue, `source`
// naming the catalogue) until the player moves them; it shows and behaves as any other group.
//
// The groups change by edits (GroupEdit), each touching one group or one membership, so edits
// made before the stored groups are read can be laid over them once they are (`replay`), never
// replacing what was stored.
enum class GroupKind : std::uint8_t { User, Catalogue };

struct SongGroup {
    std::uint64_t id = 0; // never 0
    std::string name;
    GroupKind kind = GroupKind::User;
    std::string source;        // Catalogue: the catalogue's id ("mutopia"); empty for User
    std::int32_t position = 0; // the groups' order, low first (ties: created, then id)
    std::int64_t created_utc_s = 0;

    friend bool operator==(const SongGroup&, const SongGroup&) = default;
};

// The longest group name kept, in bytes.
inline constexpr std::size_t kMaxGroupName = 80;

struct GroupEdit {
    enum class Kind : std::uint8_t {
        Create, // `group` (a new id); an id already there is left as it is
        Rename, // group.id to group.name
        Remove, // group.id and its memberships
        Add,    // `hash` into group.id (once)
        Drop,   // `hash` out of group.id
        Move,   // group.id to group.position
    };
    Kind kind = Kind::Create;
    SongGroup group;
    std::uint64_t hash = 0;
    std::int64_t at_utc_s = 0; // Add: when
};

// A group name as it is kept: valid UTF-8 (sanitized as song settings are), its runs of white
// space one space, trimmed, at most kMaxGroupName bytes; "New group" when nothing is left.
std::string clean_group_name(std::string_view name);

class LibraryGroups {
  public:
    // Applies one edit. False when it changed nothing (an unknown group, a song already in it).
    bool apply(const GroupEdit& edit);
    // The stored groups and memberships as read, with `edits` (made meanwhile) laid over them.
    struct Member {
        std::uint64_t group = 0;
        std::uint64_t hash = 0;
        std::int64_t added_utc_s = 0;
    };
    void replay(std::span<const SongGroup> groups, std::span<const Member> members,
                std::span<const GroupEdit> edits);

    // In their order.
    const std::vector<SongGroup>& groups() const { return groups_; }
    const SongGroup* find(std::uint64_t id) const;
    // The songs of a group, by hash, in the order they were added.
    std::span<const std::uint64_t> songs_of(std::uint64_t id) const;
    bool contains(std::uint64_t id, std::uint64_t hash) const;
    // A position after every group's, for a new one.
    std::int32_t next_position() const;
    // Bumped by every change.
    std::uint64_t version() const { return version_; }

  private:
    void sort_groups();
    std::vector<SongGroup> groups_;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> songs_; // by group id
    std::uint64_t version_ = 0;
};

} // namespace etude
