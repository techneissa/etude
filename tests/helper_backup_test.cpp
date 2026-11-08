// SPDX-License-Identifier: GPL-3.0-or-later
// Backing up and restoring the app's data around an update: what is copied, what a planted link or a
// full disk does, and that a restore can be run again.
#include "../helper/backup.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

using namespace etude::helper;
using namespace upload_test;

namespace {

struct Data {
    TempDir root;
    fs::path dir = root.path / "data";
    Data() {
        write_file(dir / "history.db", "db-1.1");
        write_file(dir / "history.db-wal", "wal-1.1");
        write_file(dir / "settings.conf", "pins");
        write_file(dir / "settings.conf.bak", "old pins");
    }
    std::string backup(const std::string& v = "1.1.0") { return backup_data(dir.string(), v); }
    std::string restore(const std::string& v = "1.1.0") { return restore_data(dir.string(), v); }
    fs::path saved(const std::string& v = "1.1.0") const { return dir / "update-backup" / v; }
};

} // namespace

TEST_CASE("backup: the history and the settings are copied, with their WAL and backup files") {
    Data d;
    CHECK(d.backup().empty());
    CHECK(read_file(d.saved() / "history.db") == "db-1.1");
    CHECK(read_file(d.saved() / "history.db-wal") == "wal-1.1");
    CHECK(read_file(d.saved() / "settings.conf") == "pins");
    CHECK(read_file(d.saved() / "settings.conf.bak") == "old pins");
    CHECK_FALSE(fs::exists(d.saved() / "history.db-shm")); // what is not there is not made up
    CHECK(fs::exists(d.saved() / ".complete"));
    CHECK(tree(d.dir / "update-backup").find(".new") == std::string::npos);
}

TEST_CASE("backup: the newest backup replaces the older ones") {
    Data d;
    CHECK(d.backup("1.0.0").empty());
    write_file(d.dir / "history.db", "db-1.2");
    CHECK(d.backup("1.2.0").empty());
    CHECK_FALSE(fs::exists(d.saved("1.0.0")));
    CHECK(read_file(d.saved("1.2.0") / "history.db") == "db-1.2");
    // The same version again replaces its own.
    write_file(d.dir / "history.db", "db-1.2b");
    CHECK(d.backup("1.2.0").empty());
    CHECK(read_file(d.saved("1.2.0") / "history.db") == "db-1.2b");
}

TEST_CASE("backup: no data folder means nothing to back up, and no error") {
    TempDir t;
    CHECK(backup_data((t.path / "none").string(), "1.0.0").empty());
    CHECK_FALSE(fs::exists(t.path / "none"));
}

TEST_CASE("backup: a link planted where a data file is makes the backup fail, and is not read through") {
    for (const char* name : {"history.db", "history.db-wal", "history.db-shm", "settings.conf", "settings.conf.bak"}) {
        CAPTURE(name);
        Data d;
        CHECK(d.backup("1.0.0").empty()); // an older backup that must survive
        write_file(d.root.path / "secret", "root only");
        fs::remove(d.dir / name);
        fs::create_symlink(d.root.path / "secret", d.dir / name);
        const std::string why = d.backup("1.1.0");
        CHECK_FALSE(why.empty());
        CHECK(why.find(name) != std::string::npos);
        CHECK(fs::exists(d.saved("1.0.0") / ".complete"));        // the older backup survives
        CHECK_FALSE(fs::exists(d.saved("1.1.0")));
        CHECK(tree(d.dir / "update-backup").find(".new") == std::string::npos); // no half backup left
        CHECK(read_file(d.root.path / "secret") == "root only");
    }
}

