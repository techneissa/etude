// SPDX-License-Identifier: GPL-3.0-or-later
// Crash reports, the pure side: the file name, what a log line keeps of a path, what a report says
// in short, the issue address and its length bound, which reports are kept, and the notice's state.
#include "etude/crash_report.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace etude;

namespace {

ScrubRules rules() {
    ScrubRules r;
    r.home = "/home/pat";
    r.app_dirs = {"/home/pat/.local/share/etude", "/opt/etude/versions/1.2.0"};
    r.names = {"Mia", "Pat"};
    return r;
}

std::string name_of(std::int64_t t) {
    char out[kCrashNameChars];
    crash_report_name(t, out);
    return out;
}

const char* kReport =
    "etude crash report\n"
    "Version: 1.2.0\n"
    "OS: Linux 6.12.25 aarch64\n"
    "Screen: playing a song\n"
    "Song: Moonlight Sonata\n"
    "Signal: SIGSEGV (11)\n"
    "Time (UTC): 20261020-153012\n"
    "\n"
    "Stack trace:\n"
    "0   etude   0x0000000104a3c1b8 etude_crash_write + 40\n"
    "1   libsystem_platform.dylib   0x000000018c1a2a24 _sigtramp + 56\n"
    "2   etude   0x0000000104b01234 etude::app::Scene::draw() + 812\n"
    "3   etude   0x0000000104b05678 etude::app::WindowState::run() + 1204\n"
    "\n"
    "Log (the last lines, the newest last):\n"
    "etude: a song loaded\n";

} // namespace

TEST_CASE("crash report: the file name is the UTC time and sorts in the order of the crashes") {
    CHECK(name_of(0) == "19700101-000000.txt");
    CHECK(name_of(1792487270) == "20261020-090750.txt");
    CHECK(name_of(951782399) == "20000228-235959.txt"); // the second before a leap day
    CHECK(name_of(951782400) == "20000229-000000.txt");
    CHECK(name_of(4102444799) == "20991231-235959.txt");
    CHECK(name_of(1792487270) < name_of(1792487271));
    CHECK(name_of(951782400) < name_of(1792487270));
    CHECK(is_crash_report_name(name_of(1792487270)));
    // The whole name is checked: digits, one dash, ".txt", and nothing else.
    CHECK(is_crash_report_name("20261020-153012.txt"));
    CHECK_FALSE(is_crash_report_name("../20261020-153012.txt"));
    CHECK_FALSE(is_crash_report_name("a/20261020-153012.txt"));
    CHECK_FALSE(is_crash_report_name("20261020-153012.txt/"));
    CHECK_FALSE(is_crash_report_name("20261020-153012.txtx"));
    CHECK_FALSE(is_crash_report_name("20261020-153012.txt.txt"));
    CHECK_FALSE(is_crash_report_name("20261020-153012-1.txt"));
    CHECK_FALSE(is_crash_report_name("20261020_153012.txt"));
    CHECK_FALSE(is_crash_report_name(std::string("20261020-153012.txt\0x", 21)));
    CHECK_FALSE(is_crash_report_name("seen"));
    CHECK_FALSE(is_crash_report_name("20261020-090750.txt.new"));
    CHECK_FALSE(is_crash_report_name("2026102x-090750.txt"));
}

TEST_CASE("crash report scrubbing: the home folder is ~ and any other path is <path>") {
    const ScrubRules r = rules();
    // Inside the app's folders a path stays, written from the home folder.
    CHECK(scrub_line("saved /home/pat/.local/share/etude/Songs/a.mid ok", r) ==
          "saved ~/.local/share/etude/Songs/a.mid ok");
    CHECK(scrub_line("/opt/etude/versions/1.2.0/share/etude/x.png", r) ==
          "/opt/etude/versions/1.2.0/share/etude/x.png");
    // Anywhere else it goes, the home folder's other files too.
    CHECK(scrub_line("open /home/pat/Music/secret song.mid failed", r) == "open <path>");
    CHECK(scrub_line("open /home/pat/Music/x.mid failed", r) == "open <path> failed");
    CHECK(scrub_line("home is /home/pat", r) == "home is <path>");
    CHECK(scrub_line("read /etc/os-release.", r) == "read <path>.");
    CHECK(scrub_line("path=/proc/self/exe", r) == "path=<path>");
    CHECK(scrub_line("at (/tmp/x)", r) == "at (<path>)");
    CHECK(scrub_line("\"/usr/lib/libfoo.so\" and '/var/x'", r) == "\"<path>\" and '<path>'");
    // A folder that only starts like an app folder is not one.
    CHECK(scrub_line("/opt/etude/versions/1.2.0-old/x", r) == "<path>");
    CHECK(scrub_line("/opt/etude/versions/1.2.01", r) == "<path>");
    // Not paths: words with a slash, fractions, addresses.
    CHECK(scrub_line("and/or 1/2 read/write", r) == "and/or 1/2 read/write");
    CHECK(scrub_line("see https://github.com/techneissa/etude/issues", r) ==
          "see https://github.com/techneissa/etude/issues");
    CHECK(scrub_line("a / b", r) == "a / b");
    CHECK(scrub_line("", r).empty());
}

