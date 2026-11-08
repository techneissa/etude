// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: groups, downloaded sources, and the splits and joins of versions.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

#include <chrono>

#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST_CASE("history: groups are saved edit by edit and come back") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        GroupEdit make = group_edit(GroupEdit::Kind::Create, 11, "Recital");
        make.group.position = 2;
        make.group.kind = GroupKind::Catalogue;
        make.group.source = "mutopia";
        store.edit_groups(make);
        store.edit_groups(group_edit(GroupEdit::Kind::Create, 12, "Warm-ups"));
        store.edit_groups(group_edit(GroupEdit::Kind::Add, 11, {}, 0xA));
        store.edit_groups(group_edit(GroupEdit::Kind::Add, 11, {}, 0xB));
        store.edit_groups(group_edit(GroupEdit::Kind::Add, 12, {}, 0xA));
        store.edit_groups(group_edit(GroupEdit::Kind::Drop, 11, {}, 0xB));
        store.edit_groups(group_edit(GroupEdit::Kind::Rename, 12, "Warm ups"));
        CHECK(store.groups().groups().size() == 2);
        REQUIRE(store.flush());
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    const LibraryGroups& g = again.groups();
    REQUIRE(g.groups().size() == 2);
    CHECK(g.groups()[0].name == "Warm ups");
    CHECK(g.find(11)->kind == GroupKind::Catalogue);
    CHECK(g.find(11)->source == "mutopia");
    CHECK(g.groups_of(0xA) == std::vector<std::uint64_t>{12, 11});
    CHECK_FALSE(g.contains(11, 0xB));
    // Removing a group takes its songs out with it, in the file too.
    again.edit_groups(group_edit(GroupEdit::Kind::Remove, 11));
    REQUIRE(again.flush());
    Raw raw(again.path());
    CHECK(raw.num("SELECT COUNT(*) FROM song_groups") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM song_group_members WHERE group_id = 11") == 0);
}

TEST_CASE("history: group edits made before the history is read go over what it had") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.edit_groups(group_edit(GroupEdit::Kind::Create, 5, "Exam"));
        store.edit_groups(group_edit(GroupEdit::Kind::Add, 5, {}, 0x1));
        store.edit_groups(group_edit(GroupEdit::Kind::Add, 5, {}, 0x2));
        REQUIRE(store.flush());
    }
    {
        HistoryStore again(dir.path, quiet());
        // Before the file is read: a new group, and a song dropped from the stored one.
        again.edit_groups(group_edit(GroupEdit::Kind::Create, 6, "Sight reading"));
        again.edit_groups(group_edit(GroupEdit::Kind::Drop, 5, {}, 0x1)); // not known yet: nothing
        again.edit_groups(group_edit(GroupEdit::Kind::Add, 6, {}, 0x2));
        again.open();
        REQUIRE(again.wait_until_settled(10s));
        REQUIRE(again.poll().loaded);
        const LibraryGroups& g = again.groups();
        REQUIRE(g.groups().size() == 2);
        CHECK(g.contains(5, 0x1)); // the drop came before the group was known: it changed nothing
        CHECK(g.contains(5, 0x2));
        CHECK(g.contains(6, 0x2));
        REQUIRE(again.flush());
    }
    HistoryStore third(dir.path, quiet());
    REQUIRE(open_now(third).empty());
    CHECK(third.groups().groups().size() == 2);
    CHECK(third.groups().contains(6, 0x2));
}

TEST_CASE("history: a downloaded song's source and licence are kept by its hash") {
    TempDir dir;
    SongOrigin s;
    s.catalogue = "piano-midi";
    s.id = "midis/chopin/chpn_op10_e01.mid";
    s.title = "Etudes, Opus 10: No. 1";
    s.composer = "Chopin";
    s.licence = "CC BY-SA 3.0 DE";
    s.licence_url = "https://creativecommons.org/licenses/by-sa/3.0/de/deed.en";
    s.credit = "Bernd Krueger, http://www.piano-midi.de";
    s.url = "https://www.piano-midi.de/midis/chopin/chpn_op10_e01.mid";
    s.page = "https://www.piano-midi.de/chopin.htm";
    s.downloaded_utc_s = 1'760'000'000;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.set_song_origin(0xFEDCBA9876543210ULL, s);
        store.set_song_origin(0, s); // no hash: ignored
        CHECK(store.song_origins().size() == 1);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.song_origins().size() == 1);
    CHECK(again.song_origins().at(0xFEDCBA9876543210ULL) == s);
    // Downloaded again: the latest wins.
    s.downloaded_utc_s += 60;
    again.set_song_origin(0xFEDCBA9876543210ULL, s);
    REQUIRE(again.flush());
    Raw raw(again.path());
    CHECK(raw.num("SELECT COUNT(*) FROM song_sources") == 1);
    CHECK(raw.num("SELECT downloaded_utc_s FROM song_sources") == 1'760'000'060);
}

TEST_CASE("history: the player's splits and joins of versions are kept by content hash") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.edit_version_overrides({VersionEdit::Kind::Split, 0xAAULL, 0});
        store.edit_version_overrides({VersionEdit::Kind::Join, 0xB2ULL, 0xB1ULL});
        store.edit_version_overrides({VersionEdit::Kind::Join, 0xC1ULL, 0xC2ULL});
        store.edit_version_overrides({VersionEdit::Kind::Split, 0xC1ULL, 0}); // takes the join away
        store.edit_version_overrides({VersionEdit::Kind::Split, 0, 0});       // nothing
        CHECK(store.version_overrides().split(0xAA));
        CHECK(store.version_overrides().joined(0xB1));
        CHECK_FALSE(store.version_overrides().joined(0xC2));
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    {
        HistoryStore again(dir.path, quiet());
        REQUIRE(open_now(again).empty());
        const VersionOverrides& o = again.version_overrides();
        CHECK(o.split(0xAA));
        CHECK(o.split(0xC1));
        REQUIRE(o.joins.size() == 1);
        CHECK(o.joins[0] == std::make_pair<std::uint64_t, std::uint64_t>(0xB1, 0xB2));
    }
    {
        // Edits made before the history loaded: this session's own state has nothing to clear yet,
        // but the file has, and a clear is laid over it and written all the same.
        HistoryStore early(dir.path, quiet());
        early.open();
        early.edit_version_overrides({VersionEdit::Kind::Clear, 0xAA, 0});
        early.edit_version_overrides({VersionEdit::Kind::Clear, 0xB1, 0});
        REQUIRE(early.wait_until_settled(10s));
        CHECK(early.poll().message.empty());
        CHECK_FALSE(early.version_overrides().split(0xAA));
        CHECK(early.version_overrides().joins.empty());
        CHECK(early.version_overrides().split(0xC1));
        REQUIRE(early.flush());
    }
    HistoryStore last(dir.path, quiet());
    REQUIRE(open_now(last).empty());
    CHECK_FALSE(last.version_overrides().split(0xAA));
    CHECK(last.version_overrides().split(0xC1));
    CHECK(last.version_overrides().joins.empty());
    Raw raw(last.path());
    CHECK(raw.num("SELECT COUNT(*) FROM version_overrides") == 1);
}

// ---- The played notes of a run
