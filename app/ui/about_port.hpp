// SPDX-License-Identifier: GPL-3.0-or-later
// What the About page needs from the update check and from the links it shows: a read-only view of
// where the update stands, and the actions the page's buttons take. The page draws from the view on
// the frame thread; app/update_service.hpp binds the port to the real check, which runs on a thread
// of its own.
#pragma once

#include "etude/update_handoff.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace etude::app::ui {

enum class UpdatePhase : std::uint8_t {
    NotSetUp,    // this build has no update key: nothing is offered
    NotChecked,  // not asked since the start
    Checking,
    UpToDate,
    Available,   // a newer version is out (`version`, `notes`)
    Downloading, // `percent`
    Installing,
    Failed,      // `failure` says why
};

struct UpdateView {
    UpdatePhase phase = UpdatePhase::NotChecked;
    std::string version;            // the newer version, "1.2.3"
    std::vector<std::string> notes; // the first lines of its release notes
    int percent = 0;                // while Downloading, 0..100
    std::string failure;            // while Failed: the whole reason, a sentence for the player
    std::string failure_summary;    // and a short one-line form of it ("" : `failure` is short enough)
    std::string detail;             // one more line under the status ("" for none)
    std::string release_page;       // the release's page, for a machine that cannot install it
    std::int64_t last_checked_s = 0; // unix seconds of the last check that got an answer; 0: never
    // "Update" installs on this machine; else it opens the release page.
    bool installable = false;
    // The phase that Retry repeats is the check (else the install).
    bool retry_is_check = true;
    // Cancel is offered: a download, an unpack, or the wait for the idle screen is on.
    bool cancellable = false;
    // A song is playing, counting in, or a run or recording is on: an install waits for the idle
    // screen, so Update is off until it ends.
    bool busy = false;
};

class AboutPort {
  public:
    virtual ~AboutPort() = default;

    // The latest view. The reference stays valid until the next call.
    virtual const UpdateView& update() = 0;
    // "Check now" and "Retry" after a failed check. Return at once.
    virtual void check_now() = 0;
    // "Update": installs where this machine installs, else opens the release page. "Retry" after a
    // failed install is the same call.
    virtual void update_now() = 0;
    // "Cancel" while downloading.
    virtual void cancel() = 0;
    // The app reads this each frame: whether the page's "Check automatically" switch is on.
    virtual void set_auto_check(bool on) = 0;
    // The app says each frame whether it is busy (a song plays, a run or a recording is on): an
    // update starts, and the app restarts for it, only while it is not.
    virtual void set_busy(bool busy) = 0;
    // How this start stands, said each frame once the first one is on screen: whether it is, and
    // whether the history database has opened and migrated. A freshly installed version tells the
    // install helper it is ready only when both are true (core ReadyGate); a history that failed
    // means it never does, and the helper goes back to the old version.
    virtual void startup_progress(bool frame_shown, HistoryOpen history) = 0;

    // Opens `url` in the browser where the machine has one and returns true; else returns false
    // and the page shows the address and its QR code (`link_sheet`).
    virtual bool open_link(const std::string& url) = 0;
    // The address whose sheet is open ("" for none). It lives here so that a developer flag can
    // start with one open.
    virtual const std::string& link_sheet() const = 0;
    virtual void set_link_sheet(std::string url) = 0;
};

// A port with nothing behind it (the bench, the settings check): never checked, no browser.
class NullAboutPort final : public AboutPort {
  public:
    const UpdateView& update() override { return view_; }
    void check_now() override {}
    void update_now() override {}
    void cancel() override {}
    void set_auto_check(bool) override {}
    void set_busy(bool) override {}
    void startup_progress(bool, HistoryOpen) override {}
    bool open_link(const std::string& url) override {
        sheet_ = url;
        return false;
    }
    const std::string& link_sheet() const override { return sheet_; }
    void set_link_sheet(std::string url) override { sheet_ = std::move(url); }

  private:
    UpdateView view_;
    std::string sheet_;
};

} // namespace etude::app::ui
