// SPDX-License-Identifier: GPL-3.0-or-later
// The install half of the update service: the signature check over the tarball's own bytes, the
// look into the archive, the wait for the idle screen, and the hand-over to the root helper: the
// tarball and its signature left in the update folder, then the request. Nothing is unpacked by the
// app. A folder stands in for GitHub (a FolderFetcher), the signature check is the test's own (the
// real one has tests of its own), and the helper's part is played by the test writing its files.
#include "../app/update_service.hpp"
#include "tar_builder.hpp"
#include "update_archive_fixture.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

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

} // namespace


namespace {

// A release 1.2.3 whose tarball is the sample from update_archive_fixture.hpp (or `tarball`), served
// by a folder, for a machine that installs. The signature check is the test's own: it records what
// it was asked and says what the test wants.
struct InstallRig {
    TempDir root;
    FolderFetcher* fetcher = nullptr;
    std::atomic<int> verified{0};
    std::vector<std::uint8_t> checked_bytes;
    std::string checked_signature;
    std::string checked_comment;
    std::atomic<bool> signature_good{true};
    std::function<void()> on_verify;
    const std::string tar_name = "etude-1.2.3-trixie-arm64.tar.gz";
    const std::string base = "https://github.com/techneissa/etude/releases/download/v1.2.3/";

    explicit InstallRig(const std::vector<std::uint8_t>& tarball = {}) {
        const std::vector<std::uint8_t> sample(update_fixture::sample_tar_gz().begin(),
                                               update_fixture::sample_tar_gz().end());
        const std::vector<std::uint8_t>& bytes = tarball.empty() ? sample : tarball;
        const auto asset = [&](const std::string& name) {
            return "{\"name\": \"" + name + "\", \"size\": 5, \"browser_download_url\": \"" + base +
                   name + "\"}";
        };
        const std::string feed =
            "{\"tag_name\": \"v1.2.3\", \"draft\": false, \"prerelease\": false, \"body\": \"Fixes\", "
            "\"assets\": [" + asset("release.json") + "," + asset(tar_name) + "," +
            asset(tar_name + ".minisig") + "]}";
        const std::string info =
            "{\"version\": \"1.2.3\", \"os\": \"trixie\", \"arch\": \"arm64\", \"min_os\": \"13\", "
            "\"file\": \"" + tar_name + "\", \"sha256\": \"" + kHash + "\", \"size\": " +
            std::to_string(bytes.size()) + "}";
        const fs::path sites = root.path / "sites";
        write_file(sites / "api.github.com/repos/techneissa/etude/releases/latest", feed);
        const fs::path download = sites / "github.com/techneissa/etude/releases/download/v1.2.3";
        write_file(download / "release.json", info);
        write_file(download / tar_name, std::string(bytes.begin(), bytes.end()));
        write_file(download / (tar_name + ".minisig"), "untrusted comment: a signature\nRUQ=\n");
        fs::create_directories(root.path / "update");
    }

    fs::path update_dir() const { return root.path / "update"; }

