// SPDX-License-Identifier: GPL-3.0-or-later
// The update check as the frame thread sees it: a folder stands in for GitHub (a FolderFetcher), so
// no test reaches the network. The check runs on a thread of its own, the page only reads views,
// and nothing installs.
#include "../app/loop_wake.hpp"
#include "../app/update_service.hpp"
#include "../app/update_status.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <span>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

const std::string kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const std::string kTar = "etude-1.2.0-trixie-arm64.tar.gz";
const std::string kBase = "https://github.com/techneissa/etude/releases/download/v1.2.0/";

std::string asset(const std::string& name) {
    return "{\"name\": \"" + name + "\", \"size\": 5, \"browser_download_url\": \"" + kBase +
           name + "\"}";
}

const std::string kFeed =
    "{\"tag_name\": \"v1.2.0\", \"draft\": false, \"prerelease\": false, \"body\": \"## What's "
    "Changed\\n* Faster start by @x in https://github.com/techneissa/etude/pull/1\\n* Fewer "
    "crashes\", \"assets\": [" + asset("release.json") + "," + asset(kTar) + "," +
    asset(kTar + ".minisig") + "]}";

const std::string kInfo =
    "{\"version\": \"1.2.0\", \"os\": \"trixie\", \"arch\": \"arm64\", \"min_os\": \"13\", "
    "\"file\": \"" + kTar + "\", \"sha256\": \"" + kHash + "\", \"size\": 5}";

// A folder for GitHub, with two faults a test can switch on: no connection for release.json, and a
// fetch that stays open until the service cancels it.
class ScriptFetcher final : public Fetcher {
  public:
    explicit ScriptFetcher(std::string dir) : inner_(std::move(dir)) {}

    FetchResult fetch(std::string_view url, std::span<const std::string_view> hosts,
                      std::uint64_t max_bytes, const FetchProgress& progress) override {
        if (hold.load()) {
            held.store(true);
            while (progress(0, 0)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            FetchResult cancelled;
            cancelled.cancelled = true;
            return cancelled;
        }
        if (info_offline.load() && url.size() >= 12 && url.substr(url.size() - 12) == "release.json") {
            ++inner_fetches_;
            FetchResult none;
            none.reason = "No connection";
            return none;
        }
        return inner_.fetch(url, hosts, max_bytes, progress);
    }
    int fetches() const { return inner_.fetches() + inner_fetches_.load(); }

    std::atomic<bool> hold{false};
    std::atomic<bool> held{false}; // a fetch is open
    std::atomic<bool> info_offline{false};

  private:
    FolderFetcher inner_;
    std::atomic<int> inner_fetches_{0};
};

struct Rig {
    TempDir root;
    ScriptFetcher* fetcher = nullptr;
    std::atomic<std::int64_t> now{1'800'000'000};

    Rig() {
        write_file(root.path / "sites/api.github.com/repos/techneissa/etude/releases/latest",
                   kFeed);
        write_file(root.path / "sites/github.com/techneissa/etude/releases/download/v1.2.0/"
                               "release.json",
                   kInfo);
    }

    UpdateOptions options(const std::string& version = "1.1.0", bool key_ready = true) {
        UpdateOptions o;
        auto f = std::make_unique<ScriptFetcher>((root.path / "sites").string());
        fetcher = f.get();
        o.fetcher = std::move(f);
        o.current_version = version;
        o.machine.arch = "arm64";
        o.machine.id = "debian";
        o.machine.version_id = "13";
        o.key_ready = key_ready;
        o.installs = true;
        o.update_dir = root.path / "update";
        fs::create_directories(o.update_dir);
        o.state_file = root.path / "state/update-check.txt";
        o.clock = [this] { return now.load(); };
        o.poll_ms = 10;
        o.startup_delay_ms = 0;
        return o;
    }
};

template <typename Pred> bool wait_for(UpdateService& s, Pred&& done, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (done(s.update())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool is(const ui::UpdateView& v, ui::UpdatePhase p) { return v.phase == p; }

} // namespace

TEST_CASE("update service: a build without the update key offers nothing") {
    Rig rig;
    UpdateService s(rig.options("1.1.0", false));
    CHECK(s.update().phase == ui::UpdatePhase::NotSetUp);
    s.check_now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(s.update().phase == ui::UpdatePhase::NotSetUp);
    CHECK(rig.fetcher->fetches() == 0);
}

TEST_CASE("update service: nothing is fetched until asked") {
    Rig rig;
    UpdateService s(rig.options());
    CHECK(s.update().phase == ui::UpdatePhase::NotChecked);
    CHECK(s.update().last_checked_s == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(rig.fetcher->fetches() == 0);
}

TEST_CASE("update service: a newer release is offered with its notes and its page") {
    Rig rig;
    UpdateService s(rig.options());
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    const ui::UpdateView& v = s.update();
    CHECK(v.version == "1.2.0");
    CHECK(v.installable);
    CHECK(v.detail.empty());
    CHECK(v.last_checked_s == rig.now.load());
    CHECK(v.release_page == "https://github.com/techneissa/etude/releases/tag/v1.2.0");
    REQUIRE(v.notes.size() == 2);
    CHECK(v.notes[0] == "Faster start");
    CHECK(v.notes[1] == "Fewer crashes");
}

TEST_CASE("update service: the same or an older release is up to date") {
    for (const char* version : {"1.2.0", "1.3.0"}) {
        Rig rig;
        UpdateService s(rig.options(version));
        s.check_now();
        REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::UpToDate); }));
        CHECK(s.update().last_checked_s == rig.now.load());
    }
}