TEST_CASE("crash report scrubbing: a profile's name is <profile>, as a whole word") {
    const ScrubRules r = rules();
    CHECK(scrub_line("profile Mia switched", r) == "profile <profile> switched");
    CHECK(scrub_line("Mia's run, Pat", r) == "<profile>'s run, <profile>");
    CHECK(scrub_line("Miami and Patrick", r) == "Miami and Patrick");
    ScrubRules one;
    one.names = {"A", ""};
    CHECK(scrub_line("A song of A", one) == "A song of A"); // a one-letter name would ruin the text
}

TEST_CASE("crash report scrubbing: a whole report keeps its lines") {
    const ScrubRules r = rules();
    const std::string text = "a /home/pat/x\n\nb /etc/y\nend";
    CHECK(scrub_text(text, r) == "a <path>\n\nb <path>\nend");
    CHECK(scrub_text("one\n", r) == "one\n");
    CHECK(scrub_text("", r).empty());
}

TEST_CASE("crash report: the summary reads the facts and the top frames, the handler's own left out") {
    const CrashSummary s = summarize_crash(kReport);
    CHECK(s.version == "1.2.0");
    CHECK(s.os == "Linux 6.12.25 aarch64");
    CHECK(s.signal == "SIGSEGV (11)");
    CHECK(s.screen == "playing a song");
    REQUIRE(s.frames.size() == 2);
    CHECK(s.frames[0].find("Scene::draw") != std::string::npos);
    CHECK(s.frames[1].find("WindowState::run") != std::string::npos);
    CHECK(summarize_crash("").frames.empty());
}

TEST_CASE("crash report: the issue address stays under its bound, whatever the report holds") {
    CrashSummary s = summarize_crash(kReport);
    const std::string mac = crash_issue_url(s, kCrashUrlMax);
    CHECK(mac.rfind("https://github.com/techneissa/etude/issues/new?title=", 0) == 0);
    CHECK(mac.find("&body=") != std::string::npos);
    CHECK(mac.find("Scene::draw()") != std::string::npos);
    CHECK(mac.find(' ') == std::string::npos);
    CHECK(mac.find('\n') == std::string::npos);
    CHECK(mac.size() <= kCrashUrlMax);

    // The QR code's address is the short one: the facts and the top frame or two.
    const std::string qr = crash_issue_url(s, kCrashQrUrlMax);
    CHECK(qr.size() <= kCrashQrUrlMax);
    CHECK(qr.find("SIGSEGV") != std::string::npos);
    CHECK(qr.find("1.2.0") != std::string::npos);
    CHECK(qr.find("Scene::draw()") != std::string::npos); // a frame fits in the short one too
    CHECK(qr.size() < mac.size());

    // A report with a hundred long frames and long facts is cut, never past the bound.
    for (int i = 0; i < 100; ++i) {
        s.frames.push_back(std::string(400, 'x') + " frame " + std::to_string(i));
    }
    s.os = std::string(300, 'o');
    s.screen = std::string(300, 's');
    for (const std::size_t bound : {kCrashUrlMax, std::size_t{1000}, kCrashQrUrlMax}) {
        const std::string url = crash_issue_url(s, bound);
        CHECK(url.size() <= bound);
        CHECK(url.rfind("https://github.com/techneissa/etude/issues/new?title=", 0) == 0);
    }
}

TEST_CASE("crash report: percent encoding keeps the unreserved letters and encodes the rest") {
    CHECK(percent_encode("aZ09-._~") == "aZ09-._~");
    CHECK(percent_encode("a b\nc&d=e:(x)") == "a%20b%0Ac%26d%3De:(x)");
    CHECK(percent_encode("\xC3\xA9") == "%C3%A9");
}