    UpdateOptions options() {
        UpdateOptions o;
        auto f = std::make_unique<FolderFetcher>((root.path / "sites").string());
        fetcher = f.get();
        o.fetcher = std::move(f);
        o.current_version = "1.1.0";
        o.machine.arch = "arm64";
        o.machine.id = "debian";
        o.machine.version_id = "13";
        o.key_ready = true;
        o.installs = true;
        o.update_dir = update_dir();
        o.state_file = root.path / "state/update-check.txt";
        o.poll_ms = 10;
        o.startup_delay_ms = 0;
        o.pickup_wait_ms = 1500;
        o.verify = [this](std::span<const std::uint8_t> data, std::string_view signature,
                          std::string_view comment) {
            checked_bytes.assign(data.begin(), data.end());
            checked_signature = std::string(signature);
            checked_comment = std::string(comment);
            ++verified;
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

bool is_phase(const ui::UpdateView& v, ui::UpdatePhase p) { return v.phase == p; }

// Checks, and waits for what the service offers.
void offer_update(UpdateService& s) {
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) {
        return is_phase(v, ui::UpdatePhase::Available) && v.installable;
    }));
}

bool wait_for_file(const fs::path& file, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (fs::exists(file)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // namespace

TEST_CASE("update install: the tarball is checked by signature over its own bytes, left for the "
          "helper with its signature, and a request follows") {
    InstallRig rig;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
    CHECK(read_file(rig.update_dir() / "request") == "version=1.2.3\n");
    // What the signature check was given: the tarball's bytes, the .minisig's text, and the trusted
    // comment release.json announced.
    CHECK(rig.verified == 1);
    CHECK(rig.checked_bytes == std::vector<std::uint8_t>(update_fixture::sample_tar_gz().begin(),
                                                         update_fixture::sample_tar_gz().end()));
    CHECK(rig.checked_signature == "untrusted comment: a signature\nRUQ=\n");
    CHECK(rig.checked_comment == "etude 1.2.3 trixie arm64");
    // And the tarball and its signature wait in the update folder as they came, for the helper to
    // copy and check again; nothing is unpacked, and no temporary file is left.
    CHECK(read_file(rig.update_dir() / "release.tar.gz") == std::string(update_fixture::sample_tar_gz().begin(), update_fixture::sample_tar_gz().end()));
    CHECK(read_file(rig.update_dir() / "release.tar.gz.minisig") == "untrusted comment: a signature\nRUQ=\n");
    CHECK_FALSE(fs::exists(rig.update_dir() / "staging"));
    CHECK(tree(rig.update_dir()) == "release.tar.gz\nrelease.tar.gz.minisig\nrequest\n");
    // The request lands before the service publishes the view that withdraws Cancel (#434), so the
    // view is awaited rather than read at once; a service that left Cancel on would never reach it.
    CHECK(wait_for(s, [](const ui::UpdateView& v) {
        return is_phase(v, ui::UpdatePhase::Installing) && !v.cancellable;
    }));
}

TEST_CASE("update install: the helper's refusal is shown, and what the app left is removed") {
    InstallRig rig;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
    write_file(rig.update_dir() / "status",
               "state=failed\nversion=1.2.3\nfrom=1.1.0\nreason=the unpacked folder was refused: no "
               "bin/etude\n");
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure ==
          "Update to 1.2.3 failed: the unpacked folder was refused: no bin/etude");
    CHECK_FALSE(s.update().retry_is_check);
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.tar.gz"));
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.tar.gz.minisig"));
}

TEST_CASE("update install: a request nobody picks up is a failure, and is taken back") {
    InstallRig rig;
    UpdateOptions o = rig.options();
    o.pickup_wait_ms = 50;
    UpdateService s(std::move(o));
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure == "The installer did not pick the update up");
    CHECK_FALSE(fs::exists(rig.update_dir() / "request"));
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.tar.gz"));
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.tar.gz.minisig"));
}

TEST_CASE("update install: a signature that does not check stops the install before anything is "
          "left for the helper") {
    InstallRig rig;
    rig.signature_good = false;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure == "Signature doesn't match the release key");
    CHECK(s.update().failure_summary == "Signature doesn't match");
    CHECK(tree(rig.update_dir()).empty());
}

TEST_CASE("update install: a download that is not the size release.json says is refused") {
    InstallRig rig;
    UpdateOptions o = rig.options();
    // The same file served, a size one byte short announced.
    const fs::path info = rig.root.path / "sites/github.com/techneissa/etude/releases/download/"
                                          "v1.2.3/release.json";
    std::string text = read_file(info);
    const std::size_t at = text.rfind(std::to_string(update_fixture::sample_tar_gz().size()));
    REQUIRE(at != std::string::npos);
    text.replace(at, std::to_string(update_fixture::sample_tar_gz().size()).size(),
                 std::to_string(update_fixture::sample_tar_gz().size() + 7));
    write_file(info, text);
    UpdateService s(std::move(o));
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure == "The download is not the size the release said");
    CHECK(rig.verified == 0);
}

