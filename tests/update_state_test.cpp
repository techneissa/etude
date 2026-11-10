// SPDX-License-Identifier: GPL-3.0-or-later
// What the root helper remembers across a reboot, and the data folder it finds for the app's user.
#include "etude/data_folder.hpp"
#include "etude/update_state.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("update state: failed restores are counted, and a record without them reads as none") {
    PendingUpdate s;
    s.phase = UpdatePhase::RollingBack;
    s.from = "1.1.0";
    s.to = "1.2.0";
    CHECK(format_pending_update(s).find("restores") == std::string::npos); // nothing extra when there is none
    CHECK(parse_pending_update(format_pending_update(s)).state.restores == 0);
    s.restores = 3;
    s.data_error = "the backup\nis not complete";
    const PendingParse r = parse_pending_update(format_pending_update(s));
    REQUIRE(r.ok);
    CHECK(r.state.restores == 3);
    CHECK(r.state.data_error == "the backup is not complete");
    const std::string head = "phase=rolling-back\nfrom=1.1.0\nto=1.2.0\n";
    CHECK_FALSE(parse_pending_update(head + "restores=x\n").ok);
    CHECK_FALSE(parse_pending_update(head + "restores=-1\n").ok);
}

TEST_CASE("update state: what is written is read back") {
    PendingUpdate s;
    s.phase = UpdatePhase::Pending;
    s.from = "1.1.0";
    s.to = "1.2.0";
    s.previous = "1.0.0";
    s.unit = "etude.service";
    s.starts = 2;
    s.backed_up = true;
    const PendingParse r = parse_pending_update(format_pending_update(s));
    REQUIRE(r.ok);
    CHECK(r.state.phase == UpdatePhase::Pending);
    CHECK(r.state.from == "1.1.0");
    CHECK(r.state.to == "1.2.0");
    CHECK(r.state.previous == "1.0.0");
    CHECK(r.state.unit == "etude.service");
    CHECK(r.state.starts == 2);
    CHECK(r.state.backed_up);
    s.phase = UpdatePhase::RollingBack;
    s.previous.clear();
    s.unit.clear();
    s.reason = "1.2.0 did not\nreach its first frame";
    const PendingParse back = parse_pending_update(format_pending_update(s));
    REQUIRE(back.ok);
    CHECK(back.state.phase == UpdatePhase::RollingBack);
    CHECK(back.state.previous.empty());
    CHECK(back.state.reason == "1.2.0 did not reach its first frame");
}

TEST_CASE("update state: no update in progress is a none file") {
    // The helper always writes a phase: a file with none (empty, say) is a damaged one, not "no update".
    CHECK_FALSE(parse_pending_update("").ok);
    CHECK_FALSE(parse_pending_update("\n\n").ok);
    CHECK_FALSE(parse_pending_update("starts=1\n").ok);
    CHECK(parse_pending_update("phase=none\n").ok);
    CHECK(parse_pending_update("phase=none\n").state.phase == UpdatePhase::None);
    CHECK(parse_pending_update("phase=none\n").state.phase == UpdatePhase::None);
    CHECK(format_pending_update(PendingUpdate{}) == "phase=none\n");
}

TEST_CASE("update state: a line that does not fit is an error, not a guess") {
    const std::string head = "phase=pending\nfrom=1.0.0\nto=1.1.0\n";
    CHECK(parse_pending_update(head).ok);
    for (const char* bad : {"phase=odd\n", "phase=pending\nfrom=1.0\nto=1.1.0\n",
                            "phase=pending\nfrom=1.0.0\nto=1.1.0-rc.1\n",
                            "phase=pending\nfrom=1.0.0\nto=../x\n", "phase=pending\nfrom=1.0.0\n",
                            "phase=pending\nto=1.1.0\n", "nonsense\n", "phase=pending\nzzz=1\n"}) {
        CAPTURE(bad);
        CHECK_FALSE(parse_pending_update(bad).ok);
    }
    CHECK_FALSE(parse_pending_update(head + "starts=-1\n").ok);
    CHECK_FALSE(parse_pending_update(head + "starts=x\n").ok);
    CHECK_FALSE(parse_pending_update(head + "unit=sshd.service\n").ok);
    CHECK_FALSE(parse_pending_update(head + "backup=maybe\n").ok);
    CHECK(parse_pending_update(head + "unit=etude-labwc.service\n").ok);
    CHECK_FALSE(parse_pending_update(std::string(3000, 'x')).ok);
    CHECK(parse_pending_update(head + "starts=x\n").error == "line 4: starts is not a number");
}

