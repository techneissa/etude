// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_handoff.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("handoff: the request and the ready file are one version line") {
    CHECK(format_update_request("1.2.3") == "version=1.2.3\n");
    CHECK(format_update_ready("0.10.0") == "version=0.10.0\n");
    for (const char* bad : {"", "1.2", "v1.2.3", "1.2.3-rc.1", "1.2.3+b", "01.2.3", "1.2.3\n", "../1.2.3"}) {
        CAPTURE(bad);
        CHECK(format_update_request(bad).empty());
    }
}

TEST_CASE("handoff: the helper's status is read, line by line") {
    const UpdateStatus s = parse_update_status(
        "state=rolled-back\nversion=1.2.0\nfrom=1.1.0\nreason=1.2.0 did not start\ntime=1800000000\n");
    CHECK(s.state == UpdateState::RolledBack);
    CHECK(s.version == "1.2.0");
    CHECK(s.from == "1.1.0");
    CHECK(s.reason == "1.2.0 did not start");
    CHECK(s.time_s == 1'800'000'000);
    CHECK(parse_update_status("state=ok\n").state == UpdateState::Ok);
    CHECK(parse_update_status("state=applying\n").state == UpdateState::Applying);
    CHECK(parse_update_status("state=failed\r\n").state == UpdateState::Failed);
    CHECK(parse_update_status("state=odd\n").state == UpdateState::None);
}

TEST_CASE("handoff: a bad status is read as little as it can be") {
    const UpdateStatus s = parse_update_status(
        "nonsense\nversion=1.2\nfrom=../x\nfuture=1\nreason=a\tb\n=x\n");
    CHECK(s.state == UpdateState::None);
    CHECK(s.version.empty());
    CHECK(s.from.empty());
    CHECK(s.reason == "a b");
    CHECK(parse_update_status("").state == UpdateState::None);
    const UpdateStatus long_reason = parse_update_status("reason=" + std::string(1000, 'x') + "\n");
    CHECK(long_reason.reason.size() == 300);
}

TEST_CASE("handoff: an update starts only from the idle screen") {
    CHECK(update_may_start({}));
    CHECK_FALSE(update_may_start({.playing = true}));
    CHECK_FALSE(update_may_start({.counting_in = true}));
    CHECK_FALSE(update_may_start({.run_in_progress = true}));
    CHECK_FALSE(update_may_start({.recording = true}));
}

TEST_CASE("handoff: ready is written once, when the frame is shown and the history has opened") {
    ReadyGate gate;
    CHECK_FALSE(gate.update(true, HistoryOpen::Pending));  // the frame alone is not enough
    CHECK_FALSE(gate.update(false, HistoryOpen::Opened));  // nor the history alone
    CHECK_FALSE(gate.settled());
    CHECK(gate.update(true, HistoryOpen::Opened));
    CHECK(gate.settled());
    CHECK_FALSE(gate.update(true, HistoryOpen::Opened));   // once
    CHECK_FALSE(gate.failed());
}

TEST_CASE("handoff: a history that failed to open or migrate means ready is never written") {
    ReadyGate gate;
    CHECK_FALSE(gate.update(true, HistoryOpen::Failed));
    CHECK(gate.failed());
    CHECK(gate.settled());
    CHECK_FALSE(gate.update(true, HistoryOpen::Opened)); // not even if it then reads as opened
    ReadyGate early;
    CHECK_FALSE(early.update(false, HistoryOpen::Failed)); // failed before the first frame
    CHECK_FALSE(early.update(true, HistoryOpen::Opened));
    CHECK(early.failed());
}