TEST_CASE("update install: an archive that is not a release's is refused, whatever its signature") {
    using tar_test::Tar;
    const std::string top = "etude-1.2.3";
    const auto served = [&](const Tar& tar) {
        return tar_test::gzip_stored(tar.done());
    };
    struct Case {
        const char* name;
        Tar tar;
        std::string failure;
    };
    Tar climbing;
    climbing.dir(top).dir(top + "/bin").file(top + "/bin/etude", "x", 0755).symlink(top + "/bin/out", "../../..");
    Tar absolute;
    absolute.dir(top).file(top + "/../escape", "x");
    Tar elsewhere;
    elsewhere.dir(top).file("somewhere-else/file", "x");
    Tar device;
    device.dir(top).add(top + "/null", '3');
    for (const Case& c : {Case{"a link out of the folder", climbing,
                               "The download was refused: the archive has a link that leaves its folder: bin/out"},
                          Case{"a path that climbs", absolute,
                               "The download was refused: the archive has a path that is not allowed: "
                               "etude-1.2.3/../escape"},
                          Case{"a file outside the top folder", elsewhere,
                               "The download was refused: the archive has a file outside its folder: "
                               "somewhere-else/file"},
                          Case{"a device", device,
                               "The download was refused: the archive holds a kind of file that is not allowed: "
                               "etude-1.2.3/null"}}) {
        CAPTURE(c.name);
        InstallRig rig(served(c.tar));
        UpdateService s(rig.options());
        offer_update(s);
        s.update_now();
        REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
        CHECK(s.update().failure.rfind("The download was refused: the archive", 0) == 0);
        CHECK(tree(rig.update_dir()).empty()); // nothing was left for the helper
    }
    // Garbage that is not even gzip.
    InstallRig garbage(std::vector<std::uint8_t>(300, 0x41));
    UpdateService s(garbage.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure == "The download could not be unpacked");
}

TEST_CASE("update install: only from the idle screen") {
    InstallRig rig;
    UpdateService s(rig.options());
    offer_update(s);
    s.set_busy(true);
    CHECK(s.update().busy);
    s.update_now(); // a song is playing: nothing starts
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(s.update().phase == ui::UpdatePhase::Available);
    CHECK(rig.verified == 0);
    s.set_busy(false);
    CHECK_FALSE(s.update().busy);
    s.update_now();
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
}

TEST_CASE("update install: a song that starts during the download holds the request back") {
    InstallRig rig;
    UpdateService* service = nullptr;
    rig.on_verify = [&] { service->set_busy(true); }; // the player starts a song mid-install
    UpdateService s(rig.options());
    service = &s;
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) {
        return v.detail == "Waiting until nothing is playing";
    }));
    CHECK(s.update().cancellable);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK_FALSE(fs::exists(rig.update_dir() / "request")); // it waits
    s.set_busy(false);
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
}

TEST_CASE("update install: Cancel while it waits for the idle screen puts everything back") {
    InstallRig rig;
    UpdateService* service = nullptr;
    rig.on_verify = [&] { service->set_busy(true); };
    UpdateService s(rig.options());
    service = &s;
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) {
        return v.detail == "Waiting until nothing is playing";
    }));
    s.cancel();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Available); }));
    CHECK(tree(rig.update_dir()).empty());
    // The offer still stands: Update tries again.
    s.set_busy(false);
    service = nullptr;
    rig.on_verify = nullptr;
    s.update_now();
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
}

TEST_CASE("update install: Retry after a failed install runs it again") {
    InstallRig rig;
    rig.signature_good = false;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); }));
    CHECK_FALSE(s.update().retry_is_check);
    rig.signature_good = true;
    s.update_now(); // Retry
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
    CHECK(rig.verified == 2);
}

