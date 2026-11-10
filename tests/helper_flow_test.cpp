// SPDX-License-Identifier: GPL-3.0-or-later
// The root helper's settling of an update from its state file, run as the current user against
// temporary folders and a stand-in systemctl: counting starts, confirming, going back (the links, the
// data, the status), finishing what a power cut interrupted, and what a request may not do. The whole
// thing as root, with a signed tarball and a real app, is scripts/update-helper-test.sh.
#include "../helper/apply.hpp"
#include "../helper/backup.hpp"
#include "../helper/handoff_files.hpp"
#include "../helper/layout.hpp"
#include "../helper/settle.hpp"
#include "../helper/state_store.hpp"
#include "helper_rig.hpp"
#include "upload_test_support.hpp"

#include "etude/update_handoff.hpp"
#include "etude/update_key.hpp"

#include <doctest/doctest.h>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <signal.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <memory>
#include <thread>

using namespace etude;
using namespace etude::helper;
using namespace upload_test;

using namespace helper_rig;

TEST_CASE("count: with no update on trial it does nothing") {
    Rig r;
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.current() == "1.1.0");
    CHECK(r.status().state == UpdateState::None);
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

TEST_CASE("count: a start without ready beyond the limit goes back, data and all, before the app starts") {
    Rig r;
    r.start_trial(2);
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous() == "1.0.0");
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(fs::exists(r.prefix / "versions" / "1.1.0"));
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK_FALSE(fs::exists(r.data / "history.db-shm"));
    const UpdateStatus s = r.status();
    CHECK(s.state == UpdateState::RolledBack);
    CHECK(s.version == "1.2.0");
    CHECK(s.from == "1.1.0");
    CHECK(s.reason.find("went back to 1.1.0") != std::string::npos);
    CHECK(r.calls().find("stop") == std::string::npos); // the service is not stopped from its own start
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
    CHECK(tree(r.prefix / "versions").find("1.0.0") == std::string::npos); // only two versions kept
    CHECK(fs::exists(r.prefix / "versions" / "1.1.0"));
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(read_file(r.data / "history.db") == "db-1.2"); // the new data stays
}

TEST_CASE("count: a ready file for another version, a link, a pipe or a stranger's does not count") {
    {
        Rig r;
        r.start_trial();
        r.app_says_ready("1.1.0");
        CHECK(count_start(r.c) == 0);
        CHECK(r.state().phase == UpdatePhase::Pending);
        CHECK(r.state().starts == 1);
    }
    {
        Rig r;
        r.start_trial();
        write_file(r.root.path / "elsewhere", "version=1.2.0\n");
        fs::create_symlink(r.root.path / "elsewhere", r.prefix / "update" / "ready");
        CHECK(count_start(r.c) == 0);
        CHECK(r.state().phase == UpdatePhase::Pending);
    }
    {
        Rig r;
        r.start_trial();
        REQUIRE(::mkfifo((r.prefix / "update" / "ready").c_str(), 0600) == 0);
        CHECK(count_start(r.c) == 0);
        CHECK(r.state().starts == 1);
    }
    {
        Rig r;
        r.start_trial();
        r.app_says_ready("1.2.0 and more text");
        CHECK(count_start(r.c) == 0);
        CHECK(r.state().phase == UpdatePhase::Pending);
    }
}

TEST_CASE("roll back: a data restore that fails leaves the going back to be finished at the next start") {
    Rig r;
    r.start_trial(2);
    fs::remove(r.data / "update-backup" / "1.1.0" / ".complete");
    // The app must not start on the new version's data: the unit fails (75) and is tried again.
    CHECK(count_start(r.c) == kRestoreUnfinished);
    CHECK(r.current() == "1.1.0"); // the old version is back at once ...
    CHECK(r.state().phase == UpdatePhase::RollingBack); // ... and the data is still to do
    CHECK(r.status().state == UpdateState::RolledBack);
    CHECK(r.status().reason.find("Couldn't put your data back") != std::string::npos);
    // The backup comes back (a disk that was full has room again): the next start finishes it.
    write_file(r.data / "update-backup" / "1.1.0" / ".complete", "version=1.1.0\n");
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
}

