// SPDX-License-Identifier: GPL-3.0-or-later
// The file-system steps of a Mac update, against temporary folders with fake bundles: the facts about
// the running bundle, the hash of the download, unpacking and checking the new bundle (the programs
// are stood in for, and on a Mac the real ditto and codesign run too), and the relaunch script's
// swap and rollback, run for real with a fake `open`.
#include "../app/mac_relaunch_script.hpp"
#include "../app/mac_update.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

// A bundle that is a folder with a program and a VERSION file, enough for the script and the checks.
fs::path fake_bundle(const fs::path& where, const std::string& version) {
    write_file(where / "Contents/MacOS/etude", "#!/bin/sh\n");
    ::chmod((where / "Contents/MacOS/etude").c_str(), 0755);
    write_file(where / "VERSION", version);
    return where;
}

std::string version_of(const fs::path& bundle) { return read_file(bundle / "VERSION"); }

// Runs `command` in a shell; its exit status.
int sh(const std::string& command) {
    const int raw = std::system(command.c_str());
    return raw == -1 ? -1 : WEXITSTATUS(raw);
}

std::string status_value(const fs::path& status, const std::string& key) {
    const std::string text = "\n" + read_file(status);
    const std::size_t at = text.find("\n" + key + "=");
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t from = at + key.size() + 2;
    return text.substr(from, text.find('\n', from) - from);
}

// Whether this system exchanges two folders in one call (a Linux file system may not).
bool swap_supported() {
    static const bool supported = [] {
        TempDir dir;
        fs::create_directories(dir.path / "a");
        fs::create_directories(dir.path / "b");
        return swap_directories(dir.path / "a", dir.path / "b");
    }();
    if (!supported) {
        MESSAGE("this file system cannot exchange two folders: the swap tests are not run");
    }
    return supported;
}

} // namespace

TEST_CASE("mac update: two folders are exchanged in one call, and a missing one is an error that changes nothing") {
    if (!swap_supported()) {
        return;
    }
    TempDir dir;
    write_file(dir.path / "a/name", "a");
    write_file(dir.path / "b/name", "b");
    REQUIRE(swap_directories(dir.path / "a", dir.path / "b"));
    CHECK(read_file(dir.path / "a/name") == "b");
    CHECK(read_file(dir.path / "b/name") == "a");
    int error = 0;
    CHECK_FALSE(swap_directories(dir.path / "a", dir.path / "missing", &error));
    CHECK(error == ENOENT);
    CHECK(read_file(dir.path / "a/name") == "b");
}

TEST_CASE("mac update: the app's --swap-bundle mode does the exchange and nothing else") {
    if (!swap_supported()) {
        return;
    }
    TempDir dir;
    write_file(dir.path / "a/name", "a");
    write_file(dir.path / "b/name", "b");
    const std::string app = std::string("'") + ETUDE_APP_PATH + "' --swap-bundle '";
    CHECK(sh(app + (dir.path / "a").string() + "' '" + (dir.path / "b").string() + "' >/dev/null 2>&1") == 0);
    CHECK(read_file(dir.path / "a/name") == "b");
    CHECK(read_file(dir.path / "b/name") == "a");
    CHECK(sh(app + (dir.path / "a").string() + "' '" + (dir.path / "gone").string() + "' >/dev/null 2>&1") == 1);
    CHECK(read_file(dir.path / "a/name") == "b");
}

TEST_CASE("mac update: the running bundle, its folder and the volume it is on") {
    TempDir dir;
    const fs::path exe = dir.path / "Apps/etude.app/Contents/MacOS/etude";
    write_file(exe, "x");
    const MacInstallFacts facts = gather_mac_install_facts(exe);
    CHECK(facts.bundle == (fs::canonical(dir.path) / "Apps/etude.app").string());
    CHECK(facts.parent_writable);
    CHECK_FALSE(facts.volume_read_only);
    CHECK(check_mac_install(facts).can_install());
}

TEST_CASE("mac update: a build folder is no bundle, and a program that is gone gives no facts") {
    TempDir dir;
    write_file(dir.path / "build/app/etude", "x");
    CHECK(gather_mac_install_facts(dir.path / "build/app/etude").bundle.empty());
    CHECK(check_mac_install(gather_mac_install_facts(dir.path / "build/app/etude")).block ==
          MacInstallBlock::NotABundle);
    CHECK(gather_mac_install_facts(dir.path / "nothing").bundle.empty());
}