TEST_CASE("update service: a machine the release is not built for is told so, and opens the page") {
    Rig rig;
    UpdateOptions o = rig.options();
    o.machine.version_id = "12";
    UpdateService s(std::move(o));
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(s.update().installable);
    CHECK(s.update().detail == "This release is not built for this system");
}

TEST_CASE("update service: a machine that does not install is not offered the install") {
    Rig rig;
    UpdateOptions o = rig.options();
    o.installs = false;
    UpdateService s(std::move(o));
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(s.update().installable);
    CHECK(s.update().detail.empty());
}

TEST_CASE("update service: a release whose files are not up yet is offered by its page") {
    Rig rig;
    fs::remove(rig.root.path / "sites/github.com/techneissa/etude/releases/download/v1.2.0/"
                               "release.json");
    UpdateService s(rig.options());
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(s.update().installable);
    CHECK(s.update().detail == "The files for this system are not on the release yet");
}

TEST_CASE("update service: a release.json that cannot be fetched is a failed check, not a missing file") {
    Rig rig;
    UpdateOptions o = rig.options();
    auto* script = rig.fetcher;
    script->info_offline = true;
    UpdateService s(std::move(o));
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure == "Couldn't connect");
    CHECK(s.update().retry_is_check);
    CHECK(s.update().detail.empty());
    // Back online, Retry finds the release with its files.
    script->info_offline = false;
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    CHECK(s.update().installable);
    CHECK(s.update().detail.empty());
}

TEST_CASE("update service: a failed check says why and keeps the last good time") {
    Rig rig;
    UpdateService s(rig.options());
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    const std::int64_t good = s.update().last_checked_s;
    fs::remove(rig.root.path / "sites/api.github.com/repos/techneissa/etude/releases/latest");
    rig.now += 100;
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Failed); }));
    CHECK(s.update().failure == "GitHub answered 404 (not found)");
    CHECK(s.update().failure_summary == "Not found (404)");
    CHECK(s.update().last_checked_s == good);
    // Not an answer GitHub gave.
    write_file(rig.root.path / "sites/api.github.com/repos/techneissa/etude/releases/latest",
               "<html>");
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) {
        return is(v, ui::UpdatePhase::Failed) &&
               v.failure == "GitHub's answer could not be read" &&
                   v.failure_summary == "Unreadable answer";
    }));
}

