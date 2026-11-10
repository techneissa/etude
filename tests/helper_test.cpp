// SPDX-License-Identifier: GPL-3.0-or-later
// The root helper that applies app updates: names, links and pipes refused, backups never written
// through links, a failed start rolls back, and the app's files are never trusted.
#include "../helper/apply.hpp"
#include "../helper/backup.hpp"
#include "../helper/context.hpp"
#include "../helper/handoff_files.hpp"
#include "../helper/install.hpp"
#include "../helper/interrupt.hpp"
#include "../helper/layout.hpp"
#include "../helper/process.hpp"
#include "../helper/safe_io.hpp"
#include "../helper/sandbox.hpp"
#include "../helper/service.hpp"
#include "../helper/settle.hpp"
#include "../helper/state_store.hpp"
#include "../helper/stream.hpp"
#include "../helper/tree_check.hpp"
#include "upload_test_support.hpp"

#include "etude/update_handoff.hpp"
#include "etude/update_key.hpp"

#include <doctest/doctest.h>

#include <fcntl.h>
#include <functional>
#include <pwd.h>
#include <sys/stat.h>
#include <vector>

namespace etude::helper {
void note(const std::string&) {} // the helper's log: dropped
} // namespace etude::helper

namespace {

using namespace etude;
using namespace etude::helper;
using namespace upload_test;

void make_fifo(const fs::path& p) { REQUIRE(::mkfifo(p.c_str(), 0600) == 0); }
void mode(const fs::path& p, mode_t m) { ::chmod(p.c_str(), m); }

// A temporary root with a prefix holding versions 1.0.0 (previous), 1.1.0 (current) and 1.2.0, the
// app's data, and a stand-in systemctl. Files next to the stand-in change what it does: `answer` is
// what is-active prints for every unit (exit 3), `bus_error` makes is-active fail, `hang` makes
// stop slow, `stuck` makes stop do nothing, `start_fails(_once)` makes start fail, `term_on_<cmd>`
// makes that command SIGTERM the helper once, `failed` names a crashed unit, `show_answer` is what
// show prints for it.
struct Rig {
    TempDir root;
    Context c;
    fs::path prefix = root.path / "opt";
    fs::path data = root.path / "home" / ".local/share/io.github.techneissa.etude";

    Rig() {
        fs::create_directories(prefix / "update");
        mode(prefix, 0755);
        const passwd* entry = ::getpwuid(::geteuid());
        c.conf.prefix = prefix.string();
        c.ancestor_floor = fs::canonical(root.path).string(); // /tmp is world-writable
        c.conf.state_dir = (root.path / "state").string();
        c.ready_timeout_s = 0.1;
        c.kill_nap_ms = 20;
        c.watch_poll_ms = 20;
        c.service_timeout_s = 2;
        reset_interrupt();
        install_interrupt_handlers(); // the stand-in's SIGTERM is handled, as in the helper
        c.user.name = entry->pw_name;
        c.user.uid = entry->pw_uid;
        c.user.gid = entry->pw_gid;
        c.user.home = (root.path / "home").string();
        c.conf.systemctl = (root.path / "systemctl").string();
        write_script(
            c.conf.systemctl,
            "#!/bin/sh\n"
            "dir=${0%/*}\n"
            "echo \"$*\" >>\"$dir/calls\"\n"
            "if [ -e \"$dir/term_on_$1\" ]; then rm -f \"$dir/term_on_$1\"; kill -TERM $PPID; fi\n"
            "case \"$1\" in\n"
            "is-active) f=; a=\n"
            "  [ -e \"$dir/failed\" ] && read -r f <\"$dir/failed\"\n"
            "  [ -e \"$dir/active\" ] && read -r a <\"$dir/active\"\n"
            "  if [ -e \"$dir/answer\" ]; then while IFS= read -r w || [ -n \"$w\" ]; do echo "
            "\"$w\"; done <\"$dir/answer\"; exit 3;\n"
            "  elif [ -e \"$dir/bus_error\" ]; then exit 1;\n"
            "  elif [ \"$f\" = \"$2\" ]; then echo failed; exit 3;\n"
            "  elif [ \"$a\" = \"$2\" ]; then echo active;\n"
            "  else echo inactive; exit 3; fi ;;\n"
            "stop) [ -e \"$dir/hang\" ] && sleep 5\n"
            "  [ -e \"$dir/stuck\" ] || rm -f \"$dir/active\" ;;\n"
            "show) case \"$*\" in *Result*) [ -e \"$dir/show_answer\" ] && cat "
            "\"$dir/show_answer\" ;; esac ;;\n"
            "start|restart) for last; do :; done\n"
            "  if [ -e \"$dir/start_fails_once\" ]; then rm -f \"$dir/start_fails_once\"; exit 1; "
            "fi\n"
            "  [ -e \"$dir/start_fails\" ] && exit 1\n"
            "  echo \"$last\" >\"$dir/active\" ;;\n"
            "esac\n");
        for (const char* v : {"1.0.0", "1.1.0", "1.2.0"}) {
            put_version(v);
        }
        std::string error;
        point_link(c, "current", "1.1.0", error);
        point_link(c, "previous", "1.0.0", error);
        write_file(data / "history.db", "db-1.1");
        write_file(data / "history.db-wal", "wal-1.1");
        write_file(data / "settings.conf", "pins");
    }

