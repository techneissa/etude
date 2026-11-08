// SPDX-License-Identifier: GPL-3.0-or-later
// The crash notice at a start: the newest report not told yet, shown once; the crash screen reads
// the newest report told or not and leaves its marks for the next start; the newest ten stay; the
// text the player reads is scrubbed.
#include "crash_notice.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <fstream>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

void write_report(const fs::path& dir, const std::string& name, const std::string& text) {
    fs::create_directories(dir / kCrashFolder);
    std::ofstream(dir / kCrashFolder / name, std::ios::binary) << text;
}

std::string name_at(std::int64_t t) {
    char out[kCrashNameChars];
    crash_report_name(t, out);
    return out;
}

const char* kText =
    "etude crash report\nVersion: 1.2.0\nOS: Linux 6.12 aarch64\nScreen: free play\nSong: \n"
    "Signal: SIGSEGV (11)\nTime (UTC): 20261020-153012\n\nStack trace:\n"
    "/home/pat/build/etude(+0x1a2b) [0x55aa]\n"
    "/opt/app/bin/etude(_ZN5etude3app5Scene4drawEv+0x14) [0x55ab]\n\n"
    "Log (the last lines, the newest last):\nopened /home/pat/Music/x.mid\nswitched to Mia\n";

ScrubRules rules_for(const fs::path& data) {
    ScrubRules r;
    r.home = "/home/pat";
    r.app_dirs = {data.string(), "/opt/app"};
    return r;
}

} // namespace

TEST_CASE("crash notice: a report is told once, at the next normal start") {
    TempDir t;
    write_report(t.path, name_at(1000), kText);
    const ScrubRules rules = rules_for(t.path);

    auto notice = start_crash_notice(t.path, rules);
    REQUIRE(notice.has_value());
    CHECK(notice->name == name_at(1000));
    CHECK(notice->summary.signal == "SIGSEGV (11)");
    CHECK(notice->url.rfind("https://github.com/techneissa/etude/issues/new?", 0) == 0);
    CHECK(notice->url.size() <= kCrashUrlMax);
    CHECK(notice->qr_url.size() <= kCrashQrUrlMax);

    // The start after that has nothing to tell; the report is still there to read.
    CHECK_FALSE(start_crash_notice(t.path, rules).has_value());
    CHECK(fs::exists(t.path / kCrashFolder / name_at(1000)));

    // A new crash is told, once.
    write_report(t.path, name_at(2000), kText);
    notice = start_crash_notice(t.path, rules);
    REQUIRE(notice.has_value());
    CHECK(notice->name == name_at(2000));
    CHECK_FALSE(start_crash_notice(t.path, rules).has_value());
}

TEST_CASE("crash notice: no folder, no reports, nothing to tell") {
    TempDir t;
    CHECK_FALSE(start_crash_notice(t.path, rules_for(t.path)).has_value());
    fs::create_directories(t.path / kCrashFolder);
    CHECK_FALSE(start_crash_notice(t.path, rules_for(t.path)).has_value());
}

TEST_CASE("crash notice: the crash screen's report is the newest one, told or not, and nothing is marked") {
    TempDir t;
    const ScrubRules rules = rules_for(t.path);
    CHECK_FALSE(latest_crash_notice(t.path, rules).has_value()); // no folder
    write_report(t.path, name_at(1000), kText);
    write_report(t.path, name_at(2000), kText);
    auto latest = latest_crash_notice(t.path, rules);
    REQUIRE(latest.has_value());
    CHECK(latest->name == name_at(2000));
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / kCrashSeenFile)); // looking marks nothing
    CHECK(latest->text.find("/home/pat") == std::string::npos);      // scrubbed like the notice
    CHECK(latest->url.rfind("https://github.com/techneissa/etude/issues/new?", 0) == 0);
    // Told already (the app's own notice at an earlier start): the screen can still send it.
    REQUIRE(start_crash_notice(t.path, rules).has_value());
    CHECK_FALSE(start_crash_notice(t.path, rules).has_value());
    latest = latest_crash_notice(t.path, rules);
    REQUIRE(latest.has_value());
    CHECK(latest->name == name_at(2000));
}

TEST_CASE("crash notice: Restart notes the newest report as told, so the app shows no notice for that crash") {
    TempDir t;
    write_report(t.path, name_at(1000), kText);
    mark_crash_reports_seen(t.path);
    CHECK_FALSE(crash_notice_pending(t.path));
    CHECK_FALSE(start_crash_notice(t.path, rules_for(t.path)).has_value());
    // A later crash is new again, and noting never goes back to an older name.
    write_report(t.path, name_at(2000), kText);
    CHECK(crash_notice_pending(t.path));
    mark_crash_reports_seen(t.path);
    write_report(t.path, name_at(1500), kText);
    mark_crash_reports_seen(t.path);
    CHECK_FALSE(crash_notice_pending(t.path));
    mark_crash_reports_seen(t.path / "no-such-folder"); // nothing to note, nothing made
    CHECK_FALSE(fs::exists(t.path / "no-such-folder"));
}

