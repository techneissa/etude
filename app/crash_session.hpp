// SPDX-License-Identifier: GPL-3.0-or-later
// A run's side of crash reports: the handler installed at the start, what the report says of the
// screen and the song kept up to date, and the notice of the last crash with the player's answer.
// No SDL (the window passes the folders and the opener).
#pragma once

#include "crash_notice.hpp"

#include "ui/crash_card.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app {

class CrashSession {
  public:
    // Scrubbing and the handler. `install_dir` is the folder the program lives in.
    void start(const std::filesystem::path& data_dir, const std::filesystem::path& install_dir);

    // The notice for the newest report not told yet. It is made once the profiles' names are known
    // (`update`), so they can be scrubbed from the report. The crash screen's "Restart and send
    // report" leaves a marker that is taken here: the report's sheet then opens as soon as the notice
    // is there (`take_sheet_request`).
    void begin_notice(const std::filesystem::path& data_dir);
    // A report is waiting to be told (or being told).
    bool report_waiting() const { return waiting_report_; }
    // True once, when the sheet was asked for at this start and the notice is made.
    bool take_sheet_request();
    // `--crash-demo STATE` for the shots: "mac" (a browser) or "pi" (none).
    void demo(std::string_view state);
    bool has_notice() const { return notice_.has_value(); }
    // What the card shows, null when there is no notice.
    const ui::CrashView* view() const { return notice_ ? &view_ : nullptr; }
    // The player's answer: Dismiss ends the notice, OpenUrl opens the issue through `open` and
    // ends it.
    void answer(ui::CrashChoice choice, const std::function<bool(const std::string&)>& open);

    // Every frame: the profiles' names the log and the report must not keep (`names_known` false
    // while the history is still loading), and the notice once they are.
    void update(bool names_known, const std::vector<std::string>& profile_names);
    // What the report will say of where the app is.
    void track(std::string_view screen, std::string_view song_title);

  private:
    etude::ScrubRules rules_;
    std::optional<CrashNotice> notice_;
    ui::CrashView view_;
    CrashNoticeStart start_;
    std::string last_names_;
    bool browser_ = false;
    bool waiting_report_ = false;
    bool sheet_requested_ = false;
};

} // namespace etude::app