    void put_version(const std::string& v) {
        write_file(prefix / "versions" / v / "bin" / "etude", "app " + v);
        write_file(prefix / "versions" / v / "share" / "etude" / "x", "x");
    }
    std::string calls() const { return read_file(root.path / "calls"); }
    void run_service(const std::string& unit) { write_file(root.path / "active", unit + "\n"); }
    // The unit crashed (a SEGV): is-active says failed and nothing runs.
    void crash_service(const std::string& unit) {
        fs::remove(root.path / "active");
        write_file(root.path / "failed", unit + "\n");
        write_file(root.path / "show_answer", "Result=signal\nExecMainCode=3\nExecMainStatus=11\n");
    }
    void app_says_ready(const std::string& version) {
        write_file(prefix / "update" / "ready", "version=" + version + "\n");
    }
    // 1.1.0 is the old version, 1.2.0 has just been switched to, with the data backed up.
    PendingUpdate start_trial(int starts = 0) {
        CHECK(backup_data(data.string(), "1.1.0").empty());
        write_file(data / "history.db", "db-1.2"); // the new version migrated
        write_file(data / "history.db-shm", "shm-1.2");
        PendingUpdate s;
        s.phase = UpdatePhase::Pending;
        s.from = "1.1.0";
        s.to = "1.2.0";
        s.previous = "1.0.0";
        s.unit = "etude.service";
        s.starts = starts;
        s.backed_up = true;
        std::string error;
        CHECK(save_pending(c, s, error));
        CHECK(point_link(c, "current", "1.2.0", error));
        CHECK(point_link(c, "previous", "1.1.0", error));
        return s;
    }
    PendingUpdate state() {
        std::string error;
        return load_pending(c, error);
    }
    UpdateStatus status() { return parse_update_status(read_file(prefix / "update" / "status")); }
    std::string current() { return link_version(c, "current"); }
    std::string previous() { return link_version(c, "previous"); }
};

// The old version is in use as it was, and no trace of the update is left.
void expect_old_version_in_use(Rig& r) {
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous() == "1.0.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(fs::exists(r.prefix / "versions" / "1.1.0"));
    CHECK(read_file(r.root.path / "active") == "etude.service\n"); // the old app runs
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK_FALSE(fs::exists(r.prefix / "update" / "ready"));
}

FileLimits limits(std::uint64_t max = 1024) {
    FileLimits l;
    l.max_bytes = max;
    return l;
}

Fd dir_of(const fs::path& p) {
    std::string error;
    Fd fd = open_directory(p.string(), error);
    REQUIRE(fd.ok());
    return fd;
}

} // namespace

TEST_CASE("helper io: only a plain file name is taken") {
    CHECK(plain_name("request"));
    CHECK(plain_name("release.tar.gz.minisig"));
    for (const char* bad : {"", ".", "..", "a/b", "/etc", "a\nb", "a\x7f"}) {
        CAPTURE(bad);
        CHECK_FALSE(plain_name(bad));
    }
}

TEST_CASE(
    "helper io: a link, a pipe, a folder, a name with a slash, a big or shared file is refused") {
    TempDir t;
    write_file(t.path / "outside", "secret");
    fs::create_directories(t.path / "d" / "sub");
    fs::create_symlink(t.path / "outside", t.path / "d" / "link");
    fs::create_symlink("/etc/passwd", t.path / "d" / "abs");
    make_fifo(t.path / "d" / "pipe");
    const Fd d = dir_of(t.path / "d");
    for (const char* name : {"link", "abs", "pipe", "sub"}) {
        CAPTURE(name);
        CHECK(open_plain_file(d.get(), name, limits()).status == OpenStatus::NotPlain);
    }
    CHECK(open_plain_file(d.get(), "../outside", limits()).status == OpenStatus::Failed);
    CHECK(open_plain_file(d.get(), "sub/x", limits()).status == OpenStatus::Failed);

    write_file(t.path / "big", std::string(100, 'x'));
    write_file(t.path / "one", "1");
    fs::create_hard_link(t.path / "one", t.path / "two");
    write_file(t.path / "open", "1");
    mode(t.path / "open", 0666);
    const Fd dir = dir_of(t.path);
    CHECK(open_plain_file(dir.get(), "big", limits(99)).status == OpenStatus::TooLarge);
    CHECK(open_plain_file(dir.get(), "big", limits(100)).status == OpenStatus::Ok);
    FileLimits mine = limits();
    mine.check_owner = true;
    mine.owner = ::geteuid();
    CHECK(open_plain_file(dir.get(), "big", mine).status == OpenStatus::Ok);
    CHECK(open_plain_file(dir.get(), "one", mine).status ==
          OpenStatus::WrongOwner); // two hard links
    mine.owner = ::geteuid() + 1;
    CHECK(open_plain_file(dir.get(), "big", mine).status == OpenStatus::WrongOwner);
    FileLimits closed = limits();
    closed.allow_group_other_write = false;
    CHECK(open_plain_file(dir.get(), "open", closed).status == OpenStatus::WrongOwner);
    CHECK(open_plain_file(dir.get(), "big", closed).status == OpenStatus::Ok);
}