TEST_CASE("data folder: the way SDL_GetPrefPath finds it, with XDG_DATA_HOME honoured") {
    CHECK(app_data_folder("/home/etude", "") == "/home/etude/.local/share/io.github.techneissa.etude");
    CHECK(app_data_folder("/home/etude", "/data/share") == "/data/share/io.github.techneissa.etude");
    CHECK(app_data_folder("/home/etude", "/data/share/") == "/data/share/io.github.techneissa.etude");
    CHECK(app_data_folder("", "/data/share") == "/data/share/io.github.techneissa.etude");
    // A relative XDG_DATA_HOME cannot be told apart from the app's own folder: no answer, not the
    // home folder's.
    CHECK(app_data_folder("/home/etude", "share").empty());
    CHECK(app_data_folder("home", "").empty());
    CHECK(app_data_folder("", "").empty());
}

TEST_CASE("data folder: a value in the service's Environment line") {
    const std::string env = "SDL_VIDEO_DRIVER=kmsdrm XDG_DATA_HOME=/data/share LANG=C\n";
    CHECK(environment_lookup(env, "XDG_DATA_HOME").kind == EnvKind::Value);
    CHECK(environment_lookup(env, "XDG_DATA_HOME").value == "/data/share");
    CHECK(environment_lookup(env, "LANG").kind == EnvKind::Unreadable); // not a path: not plain
    CHECK(environment_lookup(env, "HOME").kind == EnvKind::Unset);
    CHECK(environment_lookup(env, "XDG").kind == EnvKind::Unset);
    CHECK(environment_lookup("", "XDG_DATA_HOME").kind == EnvKind::Unset);
    CHECK(environment_lookup("\n", "XDG_DATA_HOME").kind == EnvKind::Unset);
}

TEST_CASE("data folder: a quoted, doubled or odd environment never falls back to the home folder") {
    // systemd wraps a whole assignment that holds a space in quotes.
    CHECK(environment_lookup("A=1 \"XDG_DATA_HOME=/data/share\" B=2", "XDG_DATA_HOME").value ==
          "/data/share");
    // A space in the value: not plain, so unreadable (the helper refuses to guess).
    CHECK(environment_lookup("\"XDG_DATA_HOME=/a b\" X=1", "XDG_DATA_HOME").kind ==
          EnvKind::Unreadable);
    // The last assignment wins, as in systemd.
    CHECK(environment_lookup("XDG_DATA_HOME=/one XDG_DATA_HOME=/two", "XDG_DATA_HOME").value == "/two");
    CHECK(environment_lookup("XDG_DATA_HOME=/one XDG_DATA_HOME=two", "XDG_DATA_HOME").kind ==
          EnvKind::Unreadable);
    for (const char* bad : {"XDG_DATA_HOME=\"/a", "\"XDG_DATA_HOME=/a\\n\"", "XDG_DATA_HOME='/a'",
                            "XDG_DATA_HOME=/a\\b", "XDG_DATA_HOME=", "XDG_DATA_HOME=relative",
                            "A=\"b XDG_DATA_HOME=/a"}) {
        CAPTURE(bad);
        CHECK(environment_lookup(bad, "XDG_DATA_HOME").kind == EnvKind::Unreadable);
    }
}

TEST_CASE("update state: the crash screen's unit is the app's own, for the two units the helper knows") {
    CHECK(crash_screen_unit("etude.service") == "etude-crash-screen.service");
    CHECK(crash_screen_unit("etude-labwc.service") == "etude-labwc-crash-screen.service");
    CHECK(crash_screen_unit("").empty());
    CHECK(crash_screen_unit("sshd.service").empty());
    CHECK(crash_screen_unit("etude-crash-screen.service").empty());
}
