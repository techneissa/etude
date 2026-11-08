// SPDX-License-Identifier: GPL-3.0-or-later
// The helper's look at a version folder it unpacked itself, and at <prefix>'s layout.
#include "../helper/context.hpp"
#include "../helper/layout.hpp"
#include "../helper/tree_check.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>
#include <unistd.h>

using namespace etude::helper;
using namespace upload_test;

namespace {

fs::path good_tree(const fs::path& base) {
    const fs::path t = base / "tree";
    write_file(t / "bin" / "etude", "app");
    ::chmod((t / "bin" / "etude").c_str(), 0755);
    write_file(t / "share" / "etude" / "sound.sf2", "samples");
    ::chmod(t.c_str(), 0755);
    return t;
}

std::string problem(const fs::path& t) { return tree_problem(t.string()); }

Context prefix_context(const fs::path& prefix) {
    Context c;
    c.conf.prefix = prefix.string();
    c.ancestor_floor = fs::weakly_canonical(prefix.parent_path()).string(); // /tmp is world-writable
    return c;
}

} // namespace

TEST_CASE("tree check: a version folder of files, folders and inside links passes") {
    TempDir d;
    const fs::path t = good_tree(d.path);
    CHECK(problem(t).empty());
    fs::create_symlink("../../bin/etude", t / "share" / "etude" / "inside");
    fs::create_symlink("bin", t / "alias");
    CHECK(problem(t).empty());
}

TEST_CASE("tree check: what an app needs must be there") {
    TempDir d;
    fs::path t = good_tree(d.path);
    fs::remove(t / "bin" / "etude");
    CHECK(problem(t) == "no bin/etude");
    write_file(t / "bin" / "etude", "app"); // not executable
    ::chmod((t / "bin" / "etude").c_str(), 0644);
    CHECK(problem(t) == "no bin/etude");
    ::chmod((t / "bin" / "etude").c_str(), 0755);
    fs::remove_all(t / "share");
    CHECK(problem(t) == "no share/etude");
    CHECK(tree_problem((d.path / "absent").string()) == "not a folder");
    fs::create_symlink(t, d.path / "link");
    CHECK(tree_problem((d.path / "link").string()) == "not a folder");
}