TEST_CASE("helper io: a copy is a new file, never through a planted link; a tree is removed "
          "without following links") {
    TempDir t;
    write_file(t.path / "src", "payload");
    write_file(t.path / "victim", "keep");
    fs::create_directories(t.path / "to");
    const Fd to = dir_of(t.path / "to");
    const Fd from = dir_of(t.path);
    std::string error;
    const auto copy = [&](const char* name, std::uint64_t max) {
        PlainFile src = open_plain_file(from.get(), "src", limits());
        return copy_to_new_file(src.fd.get(), to.get(), name, max, error);
    };
    CHECK(copy("copy", 100));
    CHECK(read_file(t.path / "to" / "copy") == "payload");
    CHECK((fs::status(t.path / "to" / "copy").permissions() & fs::perms::others_read) ==
          fs::perms::none);
    CHECK_FALSE(copy("copy", 100)); // an existing name
    fs::create_symlink(t.path / "victim", t.path / "to" / "planted");
    CHECK_FALSE(copy("planted", 100));
    CHECK(read_file(t.path / "victim") == "keep");
    CHECK_FALSE(copy("big", 3)); // larger than allowed: no file is left
    CHECK_FALSE(fs::exists(t.path / "to" / "big"));

    TempDir u; // a folder of its own, so the tree listing is only what this part leaves
    write_file(u.path / "keep" / "precious", "x");
    write_file(u.path / "doomed" / "a" / "b", "x");
    fs::create_symlink(u.path / "keep", u.path / "doomed" / "a" / "to-keep");
    fs::create_symlink(u.path / "keep", u.path / "link-to-keep");
    const Fd tree_dir = dir_of(u.path);
    CHECK(remove_tree_at(tree_dir.get(), "doomed", error));
    CHECK(remove_tree_at(tree_dir.get(), "link-to-keep", error));
    CHECK(remove_tree_at(tree_dir.get(), "never-there", error));
    CHECK(tree(u.path) == "keep/\nkeep/precious\n"); // no doomed/ and no link-to-keep left
}

namespace {

fs::path good_tree(const fs::path& base) {
    const fs::path t = base / "tree";
    write_file(t / "bin" / "etude", "app");
    mode(t / "bin" / "etude", 0755);
    write_file(t / "share" / "etude" / "sound.sf2", "samples");
    mode(t, 0755);
    return t;
}

std::string problem(const fs::path& t) { return tree_problem(t.string()); }

Context prefix_context(const fs::path& prefix) {
    Context c;
    c.conf.prefix = prefix.string();
    c.ancestor_floor =
        fs::weakly_canonical(prefix.parent_path()).string(); // /tmp is world-writable
    return c;
}

} // namespace

TEST_CASE("tree check: links that point out, at nothing or through each other are refused") {
    for (const char* target : {"/etc/passwd", "../..", "../../../etc", "missing"}) {
        CAPTURE(target);
        TempDir d;
        const fs::path t = good_tree(d.path);
        fs::create_symlink(target, t / "share" / "evil");
        CHECK(problem(t).find("share/evil") != std::string::npos);
    }
    // Each link alone reads as inside; together the second climbs out.
    TempDir d;
    const fs::path t = good_tree(d.path);
    fs::create_directories(t / "a");
    fs::create_symlink("..", t / "a" / "up");
    CHECK(problem(t).empty());
    fs::create_symlink("a/up/..", t / "c");
    CHECK(problem(t).find("leaves the folder") != std::string::npos);
}

TEST_CASE("tree check: pipes, set-uid files, shared files and hard links are refused") {
    struct Row {
        const char* word;
        std::function<void(const fs::path&)> spoil;
    };
    const std::vector<Row> rows = {
        {"not a plain file", [](const fs::path& t) { make_fifo(t / "share" / "pipe"); }},
        {"set-uid", [](const fs::path& t) { mode(t / "bin" / "etude", 04755); }},
        {"written by others",
         [](const fs::path& t) { mode(t / "share" / "etude" / "sound.sf2", 0666); }},
        {"hard link",
         [](const fs::path& t) {
             fs::create_hard_link(t / "share" / "etude" / "sound.sf2", t / "share" / "second");
         }},
        {"set-gid", [](const fs::path& t) { mode(t / "share", 02775); }},
    };
    for (const Row& row : rows) {
        CAPTURE(row.word);
        TempDir d;
        const fs::path t = good_tree(d.path);
        row.spoil(t);
        CHECK(problem(t).find(row.word) != std::string::npos);
        CHECK_FALSE(problem(t).empty());
    }
}

TEST_CASE("layout: the prefix and versions/ must be real folders that only root can write") {
    TempDir d;
    fs::create_directories(d.path / "opt");
    mode(d.path / "opt", 0755);
    Context c = prefix_context(d.path / "opt");
    CHECK(check_layout(c).empty());
    CHECK(fs::is_directory(d.path / "opt" / "versions"));
    mode(d.path / "opt", 0775);
    CHECK_FALSE(check_layout(c).empty());
    mode(d.path / "opt", 0755);
    mode(d.path / "opt" / "versions", 0777);
    CHECK_FALSE(check_layout(c).empty());
    mode(d.path / "opt" / "versions", 0755);
    fs::create_symlink(d.path / "opt", d.path / "via-link");
    CHECK_FALSE(check_layout(prefix_context(d.path / "via-link")).empty());
    CHECK_FALSE(check_layout(prefix_context(d.path / "absent")).empty());
}

