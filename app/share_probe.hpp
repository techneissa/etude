// SPDX-License-Identifier: GPL-3.0-or-later
// Whether the Pi shares its Inbox on the home network (Samba, set up by deploy/pi/setup.sh step 7).
// The app never sets it up and never talks to Samba: a worker thread reads smb.conf (and the files
// it includes) for a "[etude]" share and looks for the running smbd by its pid file, once when
// the Add songs page opens (`recheck`), so the frame thread reads an answer and waits for nothing.
// No SDL.
#pragma once

#include "ui/sources_port.hpp"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace etude::app {

struct ShareProbeOptions {
    std::filesystem::path conf = "/etc/samba/smb.conf";
    std::filesystem::path pid_file = "/run/samba/smbd.pid";
    std::filesystem::path proc = "/proc";
    // A developer flag (--fake-share): this answer, whatever the files say.
    std::optional<ui::ShareState> forced;
    std::function<void()> on_change; // the answer changed (wakes the frame loop)
};

// The answer the files give now: Shared when a "[etude]" share is defined and smbd runs.
ui::ShareState read_share_state(const ShareProbeOptions& options);

class ShareProbe {
  public:
    explicit ShareProbe(ShareProbeOptions options);
    ~ShareProbe();
    ShareProbe(const ShareProbe&) = delete;
    ShareProbe& operator=(const ShareProbe&) = delete;

    // The latest answer; Checking until the first read is done. Never waits.
    ui::ShareState state() const { return state_.load(); }
    // Reads the files again, on the worker; the first call starts it.
    void recheck();

  private:
    void run();

    ShareProbeOptions options_;
    std::atomic<ui::ShareState> state_{ui::ShareState::Checking};
    std::mutex mutex_;
    std::condition_variable cv_;
    bool wanted_ = false;
    bool quit_ = false;
    std::thread worker_;
};

} // namespace etude::app
