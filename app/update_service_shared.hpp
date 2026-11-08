// SPDX-License-Identifier: GPL-3.0-or-later
// What the update service's worker thread shares with the frame thread's side of the service
// (update_service.cpp), and the two things the worker does: the check (update_service.cpp) and the
// install (update_install.cpp). Internal to those files.
#pragma once

#include "update_files.hpp"
#include "update_service.hpp"
#include "update_status.hpp"

#include "etude/release_info.hpp"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>

namespace etude::app {

// The release the last check found for this machine, kept for "Update".
struct UpdateOffer {
    bool valid = false;
    LatestRelease release;
    ReleaseInfo info;
    AssetPick pick;
};

struct UpdateService::Shared {
    explicit Shared(UpdateOptions o);

    UpdateOptions options;
    std::mutex mutex;
    std::condition_variable cv;
    bool quit = false;
    bool check_wanted = false;
    bool install_wanted = false;
    bool cancel_wanted = false;
    bool ready_wanted = false; // the first frame is on screen: write the ready file
    bool auto_check = false;
    bool busy = false;         // a song plays, a run or a recording is on
    ui::UpdateView latest;     // the worker's, under the mutex
    std::uint64_t serial = 1;  // bumped on each change of `latest`
    // What is stored between runs, and what the daily check goes by. Only the worker changes them
    // (the constructor, before the worker starts, loads them).
    std::int64_t last_ok_s = 0;    // the last check that got an answer
    std::int64_t last_try_s = 0;   // the last attempt, an automatic or a manual one
    bool last_was_current = false;   // that answer was "up to date" for `options.current_version`
    bool last_was_available = false; // or "an update is out" (what it was is not stored)
    UpdateOffer found;             // under the mutex

    // ---- update_service.cpp
    void publish(ui::UpdateView next);
    ui::UpdateView current();
    bool stopping();
    void save_state() const;
    void load_state();
    void run_check();
    enum class Offer : std::uint8_t { Done, Cancelled, Failed };
    Offer attach_offer(const LatestRelease& release, const FetchProgress& wanted,
                       ui::UpdateView& next, FailureText& why);
    void work();

    // ---- update_install.cpp
    // Reads the helper's verdict on the last update, when one ended badly: the words for the page.
    FailureText last_update_failure() const;
    void write_ready() const;
    // Downloads, checks and hands over the offered release; every way it ends is a
    // published view.
    void run_install();

  private:
    // The Mac's: the same download, then the steps of update_install_mac.cpp.
    void run_install_mac();
    bool check_mac_zip(const UpdateOffer& offer, const std::string& zip, const std::string& signature,
                       ui::UpdateView& view, FailureText& why);
    bool prepare_mac_bundle(const UpdateOffer& offer, ui::UpdateView& view, std::filesystem::path& staged,
                            FailureText& why);
    bool start_mac_relaunch(const UpdateOffer& offer, ui::UpdateView& view,
                            const std::filesystem::path& staged, FailureText& why);
    // True when the player cancelled or the service is quitting.
    bool stop_requested();
    void fail_install(const ui::UpdateView& base, FailureText why);
    bool fetch_release(const UpdateOffer& offer, ui::UpdateView& view, std::string& tarball,
                       std::string& signature, bool& cancelled, FailureText& why);
    bool leave_release(const UpdateOffer& offer, const std::string& tarball,
                       const std::string& signature, FailureText& why);
    bool wait_until_idle(ui::UpdateView& view);
    bool hand_over(const UpdateOffer& offer, ui::UpdateView& view);
};

} // namespace etude::app
