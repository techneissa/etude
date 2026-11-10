// SPDX-License-Identifier: GPL-3.0-or-later
// The root helper's install after the files are staged (`install_staged`): the service states it must
// not install under, a failed save after the switch, and SIGTERM at each stage, run as the current user
// against temporary folders and a stand-in systemctl that can report any state and can send the signal
// itself. The same helper as root, with a signed tarball and a real app, is
// scripts/update-helper-test.sh.
#include "../helper/apply.hpp"
#include "../helper/install.hpp"
#include "helper_rig.hpp"
#include "../helper/service.hpp"

#include <chrono>
#include <fstream>
#include <thread>

using namespace helper_rig;

namespace {

InstallJob job_for(Rig& r, const Fd& update) {
    return InstallJob{r.c, update.get(), "1.2.0", "1.1.0", "1.0.0"};
}

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
    int install() { return install_staged(job_for(r, update)); }
};

} // namespace

TEST_CASE("install: a service that is not exactly inactive or failed is running, and is not installed under") {
    for (const char* word : {"activating", "deactivating", "reloading", "active"}) {
        CAPTURE(word);
        Staged s;
        write_file(s.r.root.path / "answer", std::string(word) + "\n");
        CHECK(s.install() == 1);
        const UpdateStatus status = s.r.status();
        CHECK(status.state == UpdateState::Failed);
        CHECK(status.reason.find("could not be stopped") != std::string::npos);
        CHECK(status.reason.find(word) != std::string::npos);
        // Nothing past the stop was reached: no backup (no `show`), no switch, and the app is asked to
        // start again, since the stop may have half worked.
        CHECK(s.r.calls().find("show") == std::string::npos);
        CHECK(s.r.calls().find("start etude.service") != std::string::npos);
        fs::remove(s.r.root.path / "answer");
        expect_old_version_in_use(s.r);
    }
}

TEST_CASE("install: a bus that does not answer, or a stop that hangs, aborts the update") {
    {
        Staged s;
        write_file(s.r.root.path / "bus_error", "");
        CHECK(s.install() == 1);
        CHECK(s.r.status().reason.find("could not be read") != std::string::npos);
        CHECK(s.r.calls().find("show") == std::string::npos);
        fs::remove(s.r.root.path / "bus_error");
        expect_old_version_in_use(s.r);
    }
    {
        Staged s;
        s.r.c.service_timeout_s = 0.3; // the hung stop only; the calls that ask keep 2 s
        s.r.c.query_floor_s = 2;
        write_file(s.r.root.path / "hang", ""); // stop takes 5 s: the call is killed at 0.3 s
        CHECK(s.install() == 1);
        CHECK(s.r.status().state == UpdateState::Failed);
        CHECK(s.r.status().reason.find("is active after it was told to stop") != std::string::npos);
        CHECK(s.r.calls().find("show") == std::string::npos);
        expect_old_version_in_use(s.r);
    }
    {
        Staged s;
        write_file(s.r.root.path / "stuck", ""); // stop returns, the service does not stop
        CHECK(s.install() == 1);
        CHECK(s.r.calls().find("show") == std::string::npos);
        expect_old_version_in_use(s.r);
    }
}

TEST_CASE("install: a failed save of the state after the switch puts the links back and keeps the old version") {
    // Also when the prefix folder cannot be written out after each link: the renames happened, so that is
    // logged and not a failure (which would have the caller undo or remove what is in use).
    for (const bool fsync_fails : {false, true}) {
        CAPTURE(fsync_fails);
        fail_fsync_in_test(fsync_fails);
        Staged s;
        // The saves are the Switching one, then the Pending one after the links moved: the second fails.
        fail_save_in_test(2);
        CHECK(s.install() == 1);
        fail_save_in_test(0);
        CHECK(s.r.status().state == UpdateState::Failed);
        CHECK(s.r.status().reason.find("could not be switched") != std::string::npos);
        expect_old_version_in_use(s.r);
        // The data was backed up before: it is still there, and the old version was started again.
        CHECK(fs::exists(s.r.data / "update-backup" / "1.1.0" / ".complete"));
        CHECK(s.r.calls().find("start etude.service") != std::string::npos);
    }
    fail_fsync_in_test(false);
}