TEST_CASE("mac update: a folder the user cannot write blocks the install") {
    if (::geteuid() == 0) {
        return; // root writes anywhere
    }
    TempDir dir;
    const fs::path exe = dir.path / "Apps/etude.app/Contents/MacOS/etude";
    write_file(exe, "x");
    ::chmod((dir.path / "Apps").c_str(), 0555);
    const MacInstallFacts facts = gather_mac_install_facts(exe);
    ::chmod((dir.path / "Apps").c_str(), 0755); // so the folder can be removed
    CHECK_FALSE(facts.parent_writable);
    CHECK(check_mac_install(facts).block == MacInstallBlock::NotWritable);
}

TEST_CASE("mac update: the hash of a file read in pieces is the hash of its bytes") {
    TempDir dir;
    for (const std::size_t size : {0u, 1u, 256u * 1024u, 256u * 1024u + 1u, 600u * 1024u + 7u}) {
        std::string bytes(size, '\0');
        for (std::size_t i = 0; i < size; ++i) {
            bytes[i] = static_cast<char>((i * 31 + 7) & 0xff);
        }
        write_file(dir.path / "zip", bytes);
        Digest512 pieces{};
        REQUIRE(digest_of_file(dir.path / "zip", pieces));
        FileDigest whole;
        whole.update({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
        CHECK(pieces == whole.finish());
    }
    Digest512 none{};
    CHECK_FALSE(digest_of_file(dir.path / "missing", none));
}

TEST_CASE("mac update: unpacking asks ditto to extract the zip into a fresh folder beside the app") {
    TempDir dir;
    std::vector<std::string> asked;
    const ProgramRunner run = [&](const std::vector<std::string>& argv, int) {
        asked = argv;
        fake_bundle(fs::path(argv[4]) / "etude.app", "1.2.3");
        ProgramResult done;
        done.started = true;
        done.exit_code = 0;
        return done;
    };
    write_file(dir.path / "Apps/.etude-update-1.2.3/old-leftover", "x");
    const StepResult unpacked = unpack_release_zip(run, dir.path / "release.zip",
                                                   dir.path / "Apps/.etude-update-1.2.3");
    REQUIRE(unpacked.ok);
    REQUIRE(asked.size() == 5);
    CHECK(asked[0] == "/usr/bin/ditto");
    CHECK(asked[1] == "-x");
    CHECK(asked[2] == "-k");
    CHECK_FALSE(fs::exists(dir.path / "Apps/.etude-update-1.2.3/old-leftover"));
    CHECK(find_unpacked_bundle(dir.path / "Apps/.etude-update-1.2.3") ==
          dir.path / "Apps/.etude-update-1.2.3/etude.app");
}

TEST_CASE("mac update: a failed or hung unpack leaves nothing behind") {
    TempDir dir;
    const ProgramRunner failing = [](const std::vector<std::string>&, int) {
        ProgramResult done;
        done.started = true;
        done.exit_code = 1;
        return done;
    };
    CHECK_FALSE(unpack_release_zip(failing, dir.path / "z", dir.path / "stage").ok);
    CHECK_FALSE(fs::exists(dir.path / "stage"));
    const ProgramRunner hung = [](const std::vector<std::string>&, int) {
        ProgramResult done;
        done.started = true;
        done.timed_out = true;
        return done;
    };
    CHECK_FALSE(unpack_release_zip(hung, dir.path / "z", dir.path / "stage").ok);
    const ProgramRunner missing = [](const std::vector<std::string>&, int) { return ProgramResult{}; };
    CHECK_FALSE(unpack_release_zip(missing, dir.path / "z", dir.path / "stage").ok);
}

TEST_CASE("mac update: only a folder with exactly one app inside is a release") {
    TempDir dir;
    CHECK(find_unpacked_bundle(dir.path / "none").empty());
    fs::create_directories(dir.path / "empty");
    CHECK(find_unpacked_bundle(dir.path / "empty").empty());
    fake_bundle(dir.path / "one/etude.app", "1");
    write_file(dir.path / "one/__MACOSX-note", "x");
    CHECK(find_unpacked_bundle(dir.path / "one") == dir.path / "one/etude.app");
    fake_bundle(dir.path / "two/a.app", "1");
    fake_bundle(dir.path / "two/b.app", "1");
    CHECK(find_unpacked_bundle(dir.path / "two").empty());
}

TEST_CASE("mac update: the new bundle must verify as signed and say the released version") {
    TempDir dir;
    const fs::path bundle = fake_bundle(dir.path / "etude.app", "1.2.3");
    std::string version = "1.2.3";
    int code = 0;
    std::vector<std::vector<std::string>> asked;
    const ProgramRunner run = [&](const std::vector<std::string>& argv, int) {
        asked.push_back(argv);
        ProgramResult done;
        done.started = true;
        done.exit_code = 0;
        if (argv[0] == "/usr/bin/codesign") {
            done.exit_code = code;
            done.output = code != 0 ? "etude.app: a sealed resource is missing or invalid\n" : "";
        } else {
            done.output = version + "\n";
        }
        return done;
    };
    CHECK(check_new_bundle(run, bundle, "1.2.3").ok);
    REQUIRE(asked.size() == 2);
    CHECK(asked[0] == std::vector<std::string>{"/usr/bin/codesign", "--verify", "--deep", "--strict",
                                               bundle.string()});
    CHECK(asked[1][0] == "/usr/bin/plutil");
    CHECK(asked[1][2] == "CFBundleShortVersionString");
    CHECK(asked[1].back() == (bundle / "Contents/Info.plist").string());

    version = "1.2.2"; // a signed bundle of another version is not the release
    const StepResult wrong = check_new_bundle(run, bundle, "1.2.3");
    CHECK_FALSE(wrong.ok);
    CHECK(wrong.why.find("1.2.2") != std::string::npos);

    version = "1.2.3";
    code = 1;
    const StepResult unsigned_bundle = check_new_bundle(run, bundle, "1.2.3");
    CHECK_FALSE(unsigned_bundle.ok);
    CHECK(unsigned_bundle.why.find("sealed resource") != std::string::npos);

    fs::remove(bundle / "Contents/MacOS/etude");
    CHECK_FALSE(check_new_bundle(run, bundle, "1.2.3").ok);
}

#if defined(__APPLE__)
TEST_CASE("mac update: the real ditto, codesign and plutil agree with the steps") {
    TempDir dir;
    // A bundle with a real program (a copy of /usr/bin/true, signed ad hoc) and an Info.plist.
    const fs::path bundle = dir.path / "src/etude.app";
    fs::create_directories(bundle / "Contents/MacOS");
    fs::copy_file("/usr/bin/true", bundle / "Contents/MacOS/etude");
    write_file(bundle / "Contents/Info.plist",
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\"><dict>"
               "<key>CFBundleIdentifier</key><string>app.etude.test</string>"
               "<key>CFBundleExecutable</key><string>etude</string>"
               "<key>CFBundlePackageType</key><string>APPL</string>"
               "<key>CFBundleShortVersionString</key><string>1.2.3</string>"
               "</dict></plist>\n");
    REQUIRE(run_program({"/usr/bin/codesign", "-s", "-", "--force", bundle.string()}, 60).exit_code == 0);
    REQUIRE(run_program({"/usr/bin/ditto", "-c", "-k", "--keepParent", bundle.string(),
                         (dir.path / "etude.zip").string()},
                        60)
                .exit_code == 0);

    const ProgramRunner real = run_program;
    const fs::path staging = dir.path / "Apps/.etude-update-1.2.3";
    REQUIRE(unpack_release_zip(real, dir.path / "etude.zip", staging).ok);
    const fs::path unpacked = find_unpacked_bundle(staging);
    REQUIRE_FALSE(unpacked.empty());
    CHECK(check_new_bundle(real, unpacked, "1.2.3").ok);
    CHECK_FALSE(check_new_bundle(real, unpacked, "1.2.4").ok);

    // A file changed after signing breaks the seal.
    write_file(unpacked / "Contents/Resources/added", "x");
    const StepResult tampered = check_new_bundle(real, unpacked, "1.2.3");
    CHECK_FALSE(tampered.ok);
}
#endif

TEST_CASE("mac update: run_program reads the output and ends a program that runs too long") {
    const ProgramResult echoed = run_program({"/bin/echo", "hello"}, 10);
    CHECK(echoed.started);
    CHECK(echoed.exit_code == 0);
    CHECK(echoed.output == "hello\n");
    CHECK(run_program({"/bin/sh", "-c", "echo oops >&2; exit 3"}, 10).exit_code == 3);
    CHECK_FALSE(run_program({"/nonexistent/program"}, 10).started);
    const ProgramResult slow = run_program({"/bin/sleep", "30"}, 1);
    CHECK(slow.timed_out);
    CHECK(slow.exit_code == -1);
}

TEST_CASE("mac update: spawn_detached starts a program with its output in the log and does not wait") {
    TempDir dir;
    const std::string log = (dir.path / "run.log").string();
    REQUIRE(spawn_detached({"/bin/sh", "-c", "echo started"}, log));
    bool seen = false;
    for (int waited = 0; waited < 5000 && !seen; waited += 10) {
        seen = read_file(log) == "started\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(seen);
    CHECK_FALSE(spawn_detached({"/nonexistent/program"}, log));
}

TEST_CASE("mac update: the relaunch script is saved runnable and started with its nine arguments") {
    TempDir dir;
    RelaunchPlan plan;
    plan.app_pid = 4242;
    plan.bundle = "/Apps/etude.app";
    plan.new_bundle = "/Apps/.etude-update-1.2.3/etude.app";
    plan.previous = "/Apps/.etude-1.1.0.previous";
    plan.update_dir = dir.path / "update";
    plan.version = "1.2.3";
    plan.from = "1.1.0";
    plan.ready_wait_s = 30;
    std::vector<std::string> argv;
    std::string log;
    const StepResult started = start_relaunch(plan, [&](const std::vector<std::string>& a, const std::string& l) {
        argv = a;
        log = l;
        return true;
    });
    REQUIRE(started.ok);
    const fs::path script = dir.path / "update/relaunch.sh";
    CHECK(read_file(script) == std::string(mac_relaunch_script()));
    struct stat info {};
    REQUIRE(::stat(script.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0700);
    REQUIRE(argv.size() == 11);
    CHECK(argv[0] == "/bin/sh");
    CHECK(argv[1] == script.string());
    CHECK(argv[2] == "4242");
    CHECK(argv[3] == "/Apps/etude.app");
    CHECK(argv[4] == "/Apps/.etude-update-1.2.3/etude.app");
    CHECK(argv[5] == "/Apps/.etude-1.1.0.previous");
    CHECK(argv[6] == (dir.path / "update/ready").string());
    CHECK(argv[7] == (dir.path / "update/status").string());
    CHECK(argv[8] == "1.2.3");
    CHECK(argv[9] == "1.1.0");
    CHECK(argv[10] == "30");
    CHECK(log == (dir.path / "update/relaunch.log").string());
    CHECK_FALSE(start_relaunch(plan, [](const std::vector<std::string>&, const std::string&) { return false; }).ok);
}

namespace {

// Two fake bundles, a fake `open`, and the script, in a temporary folder.
struct SwapRig {
    TempDir dir;
    fs::path apps = dir.path / "Apps";
    fs::path bundle = apps / "etude.app";
    fs::path staging = apps / ".etude-update-2.0.0";
    fs::path new_bundle = staging / "etude.app";
    fs::path previous = apps / ".etude-1.0.0.previous";
    fs::path update = dir.path / "update";
    fs::path script = update / "relaunch.sh";
    fs::path open_log = dir.path / "open.log";
    fs::path fake_open = dir.path / "open";
    // The program that exchanges two folders: the built app's --swap-bundle mode.
    std::string swap_env = std::string("ETUDE_SWAP='") + ETUDE_APP_PATH + " --swap-bundle'";

    // `ready_for`: the version whose launch writes the ready file ("" : none does).
    explicit SwapRig(const std::string& ready_for) {
        fake_bundle(bundle, "1.0.0");
        fake_bundle(new_bundle, "2.0.0");
        fs::create_directories(update);
        write_file(script, std::string(mac_relaunch_script()));
        write_file(fake_open,
                   "#!/bin/sh\n"
                   "echo \"$1 $(cat \"$1/VERSION\")\" >> '" + open_log.string() + "'\n"
                   "if [ \"$(cat \"$1/VERSION\")\" = '" + ready_for + "' ]; then\n"
                   "  (sleep 0.3; printf 'version=" + ready_for + "\\n' > '" + (update / "ready").string() +
                   "') >/dev/null 2>&1 &\n"
                   "fi\n");
        ::chmod(fake_open.c_str(), 0755);
    }

    // Runs the script for an app that has already quit (`app_pid` 2^22 + 1 is no process).
    int run(int wait_seconds, const std::string& pid = "4194305", const std::string& env = "") {
        return sh("ETUDE_OPEN='" + fake_open.string() + "' " + swap_env + " " + env + " /bin/sh '" + script.string() + "' " +
                  pid + " '" + bundle.string() + "' '" + new_bundle.string() + "' '" + previous.string() +
                  "' '" + (update / "ready").string() + "' '" + (update / "status").string() +
                  "' 2.0.0 1.0.0 " + std::to_string(wait_seconds) + " >/dev/null 2>&1");
    }

    std::string opened() { return read_file(open_log); }
};

} // namespace

TEST_CASE("mac relaunch: a new version that reaches its first frame replaces the old one, which is kept") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("2.0.0");
    // An older kept version goes once the new one runs; so does the staging folder.
    fake_bundle(rig.apps / ".etude-0.9.0.previous", "0.9.0");
    CHECK(rig.run(10) == 0);
    CHECK(version_of(rig.bundle) == "2.0.0");
    CHECK(version_of(rig.previous) == "1.0.0");
    CHECK_FALSE(fs::exists(rig.apps / ".etude-0.9.0.previous"));
    CHECK_FALSE(fs::exists(rig.staging));
    CHECK(rig.opened() == rig.bundle.string() + " 2.0.0\n");
    CHECK(status_value(rig.update / "status", "state") == "ok");
    CHECK(status_value(rig.update / "status", "version") == "2.0.0");
    CHECK(status_value(rig.update / "status", "from") == "1.0.0");
}

TEST_CASE("mac relaunch: a new version that does not reach its first frame is replaced by the old one") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig(""); // nothing writes ready
    CHECK(rig.run(1) == 1);
    CHECK(version_of(rig.bundle) == "1.0.0");
    CHECK_FALSE(fs::exists(rig.previous));
    CHECK_FALSE(fs::exists(rig.staging));
    CHECK_FALSE(fs::exists(rig.apps / ".etude-2.0.0.failed"));
    // The new one was started, then the old one.
    CHECK(rig.opened() == rig.bundle.string() + " 2.0.0\n" + rig.bundle.string() + " 1.0.0\n");
    CHECK(status_value(rig.update / "status", "state") == "rolled-back");
    CHECK(status_value(rig.update / "status", "reason").find("version 1.0.0 was put back") !=
          std::string::npos);
}

TEST_CASE("mac relaunch: a ready file left by an earlier start does not count") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("");
    write_file(rig.update / "ready", "version=2.0.0\n");
    CHECK(rig.run(1) == 1);
    CHECK(version_of(rig.bundle) == "1.0.0");
}

TEST_CASE("mac relaunch: a ready file for another version does not count either") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("1.9.9");
    CHECK(rig.run(1) == 1);
    CHECK(version_of(rig.bundle) == "1.0.0");
}