TEST_CASE("layout: a folder above the prefix that others or the group can write to refuses it") {
    TempDir d;
    const fs::path top = fs::weakly_canonical(d.path);
    fs::create_directories(top / "a/b/opt");
    for (const char* dir : {"a", "a/b", "a/b/opt"}) {
        mode(top / dir, 0755);
    }
    Context c;
    c.conf.prefix = (top / "a/b/opt").string();
    c.ancestor_floor = top.string();
    c.user.uid = ::geteuid() + 1; // the app is another user than the one running the test
    CHECK(check_layout(c).empty());
    for (const char* dir : {"a", "a/b"}) { // two levels up and the one just above the prefix
        for (const mode_t m : {mode_t(0775), mode_t(0757), mode_t(0777)}) {
            mode(top / dir, m);
            CAPTURE(dir);
            CAPTURE(m);
            CHECK(check_layout(c).find("above the prefix") != std::string::npos);
        }
        mode(top / dir, 0755);
    }
    CHECK(check_layout(c).empty());
}

TEST_CASE("layout: current and previous are relative links, replaced in one rename") {
    TempDir d;
    fs::create_directories(d.path / "opt" / "versions" / "1.0.0");
    fs::create_directories(d.path / "opt" / "versions" / "1.1.0");
    const Context c = prefix_context(d.path / "opt");
    std::string error;
    CHECK(link_version(c, "current").empty());
    CHECK(point_link(c, "current", "1.0.0", error));
    CHECK(fs::read_symlink(d.path / "opt" / "current") == "versions/1.0.0");
    CHECK(link_version(c, "current") == "1.0.0");
    CHECK(point_link(c, "current", "1.1.0", error));
    CHECK(link_version(c, "current") == "1.1.0");
    CHECK(tree(d.path / "opt").find(".new") == std::string::npos);
    CHECK(point_link(c, "current", "", error)); // removes it
    CHECK_FALSE(fs::is_symlink(d.path / "opt" / "current"));
    CHECK(point_link(c, "current", "", error)); // nothing to remove is fine
    CHECK_FALSE(point_link(c, "current", "../../etc", error));
    CHECK_FALSE(point_link(c, "current", "1.0", error));
    // A link that is not a plain version is not read as one.
    fs::create_symlink("/etc", d.path / "opt" / "previous");
    CHECK(link_version(c, "previous").empty());
    fs::remove(d.path / "opt" / "previous");
    fs::create_symlink("versions/1.0.0-rc.1", d.path / "opt" / "previous");
    CHECK(link_version(c, "previous").empty());
}

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

TEST_CASE(
    "backup: a link planted where a data file is makes the backup fail, and is not read through") {
    for (const char* name :
         {"history.db", "history.db-wal", "history.db-shm", "settings.conf", "settings.conf.bak"}) {
        CAPTURE(name);
        Data d;
        CHECK(d.backup("1.0.0").empty()); // an older backup that must survive
        write_file(d.root.path / "secret", "root only");
        fs::remove(d.dir / name);
        fs::create_symlink(d.root.path / "secret", d.dir / name);
        const std::string why = d.backup("1.1.0");
        CHECK(why.find(name) != std::string::npos);
        CHECK(fs::exists(d.saved("1.0.0") / ".complete"));
        CHECK_FALSE(fs::exists(d.saved("1.1.0")));
        CHECK(tree(d.dir / "update-backup").find(".new") ==
              std::string::npos); // no half backup left
        CHECK(read_file(d.root.path / "secret") == "root only");
    }
}

TEST_CASE("backup: a link planted at the backup folder or at its temporary name does not redirect "
          "the copy") {
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
    write_file(d.dir / "history.db", "db-1.2"); // the new version migrated and changed the settings
    write_file(d.dir / "history.db-wal", "wal-1.2");
    write_file(d.dir / "history.db-shm", "shm-1.2");
    write_file(d.dir / "settings.conf", "new pins");
    for (int twice = 0; twice < 2; ++twice) { // restoring again gives the same
        CHECK(d.restore().empty());
        CHECK(read_file(d.dir / "history.db") == "db-1.1");
        CHECK(read_file(d.dir / "history.db-wal") == "wal-1.1");
        CHECK(read_file(d.dir / "settings.conf") == "pins");
        CHECK_FALSE(fs::exists(d.dir / "history.db-shm")); // it does not meet the old database
        CHECK(tree(d.dir).find(".restore") == std::string::npos);
    }
}

