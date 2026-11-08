// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_session.hpp"

#include "crash_handler.hpp"
#include "log_ring.hpp"

#include "etude/version.hpp"

namespace etude::app {

void CrashSession::start(const std::filesystem::path& data_dir, const std::filesystem::path& install_dir) {
    rules_ = scrub_rules_for(data_dir, install_dir);
    set_log_scrub(rules_);
    set_crash_build(etude::app_version(), os_description());
    install_crash_handler(data_dir);
#if defined(__APPLE__)
    browser_ = true;
#endif
}

namespace {

void fill_view(ui::CrashView& view, const CrashNotice& notice, bool browser) {
    view.report = notice.text;
    view.qr_url = notice.qr_url;
    view.opens_browser = browser;
}

} // namespace

void CrashSession::begin_notice(const std::filesystem::path& data_dir) {
    sheet_requested_ = take_send_marker(data_dir);
    waiting_report_ = sheet_requested_ || crash_notice_pending(data_dir);
    start_.defer(data_dir, sheet_requested_);
}

bool CrashSession::take_sheet_request() {
    if (!sheet_requested_ || !notice_) {
        return false;
    }
    sheet_requested_ = false;
    return true;
}

void CrashSession::demo(std::string_view state) {
    browser_ = state == "mac";
    start_ = CrashNoticeStart{}; // the made-up report is the notice: no real one is looked for
    notice_ = demo_crash_notice(browser_);
    fill_view(view_, *notice_, browser_);
}

void CrashSession::answer(ui::CrashChoice choice, const std::function<bool(const std::string&)>& open) {
    if (!notice_ || choice == ui::CrashChoice::None) {
        return;
    }
    if (choice == ui::CrashChoice::OpenUrl && open) {
        open(notice_->url);
    }
    notice_.reset();
}

void CrashSession::update(bool names_known, const std::vector<std::string>& profile_names) {
    if (!names_known) {
        return;
    }
    std::string joined;
    for (const std::string& name : profile_names) {
        joined += name;
        joined += '\n';
    }
    if (joined != last_names_) {
        last_names_ = joined;
        rules_.names = profile_names;
        set_log_scrub(rules_);
    }
    if (start_.waiting()) {
        notice_ = start_.poll(true, rules_);
        waiting_report_ = notice_.has_value();
        if (notice_) {
            fill_view(view_, *notice_, browser_);
            log_printf("etude: the last run closed unexpectedly: report %s\n", notice_->name.c_str());
        } else {
            log_printf("etude: no crash report to tell\n");
        }
    }
}

void CrashSession::track(std::string_view screen, std::string_view song_title) {
    set_crash_screen(screen);
    set_crash_song(etude::scrub_line(song_title, rules_));
}

} // namespace etude::app
