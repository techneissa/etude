// SPDX-License-Identifier: GPL-3.0-or-later
// The network folder tile's answer: read off a smb.conf (and the file it includes) and the pid file
// of smbd, on a thread of its own.
#include "../app/share_probe.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

struct Pi {
    TempDir root;
    ShareProbeOptions options;
    Pi() {
        options.conf = root.path / "etc/smb.conf";
        options.pid_file = root.path / "run/smbd.pid";
        options.proc = root.path / "proc";
        fs::create_directories(options.proc);
    }
    void conf(const std::string& text) { write_file(options.conf, text); }
    void running(bool yes) {
        write_file(options.pid_file, "4242\n");
        std::error_code ec;
        if (yes) {
            fs::create_directories(options.proc / "4242");
        } else {
            fs::remove_all(options.proc / "4242", ec);
        }
    }
};

bool settles(ShareProbe& probe, ui::ShareState want) {
    for (int i = 0; i < 400; ++i) {
        if (probe.state() == want) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // namespace

TEST_CASE("share probe: shared only with the share defined and smbd running") {
    Pi pi;
    CHECK(read_share_state(pi.options) == ui::ShareState::NotSetUp); // nothing there
    pi.conf("[global]\n");
    pi.running(true);
    CHECK(read_share_state(pi.options) == ui::ShareState::NotSetUp); // running, no share
    pi.conf("[global]\n[etude]\n path = /x\n");
    pi.running(false);
    CHECK(read_share_state(pi.options) == ui::ShareState::NotSetUp); // a share, smbd gone
    pi.running(true);
    CHECK(read_share_state(pi.options) == ui::ShareState::Shared);
}

TEST_CASE("share probe: the share may be in the file smb.conf includes") {
    Pi pi;
    write_file(pi.root.path / "etc/etude-share.conf", "[global]\n[etude]\n path = /x\n");
    pi.conf("[global]\n include = " + (pi.root.path / "etc/etude-share.conf").string() + "\n");
    pi.running(true);
    CHECK(read_share_state(pi.options) == ui::ShareState::Shared);
}

TEST_CASE("share probe: a pid file with rubbish in it is not a running smbd") {
    Pi pi;
    pi.conf("[etude]\n");
    write_file(pi.options.pid_file, "not a number");
    CHECK(read_share_state(pi.options) == ui::ShareState::NotSetUp);
}

TEST_CASE("share probe: the answer comes from the worker, once, when asked") {
    Pi pi;
    pi.conf("[etude]\n path = /x\n");
    pi.running(true);
    int changes = 0;
    pi.options.on_change = [&changes] { ++changes; };
    ShareProbe probe(pi.options);
    CHECK(probe.state() == ui::ShareState::Checking); // nothing is read until the page asks
    probe.recheck();
    REQUIRE(settles(probe, ui::ShareState::Shared));
    pi.running(false);
    CHECK(probe.state() == ui::ShareState::Shared); // not read again by itself
    probe.recheck();
    REQUIRE(settles(probe, ui::ShareState::NotSetUp));
    CHECK(changes == 2);
}

TEST_CASE("share probe: a developer's answer wins") {
    Pi pi;
    pi.options.forced = ui::ShareState::Shared;
    CHECK(read_share_state(pi.options) == ui::ShareState::Shared);
}
