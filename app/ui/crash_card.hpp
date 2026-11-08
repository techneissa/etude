// SPDX-License-Identifier: GPL-3.0-or-later
// The notice after a crash, and what "Send report" opens. A small card in the note area says
// "Etude closed unexpectedly" with "Send report" and "Dismiss". "Send report" opens a sheet with the report to read,
// scrolling; its action opens the issue page where there is a browser, and where there is none
// (the Pi) shows the address as a QR code. Nothing is sent from here: the player's phone or browser
// does it. Laid out by crash_layout.hpp.
#pragma once

#include "kit.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace etude::app::ui {

// What the card shows (the app owns the strings).
struct CrashView {
    std::string_view report;       // the report, scrubbed
    std::string_view qr_url;       // the short address, for the code
    bool opens_browser = false;    // this machine has a browser; else the address is a QR code
};

enum class CrashChoice : std::uint8_t {
    None,
    Dismiss,
    OpenUrl, // the player asked for the issue page in the browser; the card is done
};

// The notice's and the crash screen's buttons at a `tok::wide_step`, one rule for both (the words'
// size `label_pt` and the height `grow` come from crash_notice_sizes / crash_screen_sizes): the width
// the words need, and the chip's options for a chip `w` wide. Step 0 is the kit's own chip.
float crash_button_w(const Kit& k, std::string_view words, float label_pt, float grow);
ChipOpts crash_button_opts(const Kit& k, float label_pt, float grow, float w);

class CrashCard {
  public:
    // Draws the card for `view` (null: none) in the note area `area` (points); `display` is the
    // screen's size and `top` where its top row ends. Returns what was tapped this frame.
    CrashChoice draw(Kit& k, const CrashView* view, const RectF& area, ImVec2 display, float top);
    bool up() const { return want_; }
    // The sheet with the report, at once (the crash screen's "Restart and send report" asked for it
    // at this start): the card is not shown first. Back then ends the notice's sheet and leaves the card.
    void open_report() { stage_ = Stage::Report; }
    // The sheet alone, over a page of its own (the crash screen's): drawn while it is open, nothing
    // when it is closed. `Back` closes it.
    CrashChoice draw_report_sheet(Kit& k, const CrashView& v, ImVec2 display, float top);
    bool report_open() const { return stage_ != Stage::Notice; }
    void close_report() { stage_ = Stage::Notice; }
    // The report's sheet covers the screen (as the settings sheet does): what lies under it waits.
    bool sheet_up() const { return want_ && stage_ != Stage::Notice; }

  private:
    enum class Stage : std::uint8_t { Notice, Report, Qr };
    Stage stage_ = Stage::Notice;
    bool want_ = false;
    float shown_ = 0.0f;
    // The report cut into lines for the body's width and type size (it changes with the window).
    std::vector<std::string> lines_;
    float lines_w_ = -1.0f;
    float lines_size_ = -1.0f;
    const char* lines_from_ = nullptr; // the report they were cut from
    std::size_t lines_len_ = 0;

    CrashChoice draw_notice(Kit& k, const RectF& area, float display_w, float alpha);
    CrashChoice draw_sheet(Kit& k, const CrashView& v, ImVec2 display, float top, float alpha);
    void draw_report_body(Kit& k, const CrashView& v, const Rect& body);
};

} // namespace etude::app::ui
