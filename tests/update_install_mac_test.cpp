// SPDX-License-Identifier: GPL-3.0-or-later
// The Mac's install in the update service: which release.json a Mac reads and which file it picks,
// the signature check from a hash of the saved zip, the unpack and check of the new bundle (ditto,
// codesign and plutil are stood in for), the hand-over to the relaunch script and the quit, and every
// way it ends short of that. A folder stands in for GitHub; the swap itself is tests/mac_update_test.cpp's.
#include "../app/update_service.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

const std::string kZipBytes = "PK-pretend-zip-bytes-of-a-bundle";
const std::string kSignature = "untrusted comment: a signature\nRUQ=\n";
const std::string kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

template <typename Pred> bool wait_for(UpdateService& s, Pred&& done, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (done(s.update())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool is_phase(const ui::UpdateView& v, ui::UpdatePhase p) { return v.phase == p; }

// Release 1.2.3 for a Mac, served by a folder; the running app is 1.1.0 in <root>/Apps.
struct MacRig {
    TempDir root;
    const std::string zip_name = "etude-1.2.3-macos-arm64.zip";
    const std::string base = "https://github.com/techneissa/etude/releases/download/v1.2.3/";
    std::atomic<int> quits{0};
    std::vector<std::vector<std::string>> programs; // what the runner was asked
    std::vector<std::vector<std::string>> spawned;
    std::string spawn_log;
    std::string plist_version = "1.2.3";
    int codesign_exit = 0;
    bool spawn_ok = true;
    std::atomic<bool> signature_good{true};
    std::vector<std::uint8_t> checked_digest;
    std::string checked_comment;
    std::function<void()> on_verify;
    fs::path apps = root.path / "Apps";
    fs::path bundle = apps / "etude.app";

    MacRig() {
        const auto asset = [&](const std::string& name) {
            return "{\"name\": \"" + name + "\", \"size\": 5, \"browser_download_url\": \"" + base + name + "\"}";
        };
        const std::string feed =
            "{\"tag_name\": \"v1.2.3\", \"draft\": false, \"prerelease\": false, \"body\": \"Fixes\", "
            "\"assets\": [" + asset("release.json") + "," + asset("release-macos.json") + "," +
            asset(zip_name) + "," + asset(zip_name + ".minisig") + "]}";
        // The Pi's release.json is there too: a Mac must not read it.
        const std::string pi_info =
            "{\"version\": \"1.2.3\", \"os\": \"trixie\", \"arch\": \"arm64\", \"min_os\": \"13\", "
            "\"file\": \"etude-1.2.3-trixie-arm64.tar.gz\", \"sha256\": \"" + kHash + "\", \"size\": 5}";
        const std::string mac_info =
            "{\"version\": \"1.2.3\", \"os\": \"macos\", \"arch\": \"arm64\", \"min_os\": \"14\", "
            "\"file\": \"" + zip_name + "\", \"sha256\": \"" + kHash + "\", \"size\": " +
            std::to_string(kZipBytes.size()) + "}";
        const fs::path sites = root.path / "sites";
        write_file(sites / "api.github.com/repos/techneissa/etude/releases/latest", feed);
        const fs::path download = sites / "github.com/techneissa/etude/releases/download/v1.2.3";
        write_file(download / "release.json", pi_info);
        write_file(download / "release-macos.json", mac_info);
        write_file(download / zip_name, kZipBytes);
        write_file(download / (zip_name + ".minisig"), kSignature);
        write_file(bundle / "Contents/MacOS/etude", "old");
    }

    fs::path update_dir() const { return root.path / "update"; }
    fs::path staging() const { return apps / ".etude-update-1.2.3"; }

    UpdateOptions options() {
        UpdateOptions o;
        o.fetcher = std::make_unique<FolderFetcher>((root.path / "sites").string());
        o.current_version = "1.1.0";
        o.machine.arch = "arm64";
        o.machine.id = "macos";
        o.machine.version_id = "14.5";
        o.key_ready = true;
        o.installs = true;
        o.update_dir = update_dir();
        o.state_file = root.path / "state/update-check.txt";
        o.poll_ms = 10;
        o.startup_delay_ms = 0;
        o.mac.bundle = bundle;
        o.mac.pid = 4242;
        o.mac.quit = [this] { ++quits; };
        o.mac.run = [this](const std::vector<std::string>& argv, int) {
            programs.push_back(argv);
            ProgramResult done;
            done.started = true;
            done.exit_code = 0;
            if (argv[0] == "/usr/bin/ditto") {
                const fs::path app = fs::path(argv[4]) / "etude.app";
                write_file(app / "Contents/MacOS/etude", "new");
                ::chmod((app / "Contents/MacOS/etude").c_str(), 0755);
            } else if (argv[0] == "/usr/bin/codesign") {
                done.exit_code = codesign_exit;
            } else {
                done.output = plist_version + "\n";
            }
            return done;
        };
        o.mac.spawn = [this](const std::vector<std::string>& argv, const std::string& log) {
            spawned.push_back(argv);
            spawn_log = log;
            return spawn_ok;
        };
        o.verify_digest = [this](const Digest512& digest, std::string_view, std::string_view comment) {
            checked_digest.assign(digest.begin(), digest.end());
            checked_comment = std::string(comment);
            if (on_verify) {
                on_verify();
            }
            SignatureCheck result;
            if (!signature_good) {
                result.error = SignatureError::BadSignature;
            }
            return result;
        };
        return o;
    }
};

void offer_update(UpdateService& s) {
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) {
        return is_phase(v, ui::UpdatePhase::Available) && v.installable;
    }));
}

