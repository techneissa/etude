// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in for the update check's port with a fixed view, for `--about-demo` (the headless shots
// and the icon audit of the About page, which must look the same on every run and reach no
// network). A link never opens a browser, so its sheet shows. No SDL.
#pragma once

#include "update_status.hpp"
#include "ui/about_port.hpp"

#include <chrono>
#include <string>

namespace etude::app {

class DemoAboutPort final : public ui::AboutPort {
  public:
    // `state`: "notchecked", "checking", "uptodate", "available" (installs here), "available-page"
    // (a machine that opens the release page instead), "available-blocked" (a Mac whose app cannot be
    // replaced where it runs: the status line says why), "restarting" (a Mac install handing over to
    // its relaunch script), "downloading", "installing", "failed",
    // "failed-install" (a long reason, behind the (!) icon), "failed-wrap" (a short form that needs
    // two lines at the narrowest side by side), "waiting" (an install that waits until nothing is
    // playing) or "notset".
    explicit DemoAboutPort(const std::string& state) {
        using ui::UpdatePhase;
        // Two hours ago, so "Last checked 2 h ago" holds for any shot taken within the hour.
        const std::int64_t some_time =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count() -
            2 * 60 * 60 - 30;
        view_.release_page = "https://github.com/techneissa/etude/releases/tag/v1.1.0";
        view_.last_checked_s = some_time;
        if (state == "notset") {
            view_.phase = UpdatePhase::NotSetUp;
            view_.last_checked_s = 0;
        } else if (state == "notchecked") {
            view_.last_checked_s = 0;
        } else if (state == "checking") {
            view_.phase = UpdatePhase::Checking;
        } else if (state == "uptodate") {
            view_.phase = UpdatePhase::UpToDate;
        } else if (state == "failed" || state == "failed-install" || state == "failed-wrap") {
            view_.phase = UpdatePhase::Failed;
            // The words the page would show for these, from the same functions the service uses.
            FetchResult limited;
            limited.status = 403;
            const FailureText why =
                state == "failed"
                    ? fetch_failure_text(limited)
                : state == "failed-wrap"
                    ? FailureText{"The installer did not pick the update up within five minutes",
                                  "Installer did not pick it up in time"}
                    : helper_failure_text(
                          "1.1.0", "the new version did not reach its first frame, so version "
                                   "0.1.0 was put back");
            view_.failure = why.full;
            view_.failure_summary = why.summary;
            view_.retry_is_check = state == "failed";
        } else {
            view_.phase = UpdatePhase::Available;
            view_.version = "1.1.0";
            view_.notes = {"Learn: the circle of fifths for every key",
                           "A new About page, with updates",
                           "Faster start on the Pi",
                           "Fixes for the section editor"};
            view_.installable = state != "available-page" && state != "available-blocked";
            if (state == "available-blocked") { // a Mac that cannot replace its bundle where it stands
                view_.detail = "Can't install here: it runs from a disk image. Update opens the release page";
            }
            if (state == "downloading") {
                view_.phase = UpdatePhase::Downloading;
                view_.percent = 62;
                view_.cancellable = true;
            } else if (state == "installing") {
                view_.phase = UpdatePhase::Installing;
            } else if (state == "restarting") { // the Mac's last step before the app closes
                view_.phase = UpdatePhase::Installing;
                view_.detail = "Restarting to finish the install";
            } else if (state == "waiting") { // an install waiting for the idle screen
                view_.phase = UpdatePhase::Installing;
                view_.detail = "Waiting until nothing is playing";
                view_.cancellable = true;
            }
        }
    }

    const ui::UpdateView& update() override { return view_; }
    void check_now() override {}
    void update_now() override {}
    void cancel() override {}
    void set_auto_check(bool) override {}
    void set_busy(bool busy) override { view_.busy = busy; }
    void startup_progress(bool, HistoryOpen) override {}
    bool open_link(const std::string& url) override {
        sheet_ = url;
        return false;
    }
    const std::string& link_sheet() const override { return sheet_; }
    void set_link_sheet(std::string url) override { sheet_ = std::move(url); }

  private:
    ui::UpdateView view_;
    std::string sheet_;
};

} // namespace etude::app
