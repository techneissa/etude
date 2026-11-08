// SPDX-License-Identifier: GPL-3.0-or-later
// The rig of the root helper's flow tests (tests/helper_flow_test.cpp, tests/helper_apply_test.cpp): a
// temporary prefix with versions 1.0.0 (previous), 1.1.0 (current) and 1.2.0 (staged), the app's data,
// and a stand-in systemctl, all run as the current user.
#pragma once

#include "../helper/backup.hpp"
#include "../helper/handoff_files.hpp"
#include "../helper/interrupt.hpp"
#include "../helper/layout.hpp"
#include "../helper/settle.hpp"
#include "../helper/state_store.hpp"
#include "upload_test_support.hpp"

#include "etude/update_handoff.hpp"

#include <doctest/doctest.h>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace helper_rig {

using namespace etude;
using namespace etude::helper;
using namespace upload_test;

struct Rig {
    TempDir root;
    Context c;
    fs::path prefix = root.path / "opt";
    fs::path data = root.path / "home" / ".local/share/io.github.techneissa.etude";

    Rig() {
        fs::create_directories(prefix / "update");
        ::chmod(prefix.c_str(), 0755);
        const passwd* entry = ::getpwuid(::geteuid());
        c.conf.prefix = prefix.string();
        c.ancestor_floor = fs::canonical(root.path).string(); // /tmp is world-writable
        c.conf.state_dir = (root.path / "state").string();
        c.conf.ready_timeout_s = 1;
        c.service_timeout_s = 2;
        reset_interrupt();
        install_interrupt_handlers(); // the stand-in's SIGTERM is handled, as in the helper
        c.user.name = entry->pw_name;
        c.user.uid = entry->pw_uid;
        c.user.gid = entry->pw_gid;
        c.user.home = (root.path / "home").string();
        c.conf.systemctl = (root.path / "systemctl").string();
        // The stand-in systemctl. Files in its folder change what it does: `answer` is what is-active
        // prints for every unit (exit 3), `bus_error` makes is-active fail with no word, `hang` makes
        // stop slow, `stuck` makes stop do nothing, `start_fails` makes start and restart fail
        // (`start_fails_once`: the first one only; the crash screen's start only fails with `crash_screen_fails`), and `term_on_<command>` makes that command send
        // SIGTERM (or SIGINT for `int_on_<command>`) to its parent, the helper, once. `failed` holds the name of a
        // unit that is-active calls failed (it crashed; nothing restarts it); `show_answer` is what show prints
        // (the Result, ExecMainStatus and ExecMainCode of a failed unit).
        write_file(c.conf.systemctl,
                   "#!/bin/sh\n"
                   "dir=\"" + root.path.string() + "\"\n"
                   "echo \"$*\" >>\"$dir/calls\"\n"
                   "if [ -e \"$dir/term_on_$1\" ]; then rm -f \"$dir/term_on_$1\"; kill -TERM $PPID; fi\n"
                   "if [ -e \"$dir/int_on_$1\" ]; then rm -f \"$dir/int_on_$1\"; kill -INT $PPID; fi\n"
                   "case \"$1\" in\n"
                   "is-active) if [ -e \"$dir/answer\" ]; then cat \"$dir/answer\"; exit 3;\n"
                   "  elif [ -e \"$dir/bus_error\" ]; then exit 1;\n"
                   "  elif [ \"$(cat \"$dir/failed\" 2>/dev/null)\" = \"$2\" ]; then echo failed; exit 3;\n"
                   "  elif [ \"$(cat \"$dir/active\" 2>/dev/null)\" = \"$2\" ]; then echo active;\n"
                   "  else echo inactive; exit 3; fi ;;\n"
                   "stop) [ -e \"$dir/hang\" ] && sleep 5\n"
                   "  [ -e \"$dir/stuck\" ] || rm -f \"$dir/active\"\n"
                   "  if [ -e \"$dir/term_after_stop\" ]; then rm -f \"$dir/term_after_stop\"; kill -TERM $PPID; fi ;;\n"
                   "kill) [ -e \"$dir/unkillable\" ] || rm -f \"$dir/active\" ;;\n"
                   "show) case \"$*\" in *Result*) [ -e \"$dir/show_answer\" ] && cat \"$dir/show_answer\" ;; esac ;;\n"
                   "start|restart) for last; do :; done\n"
                   "  if [ -e \"$dir/start_fails_once\" ]; then rm -f \"$dir/start_fails_once\"; exit 1; fi\n"
                   "  case \"$last\" in *crash-screen*) [ -e \"$dir/crash_screen_fails\" ] && exit 1 ;;\n"
                   "    *) [ -e \"$dir/start_fails\" ] && exit 1 ;; esac\n"
                   "  echo \"$last\" >\"$dir/active\" ;;\n"
                   "esac\n");
        ::chmod(c.conf.systemctl.c_str(), 0755);
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
    // The unit crashed: is-active says failed, and nothing is running.
    void crash_service(const std::string& unit) {
        fs::remove(root.path / "active");
        write_file(root.path / "failed", unit + "\n");
        write_file(root.path / "show_answer", "Result=signal\nExecMainCode=3\nExecMainStatus=11\n"); // a SEGV
    }
    // The unit failed as the helper's own stop fails: its stop timed out, or the process was SIGKILLed.
    void fail_by_stop(const std::string& unit, bool killed) {
        fs::remove(root.path / "active");
        write_file(root.path / "failed", unit + "\n");
        write_file(root.path / "show_answer",
                   killed ? "Result=signal\nExecMainCode=2\nExecMainStatus=9\n" : "Result=timeout\nExecMainCode=2\nExecMainStatus=9\n");
    }
    void app_says_ready(const std::string& version) {
        write_file(prefix / "update" / "ready", "version=" + version + "\n");
    }
    // 1.1.0 (the old) is current and 1.2.0 has just been switched to, with the data backed up.
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


} // namespace helper_rig