bool wait_until(const std::function<bool()>& done, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // namespace

TEST_CASE("mac install: a Mac reads release-macos.json and picks the zip, never the Pi's tarball") {
    MacRig rig;
    UpdateService s(rig.options());
    offer_update(s);
    CHECK(s.update().version == "1.2.3");
    CHECK(s.update().detail.empty());
}

TEST_CASE("mac install: a Mac sees no release when only the Pi's release.json is up") {
    MacRig rig;
    fs::remove(rig.root.path / "sites/github.com/techneissa/etude/releases/download/v1.2.3/release-macos.json");
    UpdateService s(rig.options());
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(s.update().installable);
    CHECK(s.update().detail == "The files for this system are not on the release yet");
}

TEST_CASE("mac install: the zip is checked by a hash of its saved bytes, unpacked beside the app, checked, "
          "and handed to the relaunch script before the app quits") {
    MacRig rig;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    INFO(s.update().failure);
    REQUIRE(wait_until([&] { return rig.quits.load() == 1 || s.update().phase == ui::UpdatePhase::Failed; }));
    INFO(s.update().failure);
    REQUIRE(rig.quits == 1);

    // The signature was checked over the BLAKE2b-512 of the zip's bytes, with the comment of the file.
    FileDigest whole;
    whole.update({reinterpret_cast<const std::uint8_t*>(kZipBytes.data()), kZipBytes.size()});
    const Digest512 expected = whole.finish();
    CHECK(rig.checked_digest == std::vector<std::uint8_t>(expected.begin(), expected.end()));
    CHECK(rig.checked_comment == "etude 1.2.3 macos arm64");

    // ditto unpacked into a folder beside the running bundle; then codesign and plutil looked at it.
    REQUIRE(rig.programs.size() == 3);
    CHECK(rig.programs[0][0] == "/usr/bin/ditto");
    CHECK(rig.programs[0][3] == (rig.update_dir() / "release.zip").string());
    CHECK(rig.programs[0][4] == rig.staging().string());
    CHECK(rig.programs[1][0] == "/usr/bin/codesign");
    CHECK(rig.programs[1][4] == (rig.staging() / "etude.app").string());
    CHECK(rig.programs[2][0] == "/usr/bin/plutil");

    // The script was started with the running bundle, the new one, the kept one and the version.
    REQUIRE(rig.spawned.size() == 1);
    const std::vector<std::string>& argv = rig.spawned[0];
    REQUIRE(argv.size() == 11);
    CHECK(argv[0] == "/bin/sh");
    CHECK(argv[2] == "4242");
    CHECK(argv[3] == rig.bundle.string());
    CHECK(argv[4] == (rig.staging() / "etude.app").string());
    CHECK(argv[5] == (rig.apps / ".etude-1.1.0.previous").string());
    CHECK(argv[8] == "1.2.3");
    CHECK(argv[9] == "1.1.0");
    CHECK(argv[10] == "30");
    CHECK(rig.spawn_log == (rig.update_dir() / "relaunch.log").string());

    // Nothing was moved by the app itself, and the zip is gone from the update folder.
    CHECK(read_file(rig.bundle / "Contents/MacOS/etude") == "old");
    CHECK(read_file(rig.staging() / "etude.app/Contents/MacOS/etude") == "new");
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.zip"));
    CHECK(fs::exists(rig.update_dir() / "relaunch.sh"));
    CHECK(wait_for(s, [](const ui::UpdateView& v) {
        return is_phase(v, ui::UpdatePhase::Installing) && !v.cancellable &&
               v.detail == "Restarting to finish the install";
    }));
}

TEST_CASE("mac install: a signature that does not check stops it with nothing left behind") {
    MacRig rig;
    rig.signature_good = false;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure == "Signature doesn't match the release key");
    CHECK(rig.programs.empty());
    CHECK(rig.spawned.empty());
    CHECK(rig.quits == 0);
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.zip"));
    CHECK_FALSE(fs::exists(rig.staging()));
}