TEST_CASE("mac relaunch: if the new bundle cannot be moved in, the old one stays and is started") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("2.0.0");
    fs::remove_all(rig.new_bundle);
    CHECK(rig.run(2) == 1);
    CHECK(version_of(rig.bundle) == "1.0.0");
    CHECK_FALSE(fs::exists(rig.previous));
    CHECK(rig.opened() == rig.bundle.string() + " 1.0.0\n");
    CHECK(status_value(rig.update / "status", "state") == "failed");
}

TEST_CASE("mac relaunch: an app that does not close changes nothing") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("2.0.0");
    const pid_t sleeper = ::fork();
    if (sleeper == 0) {
        ::execl("/bin/sleep", "sleep", "30", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    REQUIRE(sleeper > 0);
    CHECK(rig.run(2, std::to_string(sleeper), "ETUDE_QUIT_WAIT=1") == 1);
    ::kill(sleeper, SIGKILL);
    int status = 0;
    ::waitpid(sleeper, &status, 0);
    CHECK(version_of(rig.bundle) == "1.0.0");
    CHECK_FALSE(fs::exists(rig.new_bundle));
    CHECK_FALSE(fs::exists(rig.previous));
    CHECK_FALSE(fs::exists(rig.staging));
    CHECK(rig.opened().empty());
    CHECK(status_value(rig.update / "status", "state") == "failed");
}

TEST_CASE("mac relaunch: a kept previous version of the same number is replaced, not moved into") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("2.0.0");
    fake_bundle(rig.previous, "stale");
    CHECK(rig.run(10) == 0);
    CHECK(version_of(rig.previous) == "1.0.0");
    CHECK_FALSE(fs::exists(rig.previous / ".etude-1.0.0.previous"));
    CHECK_FALSE(fs::exists(rig.previous / "etude.app"));
}