TEST_CASE("crash report: the newest ten are kept") {
    std::vector<std::string> names;
    for (int i = 0; i < 13; ++i) {
        names.push_back(name_of(1792487270 + i));
    }
    names.push_back("seen");
    names.push_back("20261020-090750.txt.new");
    std::reverse(names.begin(), names.end()); // any order
    const std::vector<std::string> gone = reports_to_prune(names, kCrashReportsKept);
    REQUIRE(gone.size() == 3);
    CHECK(gone[0] == name_of(1792487270));
    CHECK(gone[1] == name_of(1792487271));
    CHECK(gone[2] == name_of(1792487272));
    CHECK(reports_to_prune({name_of(1), name_of(2)}, kCrashReportsKept).empty());
    CHECK(reports_to_prune({}, kCrashReportsKept).empty());
}

TEST_CASE("crash notice state: the newest report not told yet is shown once") {
    const std::vector<std::string> two = {name_of(100), name_of(200), "seen"};
    // Nothing told yet: the newest.
    CrashNoticePlan plan = plan_crash_notice(two, "");
    CHECK(plan.show == name_of(200));
    CHECK(plan.seen == name_of(200));
    // The next start, with that one told: nothing.
    plan = plan_crash_notice(two, plan.seen);
    CHECK(plan.show.empty());
    CHECK(plan.seen == name_of(200));
    // A new crash is told again, once.
    std::vector<std::string> three = two;
    three.push_back(name_of(300));
    plan = plan_crash_notice(three, name_of(200));
    CHECK(plan.show == name_of(300));
    CHECK(plan.seen == name_of(300));
    // No reports, or only a stray file.
    CHECK(plan_crash_notice({}, "").show.empty());
    CHECK(plan_crash_notice({"seen", "x.txt"}, "").show.empty());
}

TEST_CASE("crash notice state: the newest report is named whether it was told or not") {
    CHECK(newest_crash_report({name_of(100), name_of(300), name_of(200)}) == name_of(300));
    CHECK(newest_crash_report({"seen", "x.txt"}).empty());
    CHECK(newest_crash_report({}).empty());
    // Told already: the notice has nothing to say, but the crash screen's report is still that one.
    const std::vector<std::string> names = {name_of(100), name_of(200)};
    CHECK(plan_crash_notice(names, name_of(200)).show.empty());
    CHECK(newest_crash_report(names) == name_of(200));
}

TEST_CASE("crash report scrubbing: a path with a space in it leaves nothing of its tail") {
    ScrubRules r;
    r.home = "/People/Jane Doe";
    r.app_dirs = {"/People/Jane Doe/Library/Application Support/etude", "/Applications/etude.app"};
    // The known folders are matched whole, blanks and all.
    CHECK(scrub_line("saw /People/Jane Doe/x.mid failed", r) == "saw <path> failed");
    CHECK(scrub_line("open /People/Jane Doe/Music/My Song.mid failed", r) == "open <path>");
    CHECK(scrub_line("home /People/Jane Doe", r) == "home <path>");
    CHECK(scrub_line("a /People/Jane Doe/Library/Application Support/etude/Songs/a.mid ok", r) ==
          "a ~/Library/Application Support/etude/Songs/a.mid ok");
    CHECK(scrub_line("at /Applications/etude.app/Contents/x", r) == "at /Applications/etude.app/Contents/x");
    // A path outside them that goes on past a blank: the rest of the line goes.
    CHECK(scrub_line("open /Volumes/My Stick/x.mid failed", r) == "open <path>");
    CHECK(scrub_line("open /Volumes/Doe/x.mid then /tmp/y", r) == "open <path> then <path>");
    // After a quote it ends at the closing quote, however many blanks it holds.
    CHECK(scrub_line("open \"/Volumes/My Music Stick/x.mid\" failed", r) == "open \"<path>\" failed");
    CHECK(scrub_line("open '/Volumes/My Music Stick/x' failed", r) == "open '<path>' failed");
    CHECK(scrub_line("open \"/Volumes/My Music Stick/x", r) == "open \"<path>");
    // No space, no loss of the line.
    CHECK(scrub_line("open /Volumes/Stick/x.mid failed", r) == "open <path> failed");
    CHECK(scrub_line("open /Volumes/Stick/x.mid. Done.", r) == "open <path>. Done.");
}