TEST_CASE("link: a prefix folder that cannot be written out after the rename is logged, and the link counts as done") {
    Rig r;
    std::string error;
    fail_fsync_in_test(true);
    CHECK(point_link(r.c, "current", "1.2.0", error)); // the rename worked: not "nothing changed"
    CHECK(r.current() == "1.2.0");
    CHECK(point_link(r.c, "previous", "", error)); // and the removal of a link too
    CHECK(r.previous().empty());
    fail_fsync_in_test(false);
    // A rename that really fails still says so.
    fail_links_from_in_test(1);
    CHECK_FALSE(point_link(r.c, "current", "1.1.0", error));
    fail_links_from_in_test(0);
    CHECK(r.current() == "1.2.0");
}

TEST_CASE("install: nothing that `current` or `previous` points at is removed, checked just before the removal") {
    Rig r;
    std::string error;
    REQUIRE(point_link(r.c, "current", "1.2.0", error));
    REQUIRE(point_link(r.c, "previous", "1.0.0", error));
    discard_version(r.c, "1.2.0");
    discard_version(r.c, "1.0.0");
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0" / "bin" / "etude"));
    CHECK(fs::exists(r.prefix / "versions" / "1.0.0" / "bin" / "etude"));
    // The old-versions clean-up keeps them too, whatever it was told to keep.
    prune_versions(r.c, "1.1.0", "1.1.0");
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(fs::exists(r.prefix / "versions" / "1.0.0"));
    CHECK(fs::exists(r.prefix / "versions" / "1.1.0"));
    // One that nothing points at goes.
    REQUIRE(point_link(r.c, "previous", "1.1.0", error));
    discard_version(r.c, "1.0.0");
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.0.0"));
}

TEST_CASE("install: a current or previous link in a form the helper does not write blocks every removal") {
    // The helper writes `versions/X`; a `./versions/X` or an absolute link is read as no version at all,
    // and what it points at must not be taken for unused.
    for (const char* form : {"dot", "absolute", "elsewhere"}) {
        CAPTURE(form);
        Rig r;
        const fs::path current = r.prefix / "current";
        fs::remove(current);
        const fs::path target = r.prefix / "versions" / "1.2.0";
        fs::create_symlink(std::string(form) == "dot"        ? fs::path("./versions/1.2.0")
                           : std::string(form) == "absolute" ? target
                                                             : fs::path("/nonexistent/place"),
                           current);
        CHECK(link_version(r.c, "current").empty()); // not read as a version
        CHECK(version_in_use(r.c, "1.2.0"));         // so nothing may go
        CHECK(version_in_use(r.c, "1.0.0"));
        discard_version(r.c, "1.2.0");
        discard_version(r.c, "1.0.0");
        prune_versions(r.c, "1.1.0", "1.1.0");
        for (const char* v : {"1.0.0", "1.1.0", "1.2.0"}) {
            CAPTURE(v);
            CHECK(fs::exists(r.prefix / "versions" / v / "bin" / "etude"));
        }
        // The same for `previous`; and with no `previous` at all there is nothing in the way.
        fs::remove(current);
        std::string error;
        REQUIRE(point_link(r.c, "current", "1.2.0", error));
        fs::remove(r.prefix / "previous");
        CHECK_FALSE(version_in_use(r.c, "1.0.0"));
        fs::create_symlink("./versions/1.0.0", r.prefix / "previous");
        CHECK(version_in_use(r.c, "1.0.0"));
        prune_versions(r.c, "1.2.0", "1.2.0");
        CHECK(fs::exists(r.prefix / "versions" / "1.0.0"));
        CHECK(fs::exists(r.prefix / "versions" / "1.1.0"));
    }
}

TEST_CASE("install: the version current points at is never removed, whatever failed") {
    Rig r;
    std::string error;
    REQUIRE(point_link(r.c, "current", "1.2.0", error));
    discard_version(r.c, "1.2.0");
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0" / "bin" / "etude"));
    CHECK(r.current() == "1.2.0");
    // One that is not in use goes.
    REQUIRE(point_link(r.c, "current", "1.1.0", error));
    discard_version(r.c, "1.2.0");
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
}

