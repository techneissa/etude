// SPDX-License-Identifier: GPL-3.0-or-later
// The About page's end of the update check, behind ui::AboutPort. One worker thread does everything
// that can block: it reads GitHub's answer for the latest release of the public repository through
// the Fetcher (net_fetch.hpp: HTTPS, only update_fetch_hosts()), compares the version, and picks the
// release's files for this machine (update_feed.hpp). The frame thread only reads one finished view
// (a copy under a mutex, taken when it changed) and sets flags; a change wakes the frame loop.
//
// A check runs when the player asks, and once a day when "Check automatically" is on. It never
// installs by itself. Where the machine does not install updates (a Mac whose app cannot be replaced
// where it stands, or a release not built for this system) "Update" opens the release page.
//
// On a Mac "Update" downloads the zip of the app bundle, checks its signature from a hash read in
// pieces, unpacks it beside the running bundle, checks the new bundle (code signature, version),
// and hands over to a script (mac_relaunch_script.hpp) that outlives the app: the app quits, the
// script moves the old bundle aside and the new one into place, starts it, and puts the old one back
// when the new one has not reached its first frame in time.
//
// "Update" on a machine that installs: the worker downloads the tarball and its .minisig, checks the
// signature over the tarball's bytes against the key built into the app (release.json is only a
// pointer) and looks into the tarball (core read_tar refuses anything an update has no business
// doing), waits until the app is idle (no song, run or recording), leaves the tarball and the
// signature in <update_dir> and drops <update_dir>/request. It unpacks nothing: the root helper
// (helper/) copies the files out, verifies them again itself, unpacks them and does the rest. A new
// version writes <update_dir>/ready once its first frame is on screen and its history has opened and
// migrated (`startup_progress`); the helper's verdict is <update_dir>/status, which a version that
// starts after a rollback shows on the About page. No SDL.
#pragma once

#include "net_fetch.hpp"
#include "run_program.hpp"
#include "ui/about_port.hpp"

#include "etude/machine_os.hpp"
#include "etude/minisign.hpp"
#include "etude/update_feed.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace etude::app {

// A Mac that replaces its own app bundle (update_install_mac.cpp). A Mac with `blocked` set, or any
// other machine, installs nothing and opens the release page.
struct MacInstallOptions {
    std::filesystem::path bundle; // the running .app; empty: not a Mac install
    // Why this bundle cannot be replaced where it stands (core check_mac_install), for the status line.
    std::string blocked;
    // Asks the app to close; the relaunch script starts the new version once it has.
    std::function<void()> quit;
    ProgramRunner run;            // ditto, codesign and plutil; empty: run_program
    // Starts the relaunch script in a session of its own; empty: spawn_detached.
    std::function<bool(const std::vector<std::string>& argv, const std::string& log)> spawn;
    int ready_wait_s = 30;        // how long the new version has to reach its first frame
    long pid = 0;                 // this process; 0: getpid()
};

struct UpdateOptions {
    std::unique_ptr<Fetcher> fetcher;
    std::string current_version;  // app_version()
    MachineOsText machine;        // read once at start
    bool key_ready = false;       // update_key_ready(): else nothing is offered
    bool installs = false;        // this machine installs an update itself (needs `update_dir`)
    std::filesystem::path update_dir; // <prefix>/update, owned by the app's user; empty: none
    // Checks the downloaded tarball: its bytes, the .minisig's text and the trusted comment it must
    // carry. Empty: verify_release_signature, against the key built into the app.
    std::function<SignatureCheck(std::span<const std::uint8_t>, std::string_view,
                                 std::string_view)>
        verify;
    // The same check for a download that was hashed in pieces (the Mac's zip, read back from the
    // update folder). Empty: verify_release_digest.
    std::function<SignatureCheck(const Digest512&, std::string_view, std::string_view)> verify_digest;
    MacInstallOptions mac;
    // How long a request waits for the helper to pick it up before the update is called failed.
    int pickup_wait_ms = 5 * 60 * 1000;
    bool opens_browser = false;   // this machine has a browser to open a link in
    std::filesystem::path state_file; // when the last check was; empty keeps it in memory only
    std::function<std::int64_t()> clock; // unix seconds; empty: the system clock
    // The worker looks at the clock this often for the daily check, and waits this long after the
    // start before the first one.
    int poll_ms = 60'000;
    int startup_delay_ms = 20'000;
};

// The folder an update is handed over in, for the program at `exe`: <prefix>/update when `exe` is
// <prefix>/versions/<X.Y.Z>/bin/etude (a release installed by deploy/pi/setup.sh or by an update),
// else empty: a build run from its own folder, or a Mac, installs nothing.
std::filesystem::path update_dir_for(const std::filesystem::path& exe);

// Reads /etc/os-release, /etc/debian_version and the kernel's machine name (Linux), or the macOS
// version (a Mac: ID macos); elsewhere the fields stay empty, which fits no release.
MachineOsText read_machine_os();
// The release address a link opens: SDL's OpenURL in the real app (see window_startup.cpp).
using LinkOpener = std::function<bool(const std::string& url)>;

class UpdateService final : public ui::AboutPort {
  public:
    explicit UpdateService(UpdateOptions options, LinkOpener opener = {});
    ~UpdateService() override; // stops the worker (a transfer in flight is cancelled)
    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;

    // AboutPort; all for the frame thread.
    const ui::UpdateView& update() override;
    void check_now() override;
    void update_now() override;
    void cancel() override;
    void set_auto_check(bool on) override;
    void set_busy(bool busy) override;
    void startup_progress(bool frame_shown, HistoryOpen history) override;
    bool open_link(const std::string& url) override;
    const std::string& link_sheet() const override { return sheet_; }
    void set_link_sheet(std::string url) override { sheet_ = std::move(url); }

  private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
    LinkOpener opener_;
    bool opens_browser_ = false;
    ui::UpdateView shown_;
    std::uint64_t shown_version_ = 0;
    std::string sheet_;
    ReadyGate ready_gate_;    // frame thread: when the ready file is to be written, once
    bool ready_failure_logged_ = false;
    std::thread worker_;
};

} // namespace etude::app
