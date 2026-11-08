// SPDX-License-Identifier: GPL-3.0-or-later
// `count` for when its sandbox cannot be started (helper/count_in_place.cpp): it counts the starts of a
// version on trial and points `current` back when the trial fails, and it never looks at the app's data:
// no `systemctl show`, no restore, no backup, nothing read or written below the app user's home. Run as
// the current user against temporary folders, as the other helper flow tests do.
#include "../helper/settle.hpp"
#include "helper_rig.hpp"

using namespace helper_rig;

namespace {

// Every file below `folder` with its content: what must not change under the data folder.
std::string contents(const fs::path& folder) {
    std::string all = tree(folder);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file()) {
            all += it->path().string() + "=" + read_file(it->path()) + "\n";
        }
    }
    return all;
}

} // namespace

TEST_CASE("count in place: with no update, or one that is settled, it does nothing") {
    Rig r;
    CHECK(count_in_place(r.c) == 0);
    CHECK(r.current() == "1.1.0");
    CHECK(r.state().phase == UpdatePhase::None);
}

TEST_CASE("count in place: the starts of a version on trial are counted, and past the bound the links go back") {
    Rig r;
    r.start_trial();
    const std::string home_before = contents(r.root.path / "home");
    CHECK(count_in_place(r.c) == 0);
    CHECK(r.state().starts == 1);
    CHECK(count_in_place(r.c) == 0);
    CHECK(r.state().starts == 2);
    CHECK(r.current() == "1.2.0");
    // The third start is out of starts (MAX_STARTS is 2): the links go back, the data stays as it is.
    CHECK(count_in_place(r.c) == kRestoreUnfinished); // the app must not start on the new version's data
    CHECK(r.current() == "1.1.0");
    CHECK(r.previous() == "1.0.0");
    const PendingUpdate s = r.state();
    CHECK(s.phase == UpdatePhase::RollingBack); // the data restore is left for a sandboxed run
    CHECK(s.starts == 3);
    CHECK(s.restores == 1); // and this start, which got none, is a failed attempt to put it back
    CHECK(r.status().state == UpdateState::Failed);
    // Nothing below the app's home was read or written, and systemd was not asked where it is.
    CHECK(contents(r.root.path / "home") == home_before);
    CHECK(read_file(r.data / "history.db") == "db-1.2");
    CHECK(r.calls().find("show") == std::string::npos);
    CHECK(fs::exists(r.prefix / "versions" / "1.2.0")); // removed by the sandboxed run that restores
}

TEST_CASE("count in place: a going back whose data is never put back keeps the app down at every start, with the reason") {
    Rig r;
    r.start_trial(3);
    const std::string home_before = contents(r.root.path / "home");
    for (int attempt = 1; attempt <= 5; ++attempt) {
        CAPTURE(attempt);
        CHECK(count_in_place(r.c) == kRestoreUnfinished);
        CHECK(r.state().restores == attempt);
    }
    CHECK_FALSE(r.status().data.empty()); // the crash screen's reason
    CHECK(contents(r.root.path / "home") == home_before);
    CHECK(r.calls().find("show") == std::string::npos);
}

TEST_CASE("count in place: a version that said ready is confirmed, not counted") {
    Rig r;
    r.start_trial();
    r.app_says_ready("1.2.0");
    CHECK(count_in_place(r.c) == 0);
    CHECK(r.state().phase == UpdatePhase::None);
    CHECK(r.status().state == UpdateState::Ok);
    CHECK(r.current() == "1.2.0");
}

TEST_CASE("count in place: a record that cannot be read is left alone, and an interrupted switch is finished") {
    {
        Rig r;
        r.start_trial();
        write_file(r.root.path / "state" / "state", "phase=garbage\n");
        const std::string before = read_file(r.root.path / "state" / "state");
        CHECK(count_in_place(r.c) == 0);
        CHECK(read_file(r.root.path / "state" / "state") == before);
        CHECK(r.current() == "1.2.0");
        CHECK(r.calls().find("show") == std::string::npos);
    }
    {
        Rig r;
        PendingUpdate s = r.start_trial();
        s.phase = UpdatePhase::Switching; // the cut came after the switch
        std::string error;
        REQUIRE(save_pending(r.c, s, error));
        CHECK(count_in_place(r.c) == 0);
        CHECK(r.state().phase == UpdatePhase::Pending);
        CHECK(r.state().starts == 1);
    }
}