TEST_CASE("install: SIGTERM before the switch ends the update, and the old app runs again") {
    struct Stage {
        const char* name;
        const char* hook; // the file the stand-in systemctl looks for
        const char* said; // part of the status reason
        bool backup_reached;
    };
    for (const Stage& st : {Stage{"while the service is stopped", "term_on_stop", "told to stop", false},
                            Stage{"right after it stopped", "term_after_stop", "interrupted", false},
                            Stage{"while the data is backed up", "term_on_show", "interrupted", true},
                            Stage{"as SIGINT", "int_on_stop", "told to stop", false}}) {
        CAPTURE(st.name);
        Staged s;
        write_file(s.r.root.path / st.hook, "");
        CHECK(s.install() == 1);
        CHECK(was_interrupted());
        const UpdateStatus status = s.r.status();
        CHECK(status.state == UpdateState::Failed);
        CAPTURE(status.reason);
        // Which of the two the status gives depends on whether the stop or the next checkpoint saw
        // the signal first (a loaded machine moves it); either names the interrupt.
        CHECK((status.reason.find(st.said) != std::string::npos ||
               status.reason.find("interrupted") != std::string::npos));
        if (!st.backup_reached) {
            CHECK(s.r.calls().find("show") == std::string::npos);
        }
        expect_old_version_in_use(s.r);
        reset_interrupt();
    }
}

TEST_CASE("install: SIGTERM after the switch goes back short: links back, the start queued, the data left to count") {
    struct Stage {
        const char* name;
        const char* hook;
    };
    for (const Stage& st : {Stage{"as the new version is started", "term_on_start"},
                            Stage{"while the new version is waited for", "term_on_restart"},
                            Stage{"as SIGINT as it is started", "int_on_start"}}) {
        CAPTURE(st.name);
        Staged s;
        write_file(s.r.root.path / st.hook, "");
        CHECK(s.install() == 0); // the app could be stopped
        CHECK(was_interrupted());
        // systemd ends the helper soon after SIGTERM, so it does not restore the data itself: the old
        // version is back in place, the record says so, and the app's start is only queued.
        const UpdateStatus status = s.r.status();
        CHECK(status.state == UpdateState::Failed);
        CHECK(status.reason.find("interrupted") != std::string::npos);
        CHECK(s.r.current() == "1.1.0");
        CHECK(s.r.previous() == "1.0.0");
        CHECK(s.r.state().phase == UpdatePhase::RollingBack);
        CHECK(s.r.state().backed_up);
        CHECK(s.r.calls().find("stop etude.service") != std::string::npos);
        CHECK(s.r.calls().find("start --no-block etude.service") != std::string::npos);
        // The queued start runs `count` first, which finishes the going back before the app runs.
        CHECK(count_start(s.r.c) == 0);
        reset_interrupt();
        expect_old_version_in_use(s.r);
        CHECK(s.r.status().state == UpdateState::RolledBack);
    }
}

TEST_CASE("go back: an app that will not stop is stopped again, then killed, and then the old version is put back") {
    Rig r;
    r.start_trial(0);
    r.run_service("etude.service");
    write_file(r.root.path / "stuck", ""); // `systemctl stop` does nothing
    PendingUpdate trial = r.state();
    CHECK(go_back(r.c, Service(r.c), trial, "1.2.0 did not start", false) == 0);
    const std::string calls = r.calls();
    CHECK(calls.find("kill --kill-whom=main --signal=SIGKILL etude.service") != std::string::npos);
    CHECK(calls.find("stop etude.service") != calls.rfind("stop etude.service")); // stopped twice first
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.status().state == UpdateState::RolledBack);
    CHECK(r.status().reason.find("had to be killed") != std::string::npos);
    CHECK(read_file(r.data / "history.db") == "db-1.1"); // restored: the app was down when it was
    CHECK(read_file(r.root.path / "active") == "etude.service\n"); // and started again
}

