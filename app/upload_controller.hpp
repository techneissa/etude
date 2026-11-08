// SPDX-License-Identifier: GPL-3.0-or-later
// The "Add songs" page's end of the phone upload server: the settings switch turns it on and off,
// and the page reads what it is doing. The server starts and stops on a thread of this class (the
// frame thread never waits for a socket); that thread also reads the server a few times a second
// and keeps one finished view for the page, and wakes the frame loop when it changes. The thread
// begins with the first switch-on, so an app that never turns the phone page on never has it. No
// SDL.
#pragma once

#include "ui/upload_port.hpp"
#include "upload_server.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace etude::app {

// The address of this machine to show a phone, from `addresses`: a home-network address, the usual
// 192.168/16 first, then 10/8, then 172.16/12. Never a link-local (169.254) or public address, and
// loopback only when `allow_loopback` (a test). Empty when there is none.
std::string shown_address(const std::vector<std::string>& addresses, bool allow_loopback);

class UploadController final : public ui::UploadPort {
  public:
    // `show_loopback`: a server bound to loopback by name (a test) shows that address;
    // the app's own never does, since a phone cannot reach it.
    explicit UploadController(UploadServerOptions options, bool show_loopback = false);
    ~UploadController() override; // stops the server and the thread

    // UploadPort. Both are for the frame thread.
    const ui::UploadView& view() override;
    void set_on(bool on) override;

  private:
    void run();
    ui::UploadView read(const UploadServer& server, bool wanted) const;
    void publish(ui::UploadView next);

    UploadServerOptions options_;
    bool show_loopback_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool wanted_ = false;
    bool quit_ = false;
    ui::UploadView latest_; // the worker's, under the mutex
    std::uint64_t latest_version_ = 0;
    std::uint64_t shown_version_ = 0;
    ui::UploadView shown_; // the frame thread's copy
    std::thread worker_;
};

} // namespace etude::app
