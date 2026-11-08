// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_groups.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

GroupEdit create(std::uint64_t id, std::string name, std::int32_t position = 0) {
    GroupEdit e;
    e.kind = GroupEdit::Kind::Create;
    e.group.id = id;
    e.group.name = std::move(name);
    e.group.position = position;
    return e;
}

GroupEdit member(GroupEdit::Kind kind, std::uint64_t id, std::uint64_t hash) {
    GroupEdit e;
    e.kind = kind;
    e.group.id = id;
    e.hash = hash;
    return e;
}

std::vector<std::string> names(const LibraryGroups& g) {
    std::vector<std::string> out;
    for (const SongGroup& s : g.groups()) {
        out.push_back(s.name);
    }
    return out;
}

} // namespace

TEST_CASE("groups: create, rename, order, remove; a song in several groups") {
    LibraryGroups g;
    CHECK(g.apply(create(10, "Recital", 1)));
    CHECK(g.apply(create(11, "  Warm  ups ", 0)));
    CHECK_FALSE(g.apply(create(10, "Again"))); // the id is taken: left as it is
    CHECK(names(g) == std::vector<std::string>{"Warm ups", "Recital"});
    CHECK(g.apply(member(GroupEdit::Kind::Add, 10, 0xA)));
    CHECK(g.apply(member(GroupEdit::Kind::Add, 11, 0xA)));
    CHECK_FALSE(g.apply(member(GroupEdit::Kind::Add, 11, 0xA))); // once
    CHECK(g.apply(member(GroupEdit::Kind::Add, 10, 0xB)));
    CHECK(g.groups_of(0xA) == std::vector<std::uint64_t>{11, 10});
    CHECK(g.songs_of(10).size() == 2);
    GroupEdit move;
    move.kind = GroupEdit::Kind::Move;
    move.group.id = 10;
    move.group.position = -1;
    CHECK(g.apply(move));
    CHECK(names(g) == std::vector<std::string>{"Recital", "Warm ups"});
    GroupEdit rename;
    rename.kind = GroupEdit::Kind::Rename;
    rename.group.id = 11;
    rename.group.name = "";
    CHECK(g.apply(rename));
    CHECK(g.find(11)->name == "New group"); // nothing left of the name
    CHECK(g.apply(member(GroupEdit::Kind::Drop, 10, 0xB)));
    CHECK_FALSE(g.contains(10, 0xB));
    GroupEdit remove;
    remove.kind = GroupEdit::Kind::Remove;
    remove.group.id = 10;
    CHECK(g.apply(remove));
    CHECK(g.find(10) == nullptr);
    CHECK(g.groups_of(0xA) == std::vector<std::uint64_t>{11});
    CHECK_FALSE(g.apply(member(GroupEdit::Kind::Add, 10, 0xC))); // gone
    CHECK(g.next_position() == 1);
}

TEST_CASE("groups: edits made before the store was read go over what it held") {
    // Read later: one stored group with two songs. Meanwhile the player made a group, added a song
    // to the stored one and took another out of it.
    const std::vector<SongGroup> stored{{5, "Exam pieces", GroupKind::User, "", 0, 100},
                                        {6, "Mutopia", GroupKind::Catalogue, "mutopia", 1, 90}};
    const std::vector<LibraryGroups::Member> members{
        {5, 0x1, 10}, {5, 0x2, 20}, {6, 0x3, 5}, {9, 0x4, 1}}; // 9: no such group
    std::vector<GroupEdit> edits{
        create(7, "Sight reading", 2), member(GroupEdit::Kind::Add, 5, 0x3),
        member(GroupEdit::Kind::Drop, 5, 0x1), member(GroupEdit::Kind::Add, 7, 0x2)};
    LibraryGroups g;
    const std::uint64_t before = g.version();
    g.replay(stored, members, edits);
    CHECK(g.version() != before);
    CHECK(names(g) == std::vector<std::string>{"Exam pieces", "Mutopia", "Sight reading"});
    CHECK(g.find(6)->kind == GroupKind::Catalogue);
    CHECK(g.find(6)->source == "mutopia");
    const std::span<const std::uint64_t> exam = g.songs_of(5);
    CHECK(std::vector<std::uint64_t>(exam.begin(), exam.end()) ==
          std::vector<std::uint64_t>{0x2, 0x3});
    CHECK(g.groups_of(0x2) == std::vector<std::uint64_t>{5, 7});
    CHECK(g.songs_of(9).empty());
}

TEST_CASE("groups: names are cleaned and capped") {
    CHECK(clean_group_name("  a\tb  ") == "a b");
    CHECK(clean_group_name("") == "New group");
    const std::string long_name(300, 'x');
    CHECK(clean_group_name(long_name).size() == kMaxGroupName);
}