TEST_CASE("mac install: a new bundle whose code signature does not hold is refused and removed") {
    MacRig rig;
    rig.codesign_exit = 1;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure.find("code signature") != std::string::npos);
    CHECK(rig.spawned.empty());
    CHECK(rig.quits == 0);
    CHECK_FALSE(fs::exists(rig.staging()));
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.zip"));
    CHECK(read_file(rig.bundle / "Contents/MacOS/etude") == "old");
}

TEST_CASE("mac install: a new bundle that says another version than the release is refused") {
    MacRig rig;
    rig.plist_version = "1.2.2";
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure.find("1.2.2") != std::string::npos);
    CHECK(rig.quits == 0);
    CHECK_FALSE(fs::exists(rig.staging()));
}

TEST_CASE("mac install: if the script cannot be started the app stays open and the unpacked copy goes") {
    MacRig rig;
    rig.spawn_ok = false;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(rig.quits == 0);
    CHECK_FALSE(fs::exists(rig.staging()));
    CHECK_FALSE(s.update().retry_is_check);
}

TEST_CASE("mac install: a song that starts mid-install holds the unpack back, and Cancel puts it all back") {
    MacRig rig;
    UpdateService* service = nullptr;
    rig.on_verify = [&] { service->set_busy(true); };
    UpdateService s(rig.options());
    service = &s;
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return v.detail == "Waiting until nothing is playing"; }));
    CHECK(s.update().cancellable);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(rig.programs.empty()); // nothing is unpacked while a song plays
    s.cancel();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.zip"));
    CHECK(rig.quits == 0);
    CHECK(rig.spawned.empty());
}

TEST_CASE("mac install: where the bundle cannot be replaced the status line says why and Update opens the page") {
    MacRig rig;
    UpdateOptions o = rig.options();
    o.installs = false;
    o.mac.blocked = "it runs from a disk image";
    o.opens_browser = true;
    std::vector<std::string> opened;
    UpdateService s(std::move(o), [&](const std::string& url) {
        opened.push_back(url);
        return true;
    });
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(s.update().installable);
    CHECK(s.update().detail ==
          "Can't install here: it runs from a disk image. Update opens the release page");
    s.update_now();
    CHECK(opened == std::vector<std::string>{"https://github.com/techneissa/etude/releases/tag/v1.2.3"});
    CHECK(rig.programs.empty());
    CHECK(rig.quits == 0);
}