TEST_CASE("update service: an up-to-date answer survives a restart with its time") {
    Rig rig;
    {
        UpdateService s(rig.options("1.2.0"));
        s.check_now();
        REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::UpToDate); }));
    }
    UpdateService again(rig.options("1.2.0"));
    CHECK(again.update().phase == ui::UpdatePhase::UpToDate);
    CHECK(again.update().last_checked_s == 1'800'000'000);
}

TEST_CASE("update service: after a restart the state and the time agree") {
    // The app is another version than the one that was checked: the card has no answer, so it shows
    // no time either.
    for (const char* checked_as : {"1.1.0", "1.2.0"}) {
        Rig rig;
        {
            UpdateService s(rig.options(checked_as));
            s.check_now();
            REQUIRE(wait_for(s, [](const ui::UpdateView& v) {
                return is(v, ui::UpdatePhase::Available) || is(v, ui::UpdatePhase::UpToDate);
            }));
        }
        UpdateService again(rig.options(std::string(checked_as) == "1.2.0" ? "1.3.0" : "1.2.0"));
        CHECK(again.update().phase == ui::UpdatePhase::NotChecked);
        CHECK(again.update().last_checked_s == 0);
        CHECK(update_status_text(again.update()) == "Not checked yet");
    }
}

TEST_CASE("update service: after a restart an update that was out shows the stored time") {
    Rig rig;
    {
        UpdateService s(rig.options("1.1.0"));
        s.check_now();
        REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    }
    UpdateService again(rig.options("1.1.0"));
    // Its notes and files are not stored: no answer to show, but the time is, and the line does not
    // say "Not checked yet" over it.
    CHECK(again.update().phase == ui::UpdatePhase::NotChecked);
    CHECK(again.update().last_checked_s == 1'800'000'000);
    CHECK(update_status_text(again.update()) == "Not checked since starting");
    CHECK(last_checked_text(1'800'000'000 + 3600, again.update().last_checked_s) ==
          "Last checked 1 h ago");
}

TEST_CASE("update service: a stored time from the future is kept, shown as unknown, then replaced") {
    Rig rig;
    const std::int64_t future = rig.now.load() + 10 * 24 * 3600;
    write_file(rig.root.path / "state/update-check.txt",
               "last_checked=" + std::to_string(future) + "\nup_to_date_for=1.2.0\n");
    UpdateService s(rig.options("1.2.0"));
    CHECK(s.update().last_checked_s == future); // not clamped, not rewritten
    CHECK(last_checked_text(rig.now.load(), s.update().last_checked_s) == "Last checked: unknown");
    CHECK(read_file(rig.root.path / "state/update-check.txt").find(std::to_string(future)) !=
          std::string::npos);
    s.set_auto_check(true); // a time from the future does not hold the daily check back
    REQUIRE(wait_for(s, [&](const ui::UpdateView& v) { return v.last_checked_s == rig.now.load(); }));
}

TEST_CASE("update service: the state file is replaced whole, never left half-written") {
    Rig rig;
    UpdateService s(rig.options("1.2.0"));
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::UpToDate); }));
    const fs::path file = rig.root.path / "state/update-check.txt";
    CHECK(read_file(file) == "last_checked=1800000000\nlast_tried=1800000000\nup_to_date_for=1.2.0\n");
    CHECK_FALSE(fs::exists(fs::path(file).concat(".part")));
}