TEST_CASE("update install: nothing is installed where there is no update folder or no install") {
    InstallRig rig;
    UpdateOptions none = rig.options();
    none.update_dir.clear();
    UpdateService no_folder(std::move(none));
    no_folder.check_now();
    REQUIRE(wait_for(no_folder, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(no_folder.update().installable);
    no_folder.update_now(); // opens the page instead
    CHECK(no_folder.link_sheet() == "https://github.com/techneissa/etude/releases/tag/v1.2.3");
    CHECK(rig.verified == 0);
}

TEST_CASE("update install: a new version tells the helper once that its first frame is up and its history open") {
    InstallRig rig;
    UpdateService s(rig.options());
    s.startup_progress(true, HistoryOpen::Pending); // the frame alone is not enough
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(fs::exists(rig.update_dir() / "ready"));
    s.startup_progress(true, HistoryOpen::Opened);
    REQUIRE(wait_for_file(rig.update_dir() / "ready"));
    CHECK(read_file(rig.update_dir() / "ready") == "version=1.1.0\n");
    fs::remove(rig.update_dir() / "ready");
    s.startup_progress(true, HistoryOpen::Opened); // asked once only
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(fs::exists(rig.update_dir() / "ready"));
    // No folder, no file and no trouble.
    UpdateOptions none = rig.options();
    none.update_dir = rig.root.path / "absent";
    UpdateService quiet(std::move(none));
    quiet.startup_progress(true, HistoryOpen::Opened);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(fs::exists(rig.root.path / "absent"));
}

TEST_CASE("update install: a history that failed to open or migrate means ready is never written") {
    InstallRig rig;
    UpdateService s(rig.options());
    s.startup_progress(true, HistoryOpen::Failed);
    s.startup_progress(true, HistoryOpen::Opened); // not even if it then reads as opened
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK_FALSE(fs::exists(rig.update_dir() / "ready"));
}

TEST_CASE("update install: a version that starts after a rollback says why") {
    InstallRig rig;
    write_file(rig.update_dir() / "status",
               "state=rolled-back\nversion=1.2.0\nfrom=1.1.0\nreason=1.2.0 did not reach its first "
               "frame in 30 s, twice; went back to 1.1.0\n");
    UpdateService s(rig.options()); // the app is 1.1.0 again
    CHECK(s.update().phase == ui::UpdatePhase::Failed);
    CHECK(s.update().failure ==
          "Update to 1.2.0 failed: 1.2.0 did not reach its first frame in 30 s, twice; went back to "
          "1.1.0");
    CHECK(s.update().retry_is_check);
    // A verdict about a version that is not ahead of this one is not news.
    write_file(rig.update_dir() / "status", "state=failed\nversion=1.0.0\nreason=old\n");
    UpdateService old(rig.options());
    CHECK(old.update().phase == ui::UpdatePhase::NotChecked);
    write_file(rig.update_dir() / "status", "state=ok\nversion=1.2.0\nfrom=1.1.0\n");
    UpdateService fine(rig.options());
    CHECK(fine.update().phase == ui::UpdatePhase::NotChecked);
}

TEST_CASE("update install: the update folder is found from where the app runs") {
    CHECK(update_dir_for("/opt/etude/versions/1.2.3/bin/etude") == fs::path("/opt/etude/update"));
    CHECK(update_dir_for("/srv/app/versions/0.10.0/bin/etude") == fs::path("/srv/app/update"));
    // Run from a build folder, a Mac, a version that is not X.Y.Z, or another program: no install.
    CHECK(update_dir_for("/home/me/etude/build/app/etude").empty());
    CHECK(update_dir_for("/opt/etude/bin/etude").empty());
    CHECK(update_dir_for("/opt/etude/versions/current/bin/etude").empty());
    CHECK(update_dir_for("/opt/etude/versions/1.2.3/bin/display-spike").empty());
    CHECK(update_dir_for("/opt/etude/versions/1.2.3-rc.1/bin/etude").empty());
    CHECK(update_dir_for("versions/1.2.3/bin/etude").empty());
    CHECK(update_dir_for("").empty());
}