TEST_CASE("roll back: a restore that cannot finish is tried at every start the player asks for, with no limit, and the status says why") {
    Rig r;
    r.start_trial(2);
    fs::remove(r.data / "update-backup" / "1.1.0" / ".complete"); // the restore stays impossible
    for (int attempt = 1; attempt <= 5; ++attempt) {
        CAPTURE(attempt);
        // The app's own unit is kept from starting; the crash screen shows the reason and offers to try again.
        CHECK(count_start(r.c) == kRestoreUnfinished);
        CHECK(r.state().restores == attempt);
        CHECK(r.state().phase == UpdatePhase::RollingBack);
        CHECK_FALSE(r.state().data_error.empty());
        CHECK_FALSE(r.status().data.empty()); // the screen's reason
        CHECK(r.status().data == r.state().data_error);
    }
    CHECK(r.status().reason.find("Couldn't put your data back: ") != std::string::npos);
    // The backup comes back: the next start finishes it, and the status no longer names a reason.
    write_file(r.data / "update-backup" / "1.1.0" / ".complete", "version=1.1.0\n");
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.status().data.empty());
    CHECK(read_file(r.data / "history.db") == "db-1.1");
}

TEST_CASE("roll back: a going back that cannot be done (the old version gone, a link not written) keeps the app down and says why") {
    for (const bool gone : {true, false}) {
        CAPTURE(gone);
        Rig r;
        r.start_trial(2);
        if (gone) {
            fs::remove_all(r.prefix / "versions" / "1.1.0"); // the version to go back to is not there
        } else {
            fail_links_from_in_test(1); // no link can be written
        }
        for (int attempt = 1; attempt <= 4; ++attempt) {
            CAPTURE(attempt);
            CHECK(count_start(r.c) == kRestoreUnfinished);
            CHECK(r.state().restores == attempt);
        }
        fail_links_from_in_test(0);
        CHECK(r.status().state == UpdateState::Failed);
        CHECK_FALSE(r.status().data.empty());
    }
}

TEST_CASE("count: a going back that cannot even be tried (a layout that is refused) keeps the app down too") {
    Rig r;
    r.start_trial(2);
    std::string error;
    PendingUpdate s = r.state();
    s.phase = UpdatePhase::RollingBack;
    s.reason = "a test";
    REQUIRE(save_pending(r.c, s, error));
    ::chmod(r.prefix.c_str(), 0777);
    for (int attempt = 1; attempt <= 4; ++attempt) {
        CHECK(count_start(r.c) == kRestoreUnfinished);
        CHECK(r.state().restores == attempt);
    }
    CHECK_FALSE(r.status().data.empty());
}

TEST_CASE("count: it waits for the lock a bounded time, and a held lock does not keep the app from starting") {
    Rig r;
    std::string held_error;
    StateLock holder(r.c, "lock", false, held_error);
    REQUIRE(holder.held());
    std::string error;
    StateLock waiter(r.c, "lock", true, error, 0.2); // what count does, with its time cut short
    CHECK_FALSE(waiter.held());
    CHECK(error.find("held by another run") != std::string::npos);
}