TEST_CASE("restore: a link planted at a data file or a temporary name is replaced, never written "
          "through") {
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

TEST_CASE("process: a program runs with no shell, its output is kept and a slow one is killed") {
    const ProgramResult echoed = run_program({"/bin/echo", "a;b", "$HOME"}, 10);
    CHECK(echoed.ran);
    CHECK(echoed.exit_code == 0);
    CHECK(echoed.output == "a;b $HOME\n");
    CHECK(run_program({"/usr/bin/false"}, 10).exit_code == 1);
    const ProgramResult slow = run_program({"/bin/sleep", "30"}, 0.2);
    CHECK(slow.timed_out);
    CHECK(slow.exit_code == -1);
    CHECK_FALSE(run_program({"sleep", "1"}, 0.2).ran); // a program is named by its full path
    CHECK(run_program({"/nonexistent/program"}, 5).exit_code == 127);
}

TEST_CASE("process: the app's user is looked up, and root is refused") {
    AppUser user;
    std::string error;
    CHECK_FALSE(find_app_user("root", user, error));
    CHECK(error.find("root") != std::string::npos);
    CHECK_FALSE(find_app_user("no-such-user-here", user, error));
    const passwd* entry = ::getpwuid(::geteuid());
    REQUIRE(entry != nullptr);
    if (::geteuid() != 0) {
        CHECK(find_app_user(entry->pw_name, user, error));
        CHECK(user.uid == ::geteuid());
    }
}

TEST_CASE("service: only inactive and failed count as stopped; every other answer is running") {
    Rig r;
    r.c.service_timeout_s =
        90; // this test is about the words; a slow shell start must not look like a timeout
    const Service service(r.c);
    for (const char* word : {"inactive", "failed"}) {
        CAPTURE(word);
        write_file(r.root.path / "answer", std::string(word) + "\n");
        CHECK_FALSE(service.running("etude.service"));
        r.run_service("etude.service");
        std::string error;
        CHECK(service.stop("etude.service", error)); // the word after the stop is what counts
    }
    for (const char* word : {"activating", "deactivating", "active", "reloading", "refreshing",
                             "maintenance", "something-new"}) {
        CAPTURE(word);
        write_file(r.root.path / "answer", std::string(word) + "\n");
        CHECK(service.running("etude.service"));
        CHECK(service.active_unit() == "etude.service");
        std::string error;
        CHECK_FALSE(service.stop("etude.service", error));
        CHECK(error.find(word) != std::string::npos);
    }
    // More than one word, or none, is no answer, and no answer is not "stopped".
    write_file(r.root.path / "answer", "inactive\nactive\n");
    CHECK(service.running("etude.service"));
    write_file(r.root.path / "answer", "\n");
    CHECK(service.running("etude.service"));
}

TEST_CASE("service: a bus that does not answer, or a stop that hangs, fails closed") {
    Rig r;
    r.c.service_timeout_s = 0.3; // the hung stop only; the calls that ask keep 2 s
    r.c.query_floor_s = 2;
    const Service service(r.c);
    write_file(r.root.path / "bus_error", "");
    CHECK(service.state("etude.service").empty());
    CHECK(service.running("etude.service"));
    CHECK(service.active_unit() == "etude.service");
    std::string error;
    CHECK_FALSE(service.stop("etude.service", error));
    CHECK(error.find("could not be read") != std::string::npos);
    fs::remove(r.root.path / "bus_error");
    // A stop that outlasts the helper's patience is killed, and the state afterwards decides: the
    // unit is seen still active, or (a slow shell start) the answer did not come in time. Both fail
    // closed.
    write_file(r.root.path / "hang", "");
    r.run_service("etude.service");
    CHECK_FALSE(service.stop("etude.service", error));
    CHECK((error.find("is active") != std::string::npos ||
           error.find("could not be read") != std::string::npos));
}

TEST_CASE("service: a data folder reached through a link the app made is refused") {
    Rig r;
    const Service service(r.c);
    const fs::path home = r.root.path / "home";
    fs::create_directories(r.root.path / "elsewhere");
    fs::create_directories(home / ".local/share");
    const fs::path folder =
        home / ".local/share" / kAppDataFolderName; // ReadWritePaths would follow a link
    fs::remove_all(folder);
    fs::create_directory_symlink(r.root.path / "elsewhere", folder);
    if (::geteuid() == 0) {
        REQUIRE(::lchown(folder.c_str(), 65534, 65534) ==
                0); // ci-linux runs as root: make the link the app's
    }
    CHECK(data_folder_for(r.c, service, "etude.service").empty());
    fs::remove(folder);
    fs::create_directories(folder);
    CHECK(data_folder_for(r.c, service, "etude.service") == folder.string());
}

TEST_CASE("state store: a state file that cannot be read is set aside, and counts as none") {
    Rig r;
    std::string error;
    write_file(r.root.path / "state" / "state", "phase=pending\nfrom=banana\n");
    mode(r.root.path / "state", 0700);
    CHECK(load_pending(r.c, error).phase == UpdatePhase::None);
    CHECK_FALSE(error.empty());
    CHECK(fs::exists(r.root.path / "state" / "state.unreadable"));
    CHECK_FALSE(fs::exists(r.root.path / "state" / "state"));
}

TEST_CASE("state store: a state folder that others can enter or that is a link is refused") {
    Rig r;
    fs::create_directories(r.root.path / "state");
    mode(r.root.path / "state", 0755);
    std::string error;
    CHECK_FALSE(open_state_dir(r.c, error).ok());
    CHECK(error.find("root's alone") != std::string::npos);
    fs::remove(r.root.path / "state");
    fs::create_directories(r.root.path / "elsewhere");
    mode(r.root.path / "elsewhere", 0700);
    fs::create_symlink(r.root.path / "elsewhere", r.root.path / "state");
    CHECK_FALSE(open_state_dir(r.c, error).ok());
}

TEST_CASE("sandbox: a file that is not plain data, or not root's alone, is refused") {
    TempDir t;
    std::vector<std::string> properties;
    std::string error;
    const auto load = [&](const std::string& text, mode_t m = 0644, uid_t owner = ::geteuid()) {
        write_file(t.path / "s.conf", text);
        mode(t.path / "s.conf", m);
        return load_sandbox((t.path / "s.conf").string(), properties, error, owner);
    };
    CHECK(load("ProtectSystem=strict\n"));
    CHECK_FALSE(load("ProtectSystem=strict\nrm -rf /\n"));
    CHECK_FALSE(load("ProtectSystem=strict\n$(id)=1\n"));
    CHECK_FALSE(load("=strict\n"));
    CHECK_FALSE(load("[Unit]\nProtectSystem=strict\n"));
    CHECK_FALSE(load("# nothing but a comment\n[Service]\n"));
    CHECK_FALSE(load("ProtectSystem=str\x01ict\n"));
    CHECK_FALSE(load("ProtectSystem=strict\n", 0666));                  // anyone may write it
    CHECK_FALSE(load("ProtectSystem=strict\n", 0664));                  // its group may
    CHECK_FALSE(load("ProtectSystem=strict\n", 0644, ::geteuid() + 1)); // another user's
    CHECK_FALSE(load_sandbox((t.path / "missing.conf").string(), properties, error, ::geteuid()));
    std::string many;
    for (int i = 0; i < 100; ++i) {
        many += "Prop" + std::to_string(i) + "=yes\n";
    }
    CHECK_FALSE(load(many));
}

TEST_CASE("sandbox: a run that started and then went wrong is not run again in place") {
    TempDir root;
    Context c;
    c.conf.systemctl = (root.path / "systemctl").string();
    write_script(c.conf.systemctl, "#!/bin/sh\n:\n");
    reset_interrupt();
    // `body` is what the stand-in systemd-run does after the program said it began (the marker).
    const auto run = [&](const std::string& mode_name, const std::string& body) {
        write_script(root.path / "systemd-run",
                     "#!/bin/sh\necho '" + std::string(kSandboxStartMarker) + "'\n" + body + "\n");
        SandboxRun r;
        r.systemd_run = (root.path / "systemd-run").string();
        r.helper = "/usr/local/lib/etude/etude-update-helper";
        r.mode = mode_name;
        r.unit = "etude-helper-" + mode_name + "-1";
        r.limit_s = 30;
        r.properties = {"ProtectSystem=strict", "NoNewPrivileges=yes"};
        return run_sandboxed(c, r);
    };
    // systemd-run ends 1 when the unit ran and then crashed, hit RuntimeMaxSec or ran out of
    // memory: the mode is not repeated without the sandbox. 0, logged, and the next start tries
    // again.
    for (const char* m : {"count", "crashed", "idle"}) {
        CAPTURE(m);
        CHECK(run(m, "echo 'Process terminated with signal SEGV' >&2; exit 1") == 0);
        CHECK(run(m, "echo 'Failed with result timeout' >&2; exit 1") == 0);
        CHECK(run(m, "exit 137") == 0);
    }
    // The statuses of the work itself still pass.
    CHECK(run("count", "exit 75") == kRestoreUnfinished);
    CHECK(run("idle", "exit 10") == 1);
}

TEST_CASE("stream: a gzip is unpacked into a new file, and refused when it is bad or too large") {
    // gzip -9 of "hello\n".
    const std::string gz("\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\xcb\x48\xcd\xc9\xc9\xe7\x02\x00"
                         "\x20\x30\x3a\x36\x06\x00\x00\x00",
                         26);
    TempDir t;
    write_file(t.path / "a.gz", gz);
    const Fd dir = dir_of(t.path);
    std::string error;
    const auto unpack = [&](const std::string& gz_name, const std::string& out,
                            std::uint64_t limit) {
        Fd in(::open((t.path / gz_name).c_str(), O_RDONLY));
        REQUIRE(in.ok());
        return gunzip_to_file(in.get(), dir.get(), out, limit, error);
    };
    REQUIRE(unpack("a.gz", "a.out", 6));
    CHECK(read_file(t.path / "a.out") == "hello\n");
    CHECK_FALSE(unpack("a.gz", "a.out", 6)); // never over an existing file
    CHECK_FALSE(unpack("a.gz", "b.out", 5)); // too large, and no half file is left
    CHECK(error.find("more than it may") != std::string::npos);
    CHECK_FALSE(fs::exists(t.path / "b.out"));
    std::string bad = gz;
    bad[12] ^= 0x10;
    write_file(t.path / "bad.gz", bad);
    CHECK_FALSE(unpack("bad.gz", "c.out", 100));
    CHECK_FALSE(fs::exists(t.path / "c.out"));
    CHECK_FALSE(unpack("a.gz", "../x", 100)); // not a plain name
}

namespace {

struct Staged {
    Rig r;
    Fd update;
    Staged() {
        r.put_version("1.2.0");
        r.run_service("etude.service");
        std::string error;
        update = open_update_dir(r.c, error);
        REQUIRE(update.ok());
    }
    int install() {
        return install_staged(InstallJob{r.c, update.get(), "1.2.0", "1.1.0", "1.0.0"});
    }
};

} // namespace

TEST_CASE("install: a failed save of the state after the switch puts the links back and keeps the "
          "old version") {
    // Also when the prefix folder cannot be written out after each link: the renames happened, so
    // that is logged and not a failure (which would have the caller undo or remove what is in use).
    for (const bool fsync_fails : {false, true}) {
        CAPTURE(fsync_fails);
        fail_fsync_in_test(fsync_fails);
        Staged s;
        fail_save_in_test(
            2); // the Switching save, then the Pending one after the links moved: that fails
        CHECK(s.install() == 1);
        fail_save_in_test(0);
        CHECK(s.r.status().state == UpdateState::Failed);
        CHECK(s.r.status().reason.find("could not be switched") != std::string::npos);
        expect_old_version_in_use(s.r);
        CHECK(fs::exists(s.r.data / "update-backup" / "1.1.0" / ".complete")); // backed up before
        CHECK(s.r.calls().find("start etude.service") !=
              std::string::npos); // the old one started again
    }
    fail_fsync_in_test(false);
}

TEST_CASE("install: nothing that `current` or `previous` points at is removed, checked just before "
          "the removal") {
    Rig r;
    std::string error;
    REQUIRE(point_link(r.c, "current", "1.2.0", error));
    REQUIRE(point_link(r.c, "previous", "1.0.0", error));
    discard_version(r.c, "1.2.0");
    discard_version(r.c, "1.0.0");
    for (const char* v : {"1.2.0", "1.0.0"}) {
        CHECK(fs::exists(r.prefix / "versions" / v / "bin" / "etude"));
    }
    prune_versions(r.c, "1.1.0",
                   "1.1.0"); // the clean-up keeps them too, whatever it was told to keep
    for (const char* v : {"1.0.0", "1.1.0", "1.2.0"}) {
        CHECK(fs::exists(r.prefix / "versions" / v));
    }
    REQUIRE(point_link(r.c, "previous", "1.1.0", error));
    discard_version(r.c, "1.0.0"); // one that nothing points at goes
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.0.0"));
}

TEST_CASE("count: each start of a version on trial is counted, and the version stays current") {
    Rig r;
    r.start_trial();
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().starts == 1);
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().starts == 2);
    CHECK(r.state().phase == UpdatePhase::Pending);
    CHECK(r.current() == "1.2.0");
}