TEST_CASE("tree check: links that point out, at nothing or through each other are refused") {
    for (const char* target : {"/etc/passwd", "../..", "../../../etc", "missing"}) {
        CAPTURE(target);
        TempDir d;
        const fs::path t = good_tree(d.path);
        fs::create_symlink(target, t / "share" / "evil");
        const std::string why = problem(t);
        CHECK_FALSE(why.empty());
        CHECK(why.find("share/evil") != std::string::npos);
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
    {
        TempDir d;
        const fs::path t = good_tree(d.path);
        REQUIRE(::mkfifo((t / "share" / "pipe").c_str(), 0600) == 0);
        CHECK(problem(t).find("not a plain file") != std::string::npos);
    }
    {
        TempDir d;
        const fs::path t = good_tree(d.path);
        ::chmod((t / "bin" / "etude").c_str(), 04755);
        CHECK(problem(t).find("set-uid") != std::string::npos);
    }
    {
        TempDir d;
        const fs::path t = good_tree(d.path);
        ::chmod((t / "share" / "etude" / "sound.sf2").c_str(), 0666);
        CHECK(problem(t).find("written by others") != std::string::npos);
    }
    {
        TempDir d;
        const fs::path t = good_tree(d.path);
        fs::create_hard_link(t / "share" / "etude" / "sound.sf2", t / "share" / "second");
        CHECK(problem(t).find("hard link") != std::string::npos);
    }
    {
        TempDir d;
        const fs::path t = good_tree(d.path);
        ::chmod((t / "share").c_str(), 02775);
        CHECK_FALSE(problem(t).empty());
    }
}

TEST_CASE("layout: the prefix and versions/ must be real folders that only root can write") {
    TempDir d;
    fs::create_directories(d.path / "opt");
    ::chmod((d.path / "opt").c_str(), 0755);
    Context c = prefix_context(d.path / "opt");
    CHECK(check_layout(c).empty());
    CHECK(fs::is_directory(d.path / "opt" / "versions"));
    ::chmod((d.path / "opt").c_str(), 0775);
    CHECK_FALSE(check_layout(c).empty());
    ::chmod((d.path / "opt").c_str(), 0755);
    ::chmod((d.path / "opt" / "versions").c_str(), 0777);
    CHECK_FALSE(check_layout(c).empty());
    ::chmod((d.path / "opt" / "versions").c_str(), 0755);
    fs::create_symlink(d.path / "opt", d.path / "via-link");
    CHECK_FALSE(check_layout(prefix_context(d.path / "via-link")).empty());
    CHECK_FALSE(check_layout(prefix_context(d.path / "absent")).empty());
}

TEST_CASE("layout: a folder above the prefix that others or the app can write to refuses it") {
    TempDir d;
    const fs::path top = fs::weakly_canonical(d.path);
    fs::create_directories(top / "a" / "b" / "opt");
    for (const char* dir : {"a", "a/b", "a/b/opt"}) {
        ::chmod((top / dir).c_str(), 0755);
    }
    Context c;
    c.conf.prefix = (top / "a" / "b" / "opt").string();
    c.ancestor_floor = top.string();
    c.user.uid = ::geteuid() + 1; // the app is another user than the one running the test
    CHECK(check_layout(c).empty());
    // A folder two levels up that the group or the world can write: renaming it replaces the prefix.
    for (const mode_t mode : {mode_t(0775), mode_t(0757), mode_t(0777)}) {
        ::chmod((top / "a").c_str(), mode);
        const std::string why = check_layout(c);
        CAPTURE(mode);
        CHECK(why.find("above the prefix") != std::string::npos);
    }
    ::chmod((top / "a").c_str(), 0755);
    CHECK(check_layout(c).empty());
    // The same for the folder just above the prefix.
    ::chmod((top / "a" / "b").c_str(), 0770);
    CHECK_FALSE(check_layout(c).empty());
    ::chmod((top / "a" / "b").c_str(), 0755);
    // One the app owns: a user other than root (here, another than the one running the test) that is
    // the app's own.
    c.user.uid = ::geteuid();
    CHECK(check_layout(c).empty()); // the helper's own user owns it, which is root's place in service
    // A link in the path is resolved first; what it leads through is judged.
    fs::create_directory_symlink(top / "a" / "b", top / "via");
    Context linked = c;
    linked.conf.prefix = (top / "via" / "opt").string();
    CHECK(check_layout(linked).empty());
    ::chmod((top / "a").c_str(), 0777);
    CHECK_FALSE(check_layout(linked).empty()); // the real path goes through a/, which is writable
    ::chmod((top / "a").c_str(), 0755);
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

TEST_CASE("layout: leftovers of a cut-short update are cleaned, and old versions pruned") {
    TempDir d;
    const fs::path v = d.path / "opt" / "versions";
    for (const char* name : {"1.0.0", "1.1.0", "1.2.0", "1.3.0.new", ".incoming.x", "notes"}) {
        fs::create_directories(v / name);
        write_file(v / name / "f", "x");
    }
    write_file(d.path / "opt" / ".current.new", "x");
    const Context c = prefix_context(d.path / "opt");
    clean_leftovers(c);
    CHECK(tree(v) == "1.0.0/\n1.0.0/f\n1.1.0/\n1.1.0/f\n1.2.0/\n1.2.0/f\nnotes/\nnotes/f\n");
    CHECK_FALSE(fs::exists(d.path / "opt" / ".current.new"));
    CHECK(list_versions(c).size() == 3);
    prune_versions(c, "1.2.0", "1.0.0");
    CHECK(tree(v) == "1.0.0/\n1.0.0/f\n1.2.0/\n1.2.0/f\nnotes/\nnotes/f\n");
}