TEST_CASE("crash notice: Restart and send report leaves a marker the next start takes once") {
    TempDir t;
    CHECK_FALSE(take_send_marker(t.path));
    REQUIRE(leave_send_marker(t.path));
    CHECK(fs::exists(t.path / kCrashFolder / kCrashSendMarker));
    CHECK(take_send_marker(t.path));
    CHECK_FALSE(take_send_marker(t.path));
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / kCrashSendMarker));
    // A folder or a link in its place is never taken (and never followed).
    fs::create_directories(t.path / kCrashFolder / kCrashSendMarker);
    CHECK_FALSE(take_send_marker(t.path));
    fs::remove(t.path / kCrashFolder / kCrashSendMarker);
    fs::create_symlink(t.path / "elsewhere", t.path / kCrashFolder / kCrashSendMarker);
    CHECK_FALSE(take_send_marker(t.path));
    CHECK_FALSE(leave_send_marker(t.path)); // O_NOFOLLOW: the link stays what it is
    CHECK(fs::is_symlink(t.path / kCrashFolder / kCrashSendMarker));
}

TEST_CASE("crash notice: a start that did not happen has a report of its own, with the reason and no path") {
    ScrubRules rules;
    rules.home = "/home/pat";
    const CrashNotice n = restore_failure_notice("could not read /home/pat/x.bak", rules);
    CHECK(n.text.find("Reason: could not read ") != std::string::npos);
    CHECK(n.text.find("/home/pat") == std::string::npos);
    CHECK(n.text.find("Version: ") != std::string::npos);
    CHECK(n.url.rfind("https://github.com/techneissa/etude/issues/new?", 0) == 0);
    CHECK(n.qr_url.size() <= kCrashQrUrlMax);
}

TEST_CASE("crash notice: the newest ten reports are kept and the rest go") {
    TempDir t;
    for (int i = 0; i < 14; ++i) {
        write_report(t.path, name_at(1000 + i), kText);
    }
    const auto notice = start_crash_notice(t.path, rules_for(t.path));
    REQUIRE(notice.has_value());
    CHECK(notice->name == name_at(1013)); // the newest is the one told
    int left = 0;
    for (const auto& entry : fs::directory_iterator(t.path / kCrashFolder)) {
        left += is_crash_report_name(entry.path().filename().string()) ? 1 : 0;
    }
    CHECK(left == 10);
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / name_at(1000)));
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / name_at(1003)));
    CHECK(fs::exists(t.path / kCrashFolder / name_at(1004)));
    CHECK(fs::exists(t.path / kCrashFolder / name_at(1013)));
}

TEST_CASE("crash notice: the report the player reads, and the file, hold no path outside the app") {
    TempDir t;
    write_report(t.path, name_at(1000), kText);
    const auto notice = start_crash_notice(t.path, rules_for(t.path));
    REQUIRE(notice.has_value());
    CHECK(notice->text.find("/home/pat") == std::string::npos);
    CHECK(notice->text.find("<path>") != std::string::npos);
    CHECK(notice->text.find("/opt/app/bin/etude") != std::string::npos); // the app's own folder stays
    // The compiler's name in the trace is made readable.
    CHECK(notice->text.find("etude::app::Scene::draw()") != std::string::npos);
    CHECK(notice->text.find("_ZN5etude") == std::string::npos);
    // The file is rewritten clean: a copy taken off the device leaks nothing either.
    CHECK(read_file(t.path / kCrashFolder / name_at(1000)) == notice->text);
}

TEST_CASE("crash notice: the demo reports have a machine's own frames, a summary and addresses of the bounds") {
    const CrashNotice mac = demo_crash_notice(true);
    CHECK(mac.text.find("OS: macOS") != std::string::npos);
    CHECK(mac.text.find("_sigtramp") != std::string::npos);
    CHECK(mac.text.find("linux") == std::string::npos);
    const CrashNotice pi = demo_crash_notice(false);
    CHECK(pi.text.find("OS: Linux") != std::string::npos);
    CHECK(pi.text.find("libsystem") == std::string::npos);
    CHECK(pi.text.find("dylib") == std::string::npos);
    for (const CrashNotice* demo : {&mac, &pi}) {
        // The summary starts at the program's own frame, not the handler's.
        REQUIRE(demo->summary.frames.size() >= 3);
        CHECK(demo->summary.frames[0].find("Scene::draw") != std::string::npos);
        CHECK(demo->url.size() <= kCrashUrlMax);
        CHECK(demo->qr_url.size() <= kCrashQrUrlMax);
        CHECK(demo->qr_url.find("Scene::draw") != std::string::npos);
        CHECK(demo->qr_url.find("crash_signal") == std::string::npos);
    }
}