TEST_CASE("count: a start without ready beyond the limit goes back, data and all, before the app "
          "starts") {
    Rig r;
    r.start_trial(2);
    CHECK(count_start(r.c) == 0);
    CHECK(fs::exists(r.prefix / "versions" / "1.1.0"));
    CHECK_FALSE(fs::exists(r.data / "history.db-shm"));
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous() == "1.0.0");
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    const UpdateStatus s = r.status();
    CHECK(s.state == UpdateState::RolledBack);
    CHECK(s.version == "1.2.0");
    CHECK(s.from == "1.1.0");
    CHECK(s.reason.find("went back to 1.1.0") != std::string::npos);
    CHECK(r.calls().find("stop") ==
          std::string::npos); // the service is not stopped from its own start
    CHECK(r.calls().find("start") == std::string::npos);
    CHECK(count_start(r.c) == 0); // and a later start is not counted
    CHECK(r.state().phase == UpdatePhase::None);
}

TEST_CASE("count: a version that said ready is confirmed, not counted, and the others are pruned") {
    Rig r;
    r.start_trial(1);
    r.app_says_ready("1.2.0");
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.current() == "1.2.0");
    CHECK(r.status().state == UpdateState::Ok);
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.0.0")); // only two versions kept
    CHECK(fs::exists(r.prefix / "versions" / "1.1.0"));
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(read_file(r.data / "history.db") == "db-1.2"); // the new data stays
}