TEST_CASE("go back: an app that cannot be stopped even killed is left with the old version in place and a queued start") {
    Rig r;
    r.start_trial(0);
    r.run_service("etude.service");
    write_file(r.root.path / "stuck", "");
    write_file(r.root.path / "unkillable", "");
    r.c.service_timeout_s = 2;
    PendingUpdate trial = r.state();
    CHECK(go_back(r.c, Service(r.c), trial, "1.2.0 did not start", false) == 1);
    // Nothing is restored under an app that may be running; everything else is arranged.
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous() == "1.0.0");
    CHECK(r.state().phase == UpdatePhase::RollingBack);
    CHECK(r.status().state == UpdateState::Failed);
    CHECK(r.status().reason.find("could not be stopped") != std::string::npos);
    CHECK(read_file(r.data / "history.db") == "db-1.2"); // not touched
    CHECK(r.calls().find("start --no-block etude.service") != std::string::npos); // nothing is left down
    // When the app is down at last, the next start's `count` finishes it.
    fs::remove(r.root.path / "stuck");
    fs::remove(r.root.path / "unkillable");
    fs::remove(r.root.path / "active");
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK(r.status().state == UpdateState::RolledBack);
}

TEST_CASE("watch: a record that is damaged while a version is on trial is gone back from, not run unwatched") {
    Rig r;
    r.start_trial(0);
    r.run_service("etude.service");
    write_file(r.root.path / "state" / "state", "phase=pending\nfrom=banana\n"); // damaged in the middle of the watch
    CHECK(watch_trial(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous() == "1.0.0"); // recovered from the layout: the folder older than the old version
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.status().state == UpdateState::RolledBack);
    CHECK(r.status().reason.find("could not be read") != std::string::npos);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK(read_file(r.root.path / "active") == "etude.service\n");
}

TEST_CASE("apply: with no app service running (stopped, or the crash screen instead) the update is refused, not switched in place") {
    for (const char* running : {"", "etude-crash-screen.service"}) {
        CAPTURE(running);
        Rig r;
        if (*running != '\0') {
            r.run_service(running); // the crash screen is not one of the app's units
        }
        write_file(r.prefix / "update" / "request", "version=1.3.0\n");
        CHECK(apply_update(r.c) == 1);
        CHECK(r.status().state == UpdateState::Failed);
        CHECK(r.status().reason.find("not running from its own service") != std::string::npos);
        CHECK(r.current() == "1.1.0");
        CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.3.0"));
        CHECK(r.state().phase == UpdatePhase::None);
        CHECK_FALSE(fs::exists(r.prefix / "update" / "request"));
        CHECK(r.calls().find("stop") == std::string::npos); // nothing was stopped or touched
    }
}

TEST_CASE("apply: a folder where the request should be is moved aside, so the path unit does not fire again") {
    Rig r;
    r.run_service("etude.service");
    fs::create_directories(r.prefix / "update" / "request" / "deep" / "er");
    write_file(r.prefix / "update" / "request" / "deep" / "x", "x");
    CHECK(apply_update(r.c) == 1);
    CHECK_FALSE(fs::exists(r.prefix / "update" / "request")); // the name the path unit waits for is free
    CHECK(fs::exists(r.prefix / "update" / "request.refused"));
    CHECK(r.status().state == UpdateState::Failed);
    // And again, with the first still there.
    fs::create_directories(r.prefix / "update" / "request" / "again");
    write_file(r.prefix / "update" / "request" / "again" / "y", "y");
    CHECK(apply_update(r.c) == 1);
    CHECK_FALSE(fs::exists(r.prefix / "update" / "request"));
    CHECK(fs::exists(r.prefix / "update" / "request.refused.1"));
}

TEST_CASE("apply: a damaged record of an earlier update is gone back from, and the new request is refused") {
    Rig r;
    r.start_trial(1);
    r.run_service("etude.service");
    write_file(r.root.path / "state" / "state", "\x01 junk");
    write_file(r.prefix / "update" / "request", "version=1.3.0\n");
    CHECK(apply_update(r.c) == 1);
    CHECK(r.status().state == UpdateState::Failed);
    CHECK(r.status().reason.find("damaged") != std::string::npos);
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK(read_file(r.root.path / "active") == "etude.service\n");
}