TEST_CASE("update service: the daily check runs only when it is on and due, and never installs") {
    Rig rig;
    UpdateService s(rig.options());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(rig.fetcher->fetches() == 0); // off by default
    s.set_auto_check(true);
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    const int fetches = rig.fetcher->fetches();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(rig.fetcher->fetches() == fetches); // a day has not passed
    CHECK(s.update().phase == ui::UpdatePhase::Available); // offered, not installed
    rig.now += kAutoCheckEveryS;
    REQUIRE(wait_for(s, [&](const ui::UpdateView&) { return rig.fetcher->fetches() > fetches; }));
}

TEST_CASE("update service: the daily check tries once a day, a failed try included") {
    Rig rig;
    fs::remove(rig.root.path / "sites/api.github.com/repos/techneissa/etude/releases/latest");
    {
        UpdateService s(rig.options());
        s.set_auto_check(true);
        REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Failed); }));
        const int fetches = rig.fetcher->fetches();
        rig.now += 3600;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(rig.fetcher->fetches() == fetches); // not again after an hour
        s.check_now(); // the button does not wait for the day
        REQUIRE(wait_for(s, [&](const ui::UpdateView&) { return rig.fetcher->fetches() > fetches; }));
    }
    // Nor does a restart retry: the attempt is stored. (options() gives a fresh fetcher.)
    UpdateService again(rig.options());
    again.set_auto_check(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(rig.fetcher->fetches() == 0);
}

TEST_CASE("update service: a change wakes the frame loop") {
    Rig rig;
    UpdateService s(rig.options());
    const WakeCounts before = wake_counts();
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    const auto mine = static_cast<std::size_t>(WakeSource::Update);
    CHECK(wake_counts()[mine] > before[mine]);
}

TEST_CASE("update service: links open in the browser where there is one, else show their sheet") {
    Rig rig;
    std::string opened;
    UpdateOptions with = rig.options();
    with.opens_browser = true;
    UpdateService browser(std::move(with), [&](const std::string& url) {
        opened = url;
        return true;
    });
    CHECK(browser.open_link("https://github.com/techneissa/etude"));
    CHECK(opened == "https://github.com/techneissa/etude");
    CHECK(browser.link_sheet().empty());

    UpdateService none(rig.options());
    CHECK_FALSE(none.open_link("https://buymeacoffee.com/techneissa"));
    CHECK(none.link_sheet() == "https://buymeacoffee.com/techneissa");
    none.set_link_sheet("");
    CHECK(none.link_sheet().empty());
}

TEST_CASE("update service: Update opens the release page where nothing is installed") {
    Rig rig;
    UpdateOptions o = rig.options();
    o.installs = false;
    UpdateService s(std::move(o));
    s.update_now(); // nothing to update yet
    CHECK(s.link_sheet().empty());
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is(v, ui::UpdatePhase::Available); }));
    s.update_now();
    CHECK(s.link_sheet() == "https://github.com/techneissa/etude/releases/tag/v1.2.0");
}

TEST_CASE("update service: quitting in the middle of a check returns promptly") {
    Rig rig;
    auto service = std::make_unique<UpdateService>(rig.options());
    ScriptFetcher* script = rig.fetcher;
    script->hold = true;
    service->check_now();
    for (int i = 0; i < 2000 && !script->held.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(script->held.load()); // the check is in flight, inside the fetcher
    CHECK(service->update().phase == ui::UpdatePhase::Checking);
    auto destroyed = std::async(std::launch::async, [&] { service.reset(); });
    const bool prompt = destroyed.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    if (!prompt) {
        script->hold = false; // let the stuck fetch end so the test can finish
    }
    CHECK(prompt);
}

TEST_CASE("update service: the machine is read from the files, empty where there are none") {
    const MachineOsText m = read_machine_os();
#if defined(__linux__)
    CHECK_FALSE(m.arch.empty());
#elif defined(__APPLE__)
    // A Mac says so, with its macOS version, so that release-macos.json is the file it reads.
    CHECK(m.id == "macos");
    CHECK_FALSE(m.version_id.empty());
    CHECK(m.debian_version.empty());
#else
    CHECK(m.arch.empty());
#endif
}