TEST_CASE("crash notice: the profiles' names are scrubbed once they are known, not before") {
    TempDir t;
    write_report(t.path, name_at(1000), kText);
    ScrubRules rules = rules_for(t.path);
    CrashNoticeStart start;
    start.defer(t.path, false);
    CHECK(start.waiting());
    // The history has not loaded: no notice, and nothing marked told.
    CHECK_FALSE(start.poll(false, rules).has_value());
    CHECK(start.waiting());
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / kCrashSeenFile));
    rules.names = {"Mia"};
    const auto notice = start.poll(true, rules);
    REQUIRE(notice.has_value());
    CHECK(notice->text.find("Mia") == std::string::npos);
    CHECK(notice->text.find("switched to <profile>") != std::string::npos);
    CHECK(read_file(t.path / kCrashFolder / name_at(1000)).find("Mia") == std::string::npos);
    CHECK_FALSE(start.waiting());
    CHECK_FALSE(start.poll(true, rules).has_value()); // asked once
    // A start with no report to tell makes no notice, and is done too.
    TempDir empty;
    CrashNoticeStart none;
    none.defer(empty.path, false);
    CHECK_FALSE(none.poll(true, rules).has_value());
    CHECK_FALSE(none.waiting());
}

TEST_CASE("crash notice: a name that is not a report's, and a link or a folder named like one, are skipped") {
    TempDir t;
    const fs::path crashes = t.path / kCrashFolder;
    fs::create_directories(crashes);
    const fs::path secret = t.path / "secret.txt";
    std::ofstream(secret) << "Version: 9\nthe player's private text";
    fs::create_symlink(secret, crashes / name_at(3000));          // a link named like a report
    fs::create_directory(crashes / name_at(3001));                 // a folder named like one
    std::ofstream(crashes / "../20261020-153012.txt") << kText;    // outside: never listed
    std::ofstream(crashes / "20261020-153012.txt.bak") << kText;   // not a report's name
    const ScrubRules rules = rules_for(t.path);
    CHECK_FALSE(crash_notice_pending(t.path));
    CHECK_FALSE(start_crash_notice(t.path, rules).has_value());
    CHECK(read_file(secret) == "Version: 9\nthe player's private text"); // never read into a notice, never rewritten
    // A real report beside them is the one told.
    write_report(t.path, name_at(1000), kText);
    CHECK(crash_notice_pending(t.path));
    const auto notice = start_crash_notice(t.path, rules);
    REQUIRE(notice.has_value());
    CHECK(notice->name == name_at(1000));
    // A link as the `seen` note is not followed either: it is replaced by a file of its own.
    fs::remove(crashes / kCrashSeenFile);
    fs::create_symlink(secret, crashes / kCrashSeenFile);
    write_report(t.path, name_at(2000), kText);
    CHECK(start_crash_notice(t.path, rules).has_value());
    CHECK(read_file(secret) == "Version: 9\nthe player's private text");
    CHECK_FALSE(fs::is_symlink(crashes / kCrashSeenFile));
}

TEST_CASE("crash notice: the install folder from SDL's base path, with its closing slash") {
    CHECK(install_dir_of("/opt/etude/versions/1.2.0/bin/") == "/opt/etude/versions/1.2.0");
    CHECK(install_dir_of("/opt/etude/versions/1.2.0/bin") == "/opt/etude/versions/1.2.0");
    CHECK(install_dir_of("/opt/etude/versions/1.2.0/bin//") == "/opt/etude/versions/1.2.0");
    CHECK(install_dir_of("/Applications/etude.app/Contents/Resources/") == "/Applications/etude.app");
    CHECK(install_dir_of("/builds/x/app/") == "/builds/x/app"); // a build run where it was made
    CHECK(install_dir_of("/") == "/");
}

TEST_CASE("crash notice: the trace's frames of the program run as <prefix>/current/bin/etude keep their names") {
    // The loader names the main program by argv[0], and the shipped units start `<prefix>/current/bin/etude`,
    // while SDL's base path (the install folder) is the version folder `current` points to.
    const ScrubRules rules = scrub_rules_for("/home/etude/.local/share/io.github.techneissa.etude", "/opt/etude/versions/0.1.0");
    const std::string frame = "/opt/etude/current/bin/etude(_ZN5etude3app5Scene4drawEv+0x14) [0xaaaa55ab]";
    CHECK(etude::scrub_text(frame, rules) == frame);
    const std::string version_frame = "/opt/etude/versions/0.1.0/bin/etude(main+0x544) [0xaaaa8484]";
    CHECK(etude::scrub_text(version_frame, rules) == version_frame);
    // The system's own libraries and a folder beside the prefix are still paths to hide.
    CHECK(etude::scrub_text("/lib/aarch64-linux-gnu/libc.so.6(+0x2225c) [0xffff225c]", rules) == "<path>) [0xffff225c]");
    CHECK(etude::scrub_text("/opt/etude-old/bin/etude(main+0x1) [0x1]", rules) == "<path>) [0x1]");
    // No `versions` folder (a build run where it was made, a Mac's bundle): nothing is added.
    const ScrubRules plain = scrub_rules_for("/data", "/builds/x/app");
    CHECK(etude::scrub_text("/builds/x/current/bin/etude(main+0x1) [0x1]", plain) == "<path>) [0x1]");
}