TEST_CASE("crash report: the summary leaves out the handler's own frames, the full report keeps them") {
    const std::string linux_report =
        "etude crash report\nVersion: 1.2.0\nOS: Linux\nSignal: SIGSEGV (11)\n\nStack trace:\n"
        "/opt/x/bin/etude(_ZN5etude3app18write_crash_reportEPKciS2_+0x1c0) [0x5555a1b3c1b8]\n"
        "/opt/x/bin/etude(etude_crash_signal+0x28) [0x5555a1b3c4f0]\n"
        "/lib/x86_64-linux-gnu/libc.so.6(+0x3c050) [0x7f9c3dfa7050]\n"
        "/opt/x/bin/etude(etude::app::Scene::draw()+0x32c) [0x5555a1c01234]\n"
        "/opt/x/bin/etude(main+0x90) [0x5555a1c09abc]\n\nLog\n";
    CrashSummary s = summarize_crash(linux_report);
    REQUIRE(s.frames.size() == 2);
    CHECK(s.frames[0].find("Scene::draw") != std::string::npos);
    CHECK(s.frames[1].find("main") != std::string::npos);

    // The signal's trampoline with a name of its own, and a raised signal's libc frames.
    s = summarize_crash(
        "Signal: SIGABRT (6)\n\nStack trace:\n0 etude 0x1 etude::app::write_crash_report() + 1\n"
        "1 etude 0x2 etude_crash_signal + 40\n2 libsystem 0x3 __restore_rt + 8\n"
        "3 libc.so.6 0x4 gsignal + 1\n4 libc.so.6 0x5 abort + 2\n"
        "5 etude 0x6 etude::app::Library::scan() + 90\n\n");
    REQUIRE(s.frames.size() == 1);
    CHECK(s.frames[0].find("Library::scan") != std::string::npos);

    // An uncaught exception: the terminate handler and the runtime's frames go.
    s = summarize_crash(
        "Signal: uncaught C++ exception (std::runtime_error)\n\nStack trace:\n"
        "0 etude 0x1 etude::app::write_crash_report() + 1\n"
        "1 etude 0x2 etude::app::(anonymous namespace)::on_terminate() + 4\n"
        "2 libc++abi 0x3 std::__terminate(void (*)()) + 8\n"
        "3 libc++abi 0x4 __cxa_throw + 100\n"
        "4 etude 0x5 etude::app::Song::load() + 7\n\n");
    REQUIRE(s.frames.size() == 1);
    CHECK(s.frames[0].find("Song::load") != std::string::npos);

    // Neither address holds a handler frame.
    CrashSummary with_version = summarize_crash(linux_report);
    with_version.version = "1.2.0";
    for (const std::size_t bound : {kCrashUrlMax, kCrashQrUrlMax}) {
        const std::string url = crash_issue_url(with_version, bound);
        CHECK(url.find("crash_signal") == std::string::npos);
        CHECK(url.find("write_crash") == std::string::npos);
        CHECK(url.find("Scene::draw") != std::string::npos);
    }
}

TEST_CASE("crash report: the addresses hold their bounds for the longest inputs") {
    // 159 characters of an exception's name, three bytes to a character, and every part at its most.
    std::string accents;
    for (int i = 0; i < 200; ++i) {
        accents += "\xE2\x82\xAC"; // a euro sign
    }
    for (const std::string& text : {std::string(400, 'x'), accents}) {
        CrashSummary s;
        s.version = text.substr(0, 48);
        s.os = text.substr(0, 96);
        s.signal = "uncaught C++ exception (" + text.substr(0, 159) + ")";
        s.screen = text.substr(0, 96);
        for (int i = 0; i < 100; ++i) {
            s.frames.push_back(text);
        }
        for (const std::size_t bound : {kCrashUrlMax, kCrashQrUrlMax, std::size_t{1000}}) {
            CAPTURE(bound);
            const std::string url = crash_issue_url(s, bound);
            CHECK(url.size() <= bound);
            CHECK(url.rfind("https://github.com/techneissa/etude/issues/new?title=", 0) == 0);
            CHECK(url.find(' ') == std::string::npos);
        }
        // The same with no frames, and with no signal text at all.
        s.frames.clear();
        CHECK(crash_issue_url(s, kCrashQrUrlMax).size() <= kCrashQrUrlMax);
        s.signal.clear();
        CHECK(crash_issue_url(s, kCrashQrUrlMax).size() <= kCrashQrUrlMax);
        CHECK(crash_issue_url(s, kCrashUrlMax).size() <= kCrashUrlMax);
    }
    CHECK(crash_issue_url(CrashSummary{}, kCrashQrUrlMax).size() <= kCrashQrUrlMax);
}