TEST_CASE("mac relaunch: a swap program that cannot exchange leaves the old app at its path and says why") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("2.0.0");
    rig.swap_env = "ETUDE_SWAP=/usr/bin/false";
    CHECK(rig.run(2) == 1);
    CHECK(version_of(rig.bundle) == "1.0.0");
    CHECK(fs::is_directory(rig.bundle));
    CHECK_FALSE(fs::exists(rig.previous));
    CHECK_FALSE(fs::exists(rig.staging));
    CHECK(rig.opened() == rig.bundle.string() + " 1.0.0\n");
    CHECK(status_value(rig.update / "status", "state") == "failed");
    CHECK(status_value(rig.update / "status", "reason").find("nothing was changed") != std::string::npos);
    CHECK(read_file(rig.update / "relaunch.log").find("swap") != std::string::npos);
}

TEST_CASE("mac relaunch: a folder that cannot be written leaves the old app at its path") {
    if (!swap_supported() || ::geteuid() == 0) {
        return; // root writes anywhere
    }
    SwapRig rig("2.0.0");
    ::chmod(rig.apps.c_str(), 0555);
    ::chmod(rig.staging.c_str(), 0555);
    const int code = rig.run(2);
    ::chmod(rig.staging.c_str(), 0755);
    ::chmod(rig.apps.c_str(), 0755);
    CHECK(code == 1);
    CHECK(fs::is_directory(rig.bundle));
    CHECK(version_of(rig.bundle) == "1.0.0");
    CHECK(status_value(rig.update / "status", "state") == "failed");
}