TEST_CASE("roll back: without a backup nothing of the data is touched, and without an old previous the link goes") {
    Rig r;
    std::string error;
    fs::remove_all(r.data);
    write_file(r.data / "history.db", "db-new");
    PendingUpdate s;
    s.phase = UpdatePhase::Pending;
    s.from = "1.1.0";
    s.to = "1.2.0";
    s.previous = "";
    s.unit = "etude.service";
    s.starts = 3;
    s.backed_up = false;
    REQUIRE(save_pending(r.c, s, error));
    point_link(r.c, "current", "1.2.0", error);
    point_link(r.c, "previous", "1.1.0", error);
    CHECK(count_start(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous().empty());
    CHECK(read_file(r.data / "history.db") == "db-new");
    CHECK(r.state().phase == UpdatePhase::None);
}

TEST_CASE("roll back: the old version gone means the status says so, and nothing is switched blindly") {
    Rig r;
    r.start_trial(2);
    fs::remove_all(r.prefix / "versions" / "1.1.0");
    CHECK(count_start(r.c) == kRestoreUnfinished); // a going back that cannot finish does not start the app
    CHECK(r.current() == "1.2.0");
    CHECK(r.status().state == UpdateState::Failed);
    CHECK(r.status().reason.find("is gone") != std::string::npos);
}

TEST_CASE("an interrupted switch: current was pointed at the new version or it was not") {
    {
        Rig r;
        PendingUpdate s = r.start_trial();
        s.phase = UpdatePhase::Switching; // the cut came before the state said Pending
        std::string error;
        REQUIRE(save_pending(r.c, s, error));
        point_link(r.c, "previous", "1.0.0", error); // not yet moved
        CHECK(count_start(r.c) == 0);
        CHECK(r.state().phase == UpdatePhase::Pending);
        CHECK(r.state().starts == 1);
        CHECK(r.previous() == "1.1.0");
    }
    {
        Rig r;
        PendingUpdate s = r.start_trial();
        std::string error;
        point_link(r.c, "current", "1.1.0", error); // the switch never happened
        s.phase = UpdatePhase::Switching;
        REQUIRE(save_pending(r.c, s, error));
        CHECK(count_start(r.c) == 0);
        CHECK(r.state().phase == UpdatePhase::None);
        CHECK(r.current() == "1.1.0");
        CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0")); // the dropped update's folder is gone
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
    // The cut came after the links went back but before the data did.
    point_link(r.c, "current", "1.1.0", error);
    point_link(r.c, "previous", "1.0.0", error);
    CHECK(count_start(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK_FALSE(fs::exists(r.data / "history.db-shm"));
    CHECK(r.status().state == UpdateState::RolledBack);
}

TEST_CASE("watch: a version that says ready is confirmed") {
    Rig r;
    r.start_trial(1);
    r.run_service("etude.service");
    r.app_says_ready("1.2.0");
    CHECK(watch_trial(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.status().state == UpdateState::Ok);
    CHECK(r.calls().empty());
}

TEST_CASE("watch: out of starts, the service is stopped, the old version put back and the service started") {
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

TEST_CASE("watch: a service that will not stop is killed, and then rolled back") {
    Rig r;
    r.start_trial(2);
    r.run_service("etude.service");
    write_file(r.root.path / "stuck", "");
    CHECK(watch_trial(r.c) == 0);
    CHECK(r.calls().find("kill --kill-whom=main") != std::string::npos);
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1"); // restored while the app was down
    CHECK(r.status().reason.find("had to be killed") != std::string::npos);
}

TEST_CASE("watch: a service that cannot be stopped even killed keeps its data and is queued to start") {
    Rig r;
    r.c.service_timeout_s = 2;
    r.start_trial(2);
    r.run_service("etude.service");
    write_file(r.root.path / "stuck", "");
    write_file(r.root.path / "unkillable", "");
    CHECK(watch_trial(r.c) == 1);
    CHECK(r.current() == "1.1.0"); // the old version is back in place
    CHECK(r.state().phase == UpdatePhase::RollingBack); // and the data waits for the next start
    CHECK(read_file(r.data / "history.db") == "db-1.2"); // the app may be writing it: not restored
    CHECK(r.calls().find("start --no-block") != std::string::npos);
}

TEST_CASE("watch: with starts left the service is started again, and the next start is counted") {
    Rig r;
    r.c.conf.max_starts = 2;
    r.start_trial(1);
    r.run_service("etude.service");
    int result = -1;
    std::thread watcher([&] { result = watch_trial(r.c); });
    for (int i = 0; i < 400 && r.calls().find("restart etude.service") == std::string::npos; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    CHECK(r.calls().find("restart etude.service") != std::string::npos);
    CHECK(count_start(r.c) == 0); // the unit's ExecStartPre
    watcher.join();               // the second start did not say ready either: back to the old one
    CHECK(result == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.status().state == UpdateState::RolledBack);
}

TEST_CASE("resume: after a reboot a version still on trial is waited for, and a confirmed one settles") {
    Rig r;
    r.start_trial(1);
    r.app_says_ready("1.2.0");
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.current() == "1.2.0");
}

TEST_CASE("resume: a going back is finished only while the app is not running") {
    Rig r;
    r.start_trial(2);
    PendingUpdate s = r.state();
    s.phase = UpdatePhase::RollingBack;
    s.reason = "1.2.0 did not start";
    std::string error;
    REQUIRE(save_pending(r.c, s, error));
    r.run_service("etude.service");
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::RollingBack); // the app is up: its data is not restored under it
    CHECK(read_file(r.data / "history.db") == "db-1.2");
    fs::remove(r.root.path / "active");
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
}

TEST_CASE("resume: an interrupted switch is settled, and with nothing on trial nothing happens") {
    Rig r;
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    PendingUpdate s = r.start_trial();
    s.phase = UpdatePhase::Switching;
    std::string error;
    point_link(r.c, "current", "1.1.0", error);
    REQUIRE(save_pending(r.c, s, error));
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
}

TEST_CASE("apply: with no request there is nothing to do and no status") {
    Rig r;
    CHECK(apply_update(r.c) == 0);
    CHECK(r.status().state == UpdateState::None);
    CHECK(r.state().phase == UpdatePhase::None);
}

TEST_CASE("apply: a request that is not exactly a version line is refused, and removed whatever it was") {
    for (const char* text : {"", "hello\n", "version=1.2\n", "version=1.2.3-rc.1\n", "version=../../etc\n",
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
    {
        Rig r;
        write_file(r.root.path / "elsewhere", "version=1.3.0\n");
        fs::create_symlink(r.root.path / "elsewhere", r.prefix / "update" / "request");
        CHECK(apply_update(r.c) == 1);
        CHECK(r.status().state == UpdateState::Failed);
        CHECK_FALSE(fs::exists(r.prefix / "update" / "request"));
        CHECK(fs::exists(r.root.path / "elsewhere"));
    }
    {
        Rig r;
        REQUIRE(::mkfifo((r.prefix / "update" / "request").c_str(), 0600) == 0);
        CHECK(apply_update(r.c) == 1); // returns: it does not wait for a writer
        CHECK(r.status().state == UpdateState::Failed);
    }
}

TEST_CASE("apply: the files the app leaves are taken by name, never through a link, a pipe or a folder") {
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
            REQUIRE(::mkfifo(release.c_str(), 0600) == 0);
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

TEST_CASE("apply: a version that is already installed, or an update while one is on trial, is refused") {
    {
        Rig r;
        write_file(r.prefix / "update" / "request", "version=1.1.0\n");
        CHECK(apply_update(r.c) == 1);
        CHECK(r.status().reason.find("already installed") != std::string::npos);
    }
    {
        Rig r;
        write_file(r.prefix / "update" / "request", "version=1.0.0\n"); // the previous one
        CHECK(apply_update(r.c) == 1);
    }
    {
        Rig r;
        r.start_trial();
        write_file(r.prefix / "update" / "request", "version=1.3.0\n");
        CHECK(apply_update(r.c) == 1);
        CHECK(r.status().reason.find("still being settled") != std::string::npos);
        CHECK(r.current() == "1.2.0");
    }
}

TEST_CASE("apply: a prefix others can write to is not worked in") {
    Rig r;
    ::chmod(r.prefix.c_str(), 0777);
    write_file(r.prefix / "update" / "request", "version=1.3.0\n");
    CHECK(apply_update(r.c) == 1);
    // The request is consumed, so the path unit does not loop on it; nothing else under an unsafe
    // prefix is touched, no status written, nothing run.
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
    CHECK_FALSE(fs::exists(r.prefix / "update" / "release.tar.gz")); // taken out of the app's folder
    CHECK(r.current() == "1.1.0");
    CHECK(tree(r.root.path / "state").find("incoming") == std::string::npos);
}

TEST_CASE("count: a state file that cannot be read is not taken for no update: an unproven version goes back") {
    for (const char* damage : {"phase=pending\nfrom=banana\n", "\x01\x02 not a state", ""}) {
        CAPTURE(damage);
        Rig r;
        r.start_trial(1);
        write_file(r.root.path / "state" / "state", damage); // the record is damaged; 1.2.0 never said ready
        ::chmod((r.root.path / "state").c_str(), 0700);
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

TEST_CASE("count: a damaged state file is no reason to undo a version that said ready") {
    Rig r;
    r.start_trial(1);
    write_file(r.root.path / "state" / "state", "phase=pending\nfrom=banana\n");
    ::chmod((r.root.path / "state").c_str(), 0700);
    r.app_says_ready("1.2.0");
    CHECK(count_start(r.c) == 0);
    CHECK(r.current() == "1.2.0");
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(r.status().state == UpdateState::None); // nothing to tell the player: nothing went wrong for them
}

TEST_CASE("count: a start that cannot be counted is a failed start, and the version goes back") {
    Rig r;
    r.start_trial(0);
    fail_save_in_test(1); // the save of the start count
    CHECK(count_start(r.c) == 0);
    fail_save_in_test(0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.status().state == UpdateState::RolledBack);
    CHECK(r.status().reason.find("could not be counted") != std::string::npos);
}

TEST_CASE("resume: an update taken in whose run was cut short before the switch is reported, not left at applying") {
    Rig r;
    write_file(r.prefix / "update" / "status", "state=applying\nversion=1.2.0\nfrom=1.1.0\nreason=\ntime=1\n");
    // The status is root's own file in the app's folder: in a test the current user is both.
    CHECK(resume_boot(r.c) == 0);
    const UpdateStatus s = r.status();
    CHECK(s.state == UpdateState::Failed);
    CHECK(s.version == "1.2.0");
    CHECK(s.reason.find("cut short") != std::string::npos);
    CHECK(s.reason.find("1.1.0 is still in use") != std::string::npos);
    CHECK(r.current() == "1.1.0");
    // A status that says something else is left alone.
    write_file(r.prefix / "update" / "status", "state=ok\nversion=1.1.0\nfrom=1.0.0\nreason=\ntime=1\n");
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.status().state == UpdateState::Ok);
}

TEST_CASE("count and resume: a prefix that cannot be trusted is not worked in, and the app still starts") {
    Rig r;
    r.start_trial(5);
    ::chmod(r.prefix.c_str(), 0777);
    CHECK(count_start(r.c) == 0);
    CHECK(resume_boot(r.c) == 0);
    CHECK(r.state().starts == 5); // nothing was counted, nothing undone
    CHECK(r.current() == "1.2.0");
}

TEST_CASE("count: a going back that is recorded keeps the app from starting when the layout is refused") {
    Rig r;
    r.start_trial(2);
    std::string error;
    PendingUpdate s = r.state();
    s.phase = UpdatePhase::RollingBack;
    s.reason = "a test";
    REQUIRE(save_pending(r.c, s, error));
    ::chmod(r.prefix.c_str(), 0777); // the helper cannot work here ...
    CHECK(count_start(r.c) == kRestoreUnfinished); // ... so the app must not start on the new data
    CHECK(resume_boot(r.c) == 0);
    ::chmod(r.prefix.c_str(), 0755);
    CHECK(count_start(r.c) == 0); // with a layout it can work in, it finishes
    CHECK(r.state().phase == UpdatePhase::None);
}

TEST_CASE("count: a damaged record's `previous` is recovered from the versions that are there") {
    Rig r;
    r.start_trial(1);
    write_file(r.root.path / "state" / "state", "junk");
    CHECK(count_start(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous() == "1.0.0"); // not unlinked: the folder older than the old version
    CHECK(fs::exists(r.prefix / "versions" / "1.0.0"));
}

TEST_CASE("count: a damaged record with no `previous` link goes back to the newest older version") {
    Rig r;
    r.start_trial(1);
    fs::remove(r.prefix / "previous");
    write_file(r.root.path / "state" / "state", "junk");
    CHECK(count_start(r.c) == 0);
    CHECK(r.current() == "1.1.0"); // the newest version older than the one that never said ready
    CHECK(r.previous() == "1.0.0");
    CHECK_FALSE(fs::exists(r.prefix / "versions" / "1.2.0"));
    CHECK(r.status().state == UpdateState::RolledBack);
    CHECK(read_file(r.data / "history.db") == "db-1.1");
}

TEST_CASE("count: a damaged record with no earlier version is reported and the running version stays") {
    Rig r;
    r.start_trial(1);
    fs::remove(r.prefix / "previous");
    fs::remove_all(r.prefix / "versions" / "1.0.0");
    fs::remove_all(r.prefix / "versions" / "1.1.0");
    write_file(r.root.path / "state" / "state", "junk");
    CHECK(count_start(r.c) == 0); // the app still starts: there is nothing to put back
    CHECK(r.current() == "1.2.0");
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0" / "bin" / "etude"));
    CHECK(r.status().state == UpdateState::Failed);
    CHECK(r.status().reason.find("no earlier version") != std::string::npos);
    CHECK(r.state().phase == UpdatePhase::None);
}

TEST_CASE("count: a damaged record on a labwc install goes back with the labwc unit") {
    Rig r;
    r.start_trial(1);
    r.run_service("etude-labwc.service"); // the unit that is starting now
    write_file(r.root.path / "state" / "state", "junk");
    CHECK(count_start(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(read_file(r.data / "history.db") == "db-1.1"); // restored: the data folder was found by that unit
    CHECK(r.calls().find("show --property=Environment --value etude-labwc.service") != std::string::npos);
    CHECK(r.calls().find("show --property=Environment --value etude.service") == std::string::npos);
}

TEST_CASE("idle: the crash screen starts unless the helper is acting on the app") {
    Rig r;
    CHECK(update_idle(r.c) == 0); // nothing at all
    {
        std::string error;
        StateLock acting(r.c, "acting.lock", false, error); // a stop, a kill or a start is under way
        REQUIRE(acting.held());
        CHECK(update_idle(r.c) == 1);
    }
    CHECK(update_idle(r.c) == 0);
    // A run that only watches a version on trial (it holds run.lock, its update service is active) is not
    // acting: a crash then is a crash, and the screen shows.
    {
        std::string error;
        StateLock run(r.c, "run.lock", false, error);
        REQUIRE(run.held());
        write_file(r.root.path / "answer", "active\n");
        CHECK(update_idle(r.c) == 0);
    }
    fs::remove(r.root.path / "answer");
    // A record of an update, a going back that is not finished, one nobody can read: none of them keeps
    // the screen away (it is what shows the reason).
    r.start_trial(0);
    CHECK(update_idle(r.c) == 0);
    PendingUpdate s = r.state();
    s.phase = UpdatePhase::RollingBack;
    std::string error;
    REQUIRE(save_pending(r.c, s, error));
    CHECK(update_idle(r.c) == 0);
    write_file(r.root.path / "state" / "state", "junk");
    CHECK(update_idle(r.c) == 0);
}

TEST_CASE("idle: a failure the helper's own stop caused, shortly after, is not a crash; a crash is") {
    Rig r;
    // The helper has not acted: whatever failed, it is a crash.
    r.fail_by_stop("etude.service", false);
    CHECK(update_idle(r.c) == 0);
    {
        ActingScope acting(r.c, 1); // it acts, and stamps the start and the end
    }
    CHECK(seconds_since_acting(r.c) >= 0);
    CHECK(seconds_since_acting(r.c) < 5);
    // Just after, a stop that timed out, or a process killed with SIGKILL, is the helper's.
    r.fail_by_stop("etude.service", false);
    CHECK(update_idle(r.c) == 1);
    r.fail_by_stop("etude.service", true);
    CHECK(update_idle(r.c) == 1);
    r.fail_by_stop("etude-labwc.service", false);
    CHECK(update_idle(r.c) == 1);
    // A crash of its own (a SEGV, an exit status, the watchdog) is a crash even then.
    r.crash_service("etude.service");
    CHECK(update_idle(r.c) == 0);
    write_file(r.root.path / "show_answer", "Result=exit-code\nExecMainCode=1\nExecMainStatus=1\n");
    CHECK(update_idle(r.c) == 0);
    write_file(r.root.path / "show_answer", "Result=watchdog\nExecMainCode=2\nExecMainStatus=6\n");
    CHECK(update_idle(r.c) == 0);
    // A unit that is not failed was not killed by the helper, whatever it says.
    fs::remove(r.root.path / "failed");
    write_file(r.root.path / "show_answer", "Result=timeout\nExecMainCode=2\nExecMainStatus=9\n");
    CHECK(update_idle(r.c) == 0);
    // And long after, a stop's failure is nobody's but the app's.
    r.fail_by_stop("etude.service", false);
    write_file(r.root.path / "state" / "acting.stamp", std::to_string(static_cast<long long>(::time(nullptr)) - kActingWindowS - 5) + "\n");
    CHECK(update_idle(r.c) == 0);
    write_file(r.root.path / "state" / "acting.stamp", "junk\n");
    CHECK(update_idle(r.c) == 0);
}

TEST_CASE("watch: a unit that failed as the helper's own stop fails is not taken for a crash") {
    Rig r;
    r.start_trial(2); // out of starts: the watch goes back at its first deadline
    r.fail_by_stop("etude.service", false);
    CHECK(watch_trial(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    // It went back the ordinary way: the old app is started, and the crash screen is not.
    CHECK(r.calls().find("crash-screen") == std::string::npos);
    CHECK(read_file(r.root.path / "active") == "etude.service\n");
}

TEST_CASE("idle: its own trouble ends 0, and only the helper acting on the app ends 1") {
    Rig r;
    // A state folder that is not root's alone (here: not even a folder), so the lock cannot be opened.
    fs::remove_all(r.root.path / "state");
    write_file(r.root.path / "state", "in the way");
    CHECK(update_idle(r.c) == 0);
    fs::remove(r.root.path / "state");
    // A state folder with the wrong mode.
    fs::create_directories(r.root.path / "state");
    ::chmod((r.root.path / "state").c_str(), 0755);
    CHECK(update_idle(r.c) == 0);
    ::chmod((r.root.path / "state").c_str(), 0700);
    std::string error;
    StateLock acting(r.c, "acting.lock", false, error);
    REQUIRE(acting.held());
    CHECK(update_idle(r.c) == 1);
}

TEST_CASE("after a crash: a version on trial is gone back from at once, the data included, with no question asked") {
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

TEST_CASE("after a crash: a data restore that cannot finish is told, and the going back is left for the next start") {
    Rig r;
    r.start_trial(1);
    fs::remove(r.data / "update-backup" / "1.1.0" / ".complete");
    CHECK(settle_after_crash(r.c) == 0); // the screen shows either way
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::RollingBack);
    CHECK_FALSE(r.status().data.empty());
    // The player's Restart: count tries the restore again, and the app does not start on the new data.
    CHECK(count_start(r.c) == kRestoreUnfinished);
    write_file(r.data / "update-backup" / "1.1.0" / ".complete", "version=1.1.0\n");
    CHECK(count_start(r.c) == 0);
    CHECK(r.status().data.empty());
}

TEST_CASE("after a crash: a version that said ready is confirmed, not gone back from; no trial is left alone") {
    Rig r;
    r.start_trial(1);
    r.app_says_ready("1.2.0");
    CHECK(settle_after_crash(r.c) == 0);
    CHECK(r.current() == "1.2.0");
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.status().state == UpdateState::Ok);
    Rig none; // an app that crashed outside any update: nothing to do
    CHECK(settle_after_crash(none.c) == 0);
    CHECK(none.current() == "1.1.0");
    CHECK(none.calls().empty());
}

TEST_CASE("after a crash, with no sandbox: the links go back and the data is left for the next start") {
    Rig r;
    r.start_trial(1);
    CHECK(settle_after_crash_in_place(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::RollingBack);
    CHECK(read_file(r.data / "history.db") == "db-1.2"); // not touched here
    CHECK(count_start(r.c) == 0);                        // the restore is the next start's
    CHECK(read_file(r.data / "history.db") == "db-1.1");
}

TEST_CASE("watch: a trial whose unit crashed is gone back from at once and the crash screen is started, not the app") {
    Rig r;
    r.c.ready_timeout_s = 20; // far longer than the test: only the crash can end the watch
    r.start_trial(1);
    r.crash_service("etude.service");
    CHECK(watch_trial(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(read_file(r.data / "history.db") == "db-1.1");
    CHECK(r.status().state == UpdateState::RolledBack);
    CHECK(r.calls().find("start --no-block etude-crash-screen.service") != std::string::npos);
    CHECK(r.calls().find("start etude.service") == std::string::npos);
    CHECK(read_file(r.root.path / "active") == "etude-crash-screen.service\n"); // the screen runs, the app does not
}

TEST_CASE("go back: a start that fails is tried once more, and then reported") {
    for (const bool both : {false, true}) {
        CAPTURE(both);
        Rig r;
        r.start_trial(2);
        r.run_service("etude.service");
        write_file(r.root.path / (both ? "start_fails" : "start_fails_once"), "");
        const PendingUpdate trial = r.state();
        const int rc = go_back(r.c, Service(r.c), trial, "a test", false);
        CHECK(r.current() == "1.1.0"); // the old version is back either way
        if (both) {
            // The app does not start: the helper starts the crash screen itself, so the player is not left
            // with a black screen.
            CHECK(rc == 0);
            CHECK(read_file(r.root.path / "active") == "etude-crash-screen.service\n");
            write_file(r.root.path / "crash_screen_fails", "");
            fs::remove(r.root.path / "active");
            const int nothing = go_back(r.c, Service(r.c), trial, "a test", false);
            CHECK(nothing == 1); // neither could be started
            CHECK(r.status().reason.find("could not be started again") != std::string::npos);
            CHECK(r.status().state == UpdateState::Failed);
            CHECK_FALSE(fs::exists(r.root.path / "active"));
        } else {
            CHECK(rc == 0); // the second try worked
            CHECK(read_file(r.root.path / "active") == "etude.service\n");
            CHECK(r.status().state == UpdateState::RolledBack);
        }
    }
}

TEST_CASE("go back: a start the unfinished restore keeps from running is followed by the crash screen, started by the helper after the lock") {
    Rig r;
    r.c.conf.max_starts = 2;
    r.start_trial(2);
    r.run_service("etude.service");
    fs::remove(r.data / "update-backup" / "1.1.0" / ".complete"); // the restore cannot finish
    write_file(r.root.path / "start_fails", "");                   // so `count` ends 75 and the app's start fails
    CHECK(watch_trial(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::RollingBack);
    CHECK_FALSE(r.status().data.empty());
    const std::string calls = r.calls();
    // The app's start was tried once (another try would only count another attempt) ...
    CHECK(calls.find("start etude.service") != std::string::npos);
    CHECK(calls.find("start etude.service") == calls.rfind("start etude.service"));
    // ... and then the screen was started, without waiting.
    CHECK(calls.find("start --no-block etude-crash-screen.service") > calls.find("start etude.service"));
    CHECK(read_file(r.root.path / "active") == "etude-crash-screen.service\n");
    // The screen's own condition lets it start: the helper holds no lock and has forgotten acting.
    CHECK(update_idle(r.c) == 0);
}

TEST_CASE("watch: a stop that comes while the lock is waited for goes back, and the trial is never left unwatched") {
    struct sigaction action{};
    action.sa_handler = [](int) {};
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ::sigaction(SIGUSR1, &action, nullptr);
    Rig r;
    r.start_trial(0);
    r.run_service("etude.service");
    std::string held_error;
    auto holder = std::make_unique<StateLock>(r.c, "lock", false, held_error);
    REQUIRE(holder->held());
    const int before = lock_signals_seen_in_test();
    int result = -1;
    std::thread watcher([&] { result = watch_trial(r.c); });
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const auto nudge = [&] {
        ::pthread_kill(watcher.native_handle(), SIGUSR1);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };
    // Cut short once inside the lock wait, so it is known to be there; then told to stop.
    while (lock_signals_seen_in_test() == before && std::chrono::steady_clock::now() < give_up) {
        nudge();
    }
    raise_interrupt();
    // The watch gives up the wait and takes the stop; the going back then needs the lock itself.
    while (interrupt_pending() && std::chrono::steady_clock::now() < give_up) {
        nudge();
    }
    holder.reset();
    watcher.join();
    reset_interrupt();
    CHECK(result == 0);
    CHECK(r.current() == "1.1.0"); // not left on 1.2.0, unwatched
    CHECK(r.state().phase == UpdatePhase::RollingBack); // the data is for the next `count`
    CHECK(read_file(r.root.path / "active") == "etude.service\n"); // and the app was started again
}

TEST_CASE("state store: a blocked lock waits through a signal that is not a stop, and gives up on a stop") {
    struct sigaction action{};
    action.sa_handler = [](int) {};
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0; // no SA_RESTART: the blocked flock is cut short
    ::sigaction(SIGUSR1, &action, nullptr);
    TempDir t;
    Context c;
    c.conf.state_dir = (t.path / "state").string();
    for (const bool stop : {false, true}) {
        CAPTURE(stop);
        std::string held_error;
        auto holder = std::make_unique<StateLock>(c, "lock", false, held_error);
        REQUIRE(holder->held());
        std::atomic<bool> done{false};
        bool acquired = false;
        std::string why;
        const int before = lock_signals_seen_in_test();
        std::thread waiter([&] {
            StateLock lock(c, "lock", true, why);
            acquired = lock.held();
            done = true;
        });
        // Signals are sent until the waiter has been cut short inside `flock` (the counter says so:
        // one that lands before the wait begins does nothing and is sent again).
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        const auto signalled = [&] {
            ::pthread_kill(waiter.native_handle(), SIGUSR1);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        };
        if (stop) {
            // Wait until it is blocked (cut short once by an ordinary signal), then tell it to stop.
            while (lock_signals_seen_in_test() == before && std::chrono::steady_clock::now() < give_up) {
                signalled();
            }
            raise_interrupt();
            while (!done && std::chrono::steady_clock::now() < give_up) {
                signalled();
            }
            CHECK(done); // told to stop: it gives up
            CHECK_FALSE(acquired);
            CHECK(why.find("interrupted") != std::string::npos);
        } else {
            while (lock_signals_seen_in_test() < before + 3 && std::chrono::steady_clock::now() < give_up) {
                signalled();
            }
            CHECK(lock_signals_seen_in_test() >= before + 3); // three signals reached the blocked wait
            CHECK_FALSE(done);                                // and it went on waiting
        }
        holder.reset();
        waiter.join();
        CHECK(acquired == !stop);
        reset_interrupt();
    }
}
