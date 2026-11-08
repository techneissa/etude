// SPDX-License-Identifier: GPL-3.0-or-later
// What the "Add songs" settings page needs from the phone upload server: a read-only view of it
// (the switch, the address, the PIN, the time left, the file coming in) and one action, the switch.
// The page draws from the view on the frame thread; app/upload_controller.hpp binds the port to the
// real server, which starts and stops on a thread of its own.
#pragma once

#include <string>

namespace etude::app::ui {

struct UploadView {
    // The switch: the phone page is wanted. It follows a tap at once; `ready` follows the server.
    bool on = false;
    // The server is listening (and the address, PIN and time left below are real).
    bool ready = false;
    // A file is coming in (`file` and `percent`).
    bool receiving = false;
    // The address people type, "etude.local:8080", and the same by number, "192.168.1.20:8080"
    // (both empty while the Pi has no home-network address: it is "not on a network yet", and the
    // page shows no code).
    std::string name_address;
    std::string number_address;
    // The address the QR code carries: an http:// address a phone can open.
    std::string url;
    std::string pin;
    // Seconds until the server turns itself off for lack of use.
    int remaining_s = 0;
    // The file coming in, its percent (0..100), and how many have arrived since the switch went on.
    std::string file;
    int percent = 0;
    int received = 0;
    // Why it is off or stopped, one line for the page ("" when there is nothing to say).
    std::string note;
};

class UploadPort {
  public:
    virtual ~UploadPort() = default;

    // The latest view. The reference stays valid until the next call.
    virtual const UploadView& view() = 0;
    // The switch. Returns at once; the server starts or stops on its own thread.
    virtual void set_on(bool on) = 0;
};

// A port with no server behind it, for a UI that runs without one (the bench, the settings check):
// the switch turns and nothing listens.
class NullUploadPort final : public UploadPort {
  public:
    const UploadView& view() override { return view_; }
    void set_on(bool on) override { view_.on = on; }

  private:
    UploadView view_;
};

} // namespace etude::app::ui