TEST_CASE("mac relaunch: with no staged bundle the old app stays and the swap program is never needed") {
    SwapRig rig("2.0.0");
    fs::remove_all(rig.new_bundle);
    rig.swap_env = ""; // the copy of the new program, which is not there
    CHECK(rig.run(2) == 1);
    CHECK(fs::is_directory(rig.bundle));
    CHECK(version_of(rig.bundle) == "1.0.0");
    CHECK(rig.opened() == rig.bundle.string() + " 1.0.0\n");
    CHECK(status_value(rig.update / "status", "state") == "failed");
}

TEST_CASE("mac relaunch: an old bundle that cannot be moved to its kept name stays whole where it is") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("2.0.0");
    rig.previous = rig.dir.path / "no-such-folder/.etude-1.0.0.previous";
    CHECK(rig.run(10) == 0); // the new version runs; the old one is kept at the staged path
    CHECK(version_of(rig.bundle) == "2.0.0");
    CHECK(version_of(rig.new_bundle) == "1.0.0");
    CHECK(status_value(rig.update / "status", "state") == "ok");
}

TEST_CASE("mac relaunch: a rollback whose swap fails leaves a whole app at the path") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig(""); // the new version never reaches its first frame
    // The first exchange works, the second (the rollback) does not.
    const fs::path flaky = rig.dir.path / "flaky";
    write_file(flaky, std::string("#!/bin/sh\nif [ -f '") + (rig.dir.path / "used").string() +
                          "' ]; then exit 1; fi\ntouch '" + (rig.dir.path / "used").string() + "'\nexec '" +
                          ETUDE_APP_PATH + "' --swap-bundle \"$@\"\n");
    ::chmod(flaky.c_str(), 0755);
    rig.swap_env = "ETUDE_SWAP='" + flaky.string() + "'";
    CHECK(rig.run(1) == 1);
    CHECK(fs::is_directory(rig.bundle));
    CHECK(version_of(rig.bundle) == "2.0.0"); // the new one stays, the old is kept
    CHECK(version_of(rig.previous) == "1.0.0"); // the old one is kept
    CHECK(status_value(rig.update / "status", "state") == "failed");
}

TEST_CASE("mac relaunch: the swap program is a copy of the new bundle's own, used for the swap and the rollback") {
    if (!swap_supported()) {
        return;
    }
    SwapRig rig("");
    // The new bundle's program hands the mode on to the built app, as the real one would handle it.
    write_file(rig.new_bundle / "Contents/MacOS/etude",
               std::string("#!/bin/sh\nexec '") + ETUDE_APP_PATH + "' \"$@\"\n");
    ::chmod((rig.new_bundle / "Contents/MacOS/etude").c_str(), 0755);
    rig.swap_env = ""; // the script copies the program out of the staged bundle
    CHECK(rig.run(1) == 1);
    CHECK(fs::exists(rig.update / "swap-tool"));
    CHECK(version_of(rig.bundle) == "1.0.0");
    CHECK(status_value(rig.update / "status", "state") == "rolled-back");
}
