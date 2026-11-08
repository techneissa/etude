// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in for the upload server's port with a fixed view, for `--upload-demo` (the headless
// shots and the icon audit of the Add songs page, which must look the same on every run and open no
// socket). The switch turns it between the demo's state and off. No SDL.
#pragma once

#include "ui/upload_port.hpp"

#include <string>

namespace etude::app {

class DemoUploadPort final : public ui::UploadPort {
  public:
    // `state`: "off", "ready", "receiving", "done" (ready, after three songs came in) or
    // "nonetwork".
    explicit DemoUploadPort(const std::string& state) {
        shown_.name_address = "etude.local:8080";
        shown_.number_address = "192.168.1.20:8080";
        shown_.url = "http://192.168.1.20:8080/";
        shown_.pin = "4821";
        shown_.remaining_s = 9 * 60 + 42;
        if (state == "receiving") {
            shown_.receiving = true;
            shown_.file = "Moonlight Sonata.mxl";
            shown_.percent = 62;
            shown_.received = 1;
            shown_.remaining_s = 8 * 60 + 12;
        } else if (state == "done") {
            shown_.received = 3;
        }
        if (state == "nonetwork") { // ready, with no address to show
            shown_.name_address.clear();
            shown_.number_address.clear();
            shown_.url.clear();
        }
        shown_.on = shown_.ready = state != "off";
        on_view_ = shown_;
        if (!shown_.on) {
            shown_ = ui::UploadView();
        }
    }

    const ui::UploadView& view() override { return shown_; }
    void set_on(bool on) override { shown_ = on ? on_view_ : ui::UploadView(); }

  private:
    ui::UploadView shown_;
    ui::UploadView on_view_; // what the switch brings back
};

} // namespace etude::app