TEST_CASE("backup: a link planted at the backup folder or at its temporary name does not redirect the copy") {
    Data d;
    write_file(d.root.path / "elsewhere" / "x", "x");
    fs::create_symlink(d.root.path / "elsewhere", d.dir / "update-backup");
    CHECK_FALSE(d.backup().empty());
    CHECK(tree(d.root.path / "elsewhere") == "x\n");
    fs::remove(d.dir / "update-backup");
    fs::create_directories(d.dir / "update-backup");
    fs::create_symlink(d.root.path / "elsewhere", d.dir / "update-backup" / "1.1.0.new");
    CHECK(d.backup().empty()); // the link is removed, not followed
    CHECK(tree(d.root.path / "elsewhere") == "x\n");
    CHECK(read_file(d.saved() / "history.db") == "db-1.1");
}

TEST_CASE("restore: the data is put back as it was, and what the backup lacks is removed") {
    Data d;
    CHECK(d.backup().empty());
    // The new version migrated the database, made a shared-memory file and changed the settings.
    write_file(d.dir / "history.db", "db-1.2");
    write_file(d.dir / "history.db-wal", "wal-1.2");
    write_file(d.dir / "history.db-shm", "shm-1.2");
    write_file(d.dir / "settings.conf", "new pins");
    CHECK(d.restore().empty());
    CHECK(read_file(d.dir / "history.db") == "db-1.1");
    CHECK(read_file(d.dir / "history.db-wal") == "wal-1.1");
    CHECK(read_file(d.dir / "settings.conf") == "pins");
    CHECK_FALSE(fs::exists(d.dir / "history.db-shm")); // the new version's file does not meet the old database
    CHECK(tree(d.dir).find(".restore") == std::string::npos);
    CHECK(d.restore().empty()); // and again: the same
    CHECK(read_file(d.dir / "history.db") == "db-1.1");
}

TEST_CASE("restore: a link planted at a data file or a temporary name is replaced, never written through") {
    Data d;
    CHECK(d.backup().empty());
    write_file(d.root.path / "victim", "keep");
    fs::remove(d.dir / "history.db");
    fs::create_symlink(d.root.path / "victim", d.dir / "history.db");
    fs::create_symlink(d.root.path / "victim", d.dir / ".settings.conf.restore");
    CHECK(d.restore().empty());
    CHECK(read_file(d.root.path / "victim") == "keep");
    CHECK_FALSE(fs::is_symlink(d.dir / "history.db"));
    CHECK(read_file(d.dir / "history.db") == "db-1.1");
}

TEST_CASE("restore: a backup that is a link, missing or not complete touches nothing") {
    Data d;
    CHECK(d.backup().empty());
    write_file(d.dir / "history.db", "db-1.2");
    fs::remove(d.saved() / ".complete");
    CHECK_FALSE(d.restore().empty());
    CHECK(read_file(d.dir / "history.db") == "db-1.2");
    CHECK_FALSE(d.restore("9.9.9").empty());
    fs::remove_all(d.saved());
    write_file(d.root.path / "decoy" / "history.db", "decoy");
    write_file(d.root.path / "decoy" / ".complete", "x");
    fs::create_symlink(d.root.path / "decoy", d.saved());
    CHECK_FALSE(d.restore().empty());
    CHECK(read_file(d.dir / "history.db") == "db-1.2");
}

TEST_CASE("restore: a cut between the old backup's removal and the new one's rename is still restorable") {
    Data d;
    CHECK(d.backup().empty());
    fs::rename(d.saved(), d.dir / "update-backup" / "1.1.0.new"); // as if the rename never happened
    write_file(d.dir / "history.db", "db-1.2");
    CHECK(d.restore().empty());
    CHECK(read_file(d.dir / "history.db") == "db-1.1");
}

TEST_CASE("restore: a failed copy leaves the data as it was") {
    Data d;
    CHECK(d.backup().empty());
    write_file(d.dir / "history.db", "db-1.2");
    // A data file that cannot be copied: the backup's copy is a folder.
    fs::remove(d.saved() / "settings.conf");
    fs::create_directories(d.saved() / "settings.conf");
    const std::string why = d.restore();
    CHECK_FALSE(why.empty());
    CHECK(read_file(d.dir / "history.db") == "db-1.2");
    CHECK(tree(d.dir).find(".restore") == std::string::npos);
}