TEST_CASE(
    "count: a ready file for another version, a link, a pipe or a stranger's does not count") {
    for (const char* what : {"other", "link", "pipe", "text"}) {
        CAPTURE(what);
        Rig r;
        r.start_trial();
        const fs::path ready = r.prefix / "update" / "ready";
        if (std::string(what) == "other") {
            r.app_says_ready("1.1.0");
        } else if (std::string(what) == "link") {
            write_file(r.root.path / "elsewhere", "version=1.2.0\n");
            fs::create_symlink(r.root.path / "elsewhere", ready);
        } else if (std::string(what) == "pipe") {
            make_fifo(ready);
        } else {
            r.app_says_ready("1.2.0 and more text");
        }
        CHECK(count_start(r.c) == 0);
        CHECK(r.state().phase == UpdatePhase::Pending);
        CHECK(r.state().starts == 1);
    }
}

TEST_CASE("a power cut in the middle of going back is finished at the next start") {
    Rig r;
    r.start_trial(2);
    PendingUpdate s = r.state();
    s.phase = UpdatePhase::RollingBack;
    s.reason = "1.2.0 did not start";
    std::string error;
    REQUIRE(save_pending(r.c, s, error));
    point_link(r.c, "current", "1.1.0",
               error); // the cut came after the links went back, before the data
    point_link(r.c, "previous", "1.0.0", error);
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK_FALSE(fs::exists(r.data / "history.db-shm"));
    CHECK(r.status().state == UpdateState::RolledBack);
}