TEST_CASE("install: a current that cannot be put back keeps the record and the new version") {
    Staged s;
    fail_save_in_test(2);       // the save of Pending fails, so the links are to be undone ...
    fail_links_from_in_test(3); // ... and the calls that would undo them (the 3rd and later) fail too
    CHECK(s.install() == 1);
    fail_save_in_test(0);
    fail_links_from_in_test(0);
    CHECK(s.r.current() == "1.2.0"); // still on the new version, and it is not removed
    CHECK(fs::exists(s.r.prefix / "versions" / "1.2.0" / "bin" / "etude"));
    CHECK(s.r.state().phase == UpdatePhase::RollingBack);
    CHECK(s.r.status().state == UpdateState::Failed);
    CHECK(s.r.status().reason.find("could not be put back") != std::string::npos);
    // The app's next start finishes it.
    fs::remove(s.r.root.path / "active");
    CHECK(count_start(s.r.c) == 0);
    CHECK(s.r.current() == "1.1.0");
    CHECK(s.r.previous() == "1.0.0");
    CHECK(s.r.state().phase == UpdatePhase::None);
    CHECK_FALSE(fs::exists(s.r.prefix / "versions" / "1.2.0"));
    CHECK(read_file(s.r.data / "history.db") == "db-1.1");
}

TEST_CASE("install: a new version that says ready is confirmed, and nothing is rolled back") {
    Staged s;
    // The stand-in app says ready as soon as it is started (the helper clears a stale file first).
    std::thread app([&] {
        for (int i = 0; i < 200; ++i) {
            if (fs::exists(s.r.root.path / "calls") &&
                s.r.calls().find("start etude.service") != std::string::npos) {
                s.r.app_says_ready("1.2.0");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    CHECK(s.install() == 0);
    app.join();
    CHECK(s.r.current() == "1.2.0");
    CHECK(s.r.previous() == "1.1.0");
    CHECK(s.r.status().state == UpdateState::Ok);
    CHECK(s.r.state().phase == UpdatePhase::None);
    CHECK_FALSE(was_interrupted());
}

namespace {

// `Key=seconds` in a unit file of deploy/pi (a bare number of seconds), -1 when it is not there.
int unit_seconds(const std::string& unit, const std::string& key) {
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/deploy/pi/" + unit);
    REQUIRE(in.good());
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) == 0) {
            return std::stoi(line.substr(key.size() + 1));
        }
    }
    return -1;
}

} // namespace

TEST_CASE("install: the units' timeouts cover the longest each run can take, at the defaults and at the settings' maxima") {
    Context c;
    // The figures, worked out from the named timeouts (90 s a call and 5 more to be reaped, 20 s a look,
    // 240 s a restore).
    CHECK(worst_case_hard_stop_seconds(c) == 471);
    CHECK(worst_case_update_seconds(c) == 2401);
    CHECK(worst_case_stop_seconds(c) == 996);
    CHECK(worst_case_count_seconds(c) == 510);
    // The largest values the settings file may ask for.
    Context maxima;
    maxima.service_timeout_s = kMaxServiceTimeoutS;
    maxima.conf.service_timeout_s = kMaxServiceTimeoutS;
    maxima.conf.ready_timeout_s = kMaxReadyTimeoutS;
    maxima.conf.max_starts = kMaxStartsLimit;
    CHECK(worst_case_update_seconds(maxima) == 3201);
    // The unit files say at least that, with a margin, so the numbers cannot drift apart again.
    constexpr int kMargin = 60;
    for (const Context* each : {&c, &maxima}) {
        CAPTURE(each == &maxima);
        CHECK(unit_seconds("etude-update.service", "TimeoutStartSec") >= worst_case_update_seconds(*each) + kMargin);
        CHECK(unit_seconds("etude-update.service", "TimeoutStopSec") >= worst_case_stop_seconds(*each) + kMargin);
        CHECK(unit_seconds("etude-update-resume.service", "TimeoutStopSec") >=
              worst_case_stop_seconds(*each) + kMargin);
        for (const char* app : {"etude.service", "etude-labwc.service", "etude-crash-screen.service",
                                "etude-labwc-crash-screen.service"}) {
            CAPTURE(app);
            CHECK(unit_seconds(app, "TimeoutStartSec") >= worst_case_count_seconds(*each) + kMargin);
        }
    }
    // A slower `SERVICE_TIMEOUT` would move every figure with it: the settings may not ask for one.
    Context slow;
    slow.service_timeout_s = 300;
    CHECK(worst_case_stop_seconds(slow) > unit_seconds("etude-update.service", "TimeoutStopSec"));
}