TEST_CASE("watch: out of starts, the service is stopped, the old version put back and the service "
          "started") {
    Rig r;
    r.c.conf.max_starts = 2;
    r.start_trial(2);
    r.run_service("etude.service");
    CHECK(watch_trial(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    const std::string calls = r.calls();
    CHECK(calls.find("stop etude.service") < calls.find("start etude.service"));
    CHECK(read_file(r.root.path / "active") == "etude.service\n");
    CHECK(r.status().state == UpdateState::RolledBack);
}

TEST_CASE(
    "apply: a request that is not exactly a version line is refused, and removed whatever it was") {
    for (const char* text :
         {"", "hello\n", "version=1.2\n", "version=1.2.3-rc.1\n", "version=../../etc\n",
          "version=1.2.0\nextra\n", "version=1.2.0 \n", "VERSION=1.2.0\n"}) {
        CAPTURE(text);
        Rig r;
        write_file(r.prefix / "update" / "request", text);
        CHECK(apply_update(r.c) == 1);
        CHECK(r.status().state == UpdateState::Failed);
        CHECK_FALSE(fs::exists(r.prefix / "update" / "request"));
        CHECK(r.current() == "1.1.0");
    }
}

TEST_CASE("apply: a request that is a link or a pipe is refused, and the link is not followed") {
    for (const bool pipe : {false, true}) {
        Rig r;
        const fs::path request = r.prefix / "update" / "request";
        if (pipe) {
            make_fifo(request); // returns: it does not wait for a writer
        } else {
            write_file(r.root.path / "elsewhere", "version=1.3.0\n");
            fs::create_symlink(r.root.path / "elsewhere", request);
        }
        CHECK(apply_update(r.c) == 1);
        CHECK(r.status().state == UpdateState::Failed);
        if (!pipe) {
            CHECK_FALSE(fs::exists(request));
            CHECK(fs::exists(r.root.path / "elsewhere"));
        }
    }
}

TEST_CASE("count and resume: a prefix that cannot be trusted is not worked in, and the app still "
          "starts") {
    Rig r;
    r.start_trial(5);
    mode(r.prefix, 0777);
    CHECK(count_start(r.c) == 0);
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.state().starts == 5); // nothing was counted, nothing undone
    CHECK(r.current() == "1.2.0");
}

TEST_CASE(
    "apply: the files the app leaves are taken by name, never through a link, a pipe or a folder") {
    for (const char* what : {"link", "pipe", "folder", "missing"}) {
        CAPTURE(what);
        Rig r;
        write_file(r.prefix / "update" / "request", "version=1.3.0\n");
        write_file(r.root.path / "elsewhere", "bytes");
        const fs::path release = r.prefix / "update" / "release.tar.gz";
        const std::string kind = what;
        if (kind == "link") {
            fs::create_symlink(r.root.path / "elsewhere", release);
        } else if (kind == "pipe") {
            make_fifo(release);
        } else if (kind == "folder") {
            fs::create_directories(release);
        }
        write_file(r.prefix / "update" / "release.tar.gz.minisig", "sig");
        CHECK(apply_update(r.c) == 1);
        const UpdateStatus s = r.status();
        CHECK(s.state == UpdateState::Failed);
        CHECK(s.version == "1.3.0");
        CHECK(r.current() == "1.1.0");
        CHECK(r.state().phase == UpdatePhase::None);
        CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.3.0"));
        CHECK(read_file(r.root.path / "elsewhere") == "bytes");
    }
}

TEST_CASE(
    "apply: a version that is already installed, or an update while one is on trial, is refused") {
    for (const char* version : {"1.1.0", "1.0.0"}) { // the current one, and the previous one
        Rig r;
        write_file(r.prefix / "update" / "request", std::string("version=") + version + "\n");
        CHECK(apply_update(r.c) == 1);
        CHECK(r.status().reason.find("already installed") != std::string::npos);
    }
    Rig r;
    r.start_trial();
    write_file(r.prefix / "update" / "request", "version=1.3.0\n");
    CHECK(apply_update(r.c) == 1);
    CHECK(r.status().reason.find("still being settled") != std::string::npos);
    CHECK(r.current() == "1.2.0");
}

TEST_CASE("apply: a prefix others can write to is not worked in") {
    Rig r;
    mode(r.prefix, 0777);
    write_file(r.prefix / "update" / "request", "version=1.3.0\n");
    CHECK(apply_update(r.c) == 1);
    // The request is consumed, so the path unit does not loop on it; nothing else is touched.
    CHECK_FALSE(fs::exists(r.prefix / "update" / "request"));
    CHECK_FALSE(fs::exists(r.prefix / "update" / "status"));
    CHECK(r.current() == "1.1.0");
}

TEST_CASE("apply: without the public key built in, nothing the app left is accepted") {
    if (update_key_ready()) {
        return; // the placeholder is what this case is about
    }
    Rig r;
    write_file(r.prefix / "update" / "request", "version=1.3.0\n");
    write_file(r.prefix / "update" / "release.tar.gz", "not a tarball");
    write_file(r.prefix / "update" / "release.tar.gz.minisig", "not a signature");
    CHECK(apply_update(r.c) == 1);
    CHECK(r.status().reason == "updates are not set up in this build");
    CHECK_FALSE(
        fs::exists(r.prefix / "update" / "release.tar.gz")); // taken out of the app's folder
    CHECK(r.current() == "1.1.0");
    CHECK(tree(r.root.path / "state").find("incoming") == std::string::npos);
}

TEST_CASE("count: a state file that cannot be read is not taken for no update: an unproven version "
          "goes back") {
    for (const char* damage : {"phase=pending\nfrom=banana\n", "\x01\x02 not a state", ""}) {
        CAPTURE(damage);
        Rig r;
        r.start_trial(1);
        write_file(r.root.path / "state" / "state", damage); // damaged; 1.2.0 never said ready
        mode(r.root.path / "state", 0700);
        CHECK(count_start(r.c) == 0);
        CHECK(r.current() == "1.1.0");
        CHECK(r.previous() == "1.0.0"); // recovered from the versions that are there
        CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
        CHECK(r.state().phase == UpdatePhase::None);
        CHECK(r.status().state == UpdateState::RolledBack);
        CHECK(r.status().reason.find("could not be read") != std::string::npos);
        CHECK(read_file(r.data / "history.db") == "db-1.1"); // and the data with it
        CHECK(fs::exists(r.root.path / "state" / "state.unreadable"));
    }
}

TEST_CASE("after a crash: a version on trial is gone back from at once, the data included, with no "
          "question asked") {
    Rig r;
    r.start_trial(1);
    r.crash_service("etude.service");
    CHECK(settle_after_crash(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK(r.status().state == UpdateState::RolledBack);
    CHECK(r.status().reason.find("closed unexpectedly") != std::string::npos);
    CHECK(r.status().data.empty());
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(r.calls().find("start") == std::string::npos); // the app is not started: the screen asks
}
