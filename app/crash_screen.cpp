// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_screen.hpp"

#include "crash_notice.hpp"

#include "etude/update_handoff.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace etude::app {

bool is_crash_screen_option(std::string_view argument) {
    return argument == "--crash-screen" || argument == "--safe-mode";
}

ui::CrashScreenView crash_screen_view(const CrashScreenFacts& facts) {
    ui::CrashScreenView view;
    if (!facts.data_error.empty()) {
        view.title = "Etude did not start";
        view.detail = "Couldn't put your data back: " + facts.data_error +
                      ". Etude does not start on data that was not put back.";
        view.buttons = {ui::CrashButton::TryAgain, ui::CrashButton::SendReport};
        return view;
    }
    view.title = "Etude closed unexpectedly";
    view.detail = "A report is saved on this device. Nothing is sent unless you send it.";
    view.buttons = {ui::CrashButton::Restart, ui::CrashButton::RestartAndSend};
    return view;
}

CrashLeave crash_leave_for(ui::CrashButton button) {
    switch (button) {
    case ui::CrashButton::Restart:
    case ui::CrashButton::TryAgain:
        return CrashLeave::Restart;
    case ui::CrashButton::RestartAndSend:
        return CrashLeave::RestartAndSend;
    case ui::CrashButton::SendReport:
        return CrashLeave::ShowReport;
    }
    return CrashLeave::Stay;
}

std::optional<ui::CrashButton> crash_button_named(std::string_view name) {
    if (name == "restart") {
        return ui::CrashButton::Restart;
    }
    if (name == "restart-send") {
        return ui::CrashButton::RestartAndSend;
    }
    if (name == "try-again") {
        return ui::CrashButton::TryAgain;
    }
    if (name == "send-report") {
        return ui::CrashButton::SendReport;
    }
    return std::nullopt;
}

bool leaves_screen(CrashLeave leave) {
    return leave == CrashLeave::Restart || leave == CrashLeave::RestartAndSend;
}

CrashScreenFacts crash_screen_facts_from_status(std::string_view status_text) {
    CrashScreenFacts facts;
    facts.data_error = etude::parse_update_status(status_text).data;
    return facts;
}

CrashScreenFacts read_crash_screen_facts(const std::filesystem::path& update_dir) {
    if (update_dir.empty()) {
        return {};
    }
    const std::string path = (update_dir / std::string(etude::kUpdateStatusFile)).string();
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    std::string text;
    struct stat st{};
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= 4096) {
        char buffer[4096];
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n > 0) {
            text.assign(buffer, static_cast<std::size_t>(n));
        }
    }
    ::close(fd);
    return crash_screen_facts_from_status(text);
}

void leave_for_next_start(const std::filesystem::path& data_dir, CrashLeave leave) {
    if (leave == CrashLeave::Restart) {
        mark_crash_reports_seen(data_dir);
    } else if (leave == CrashLeave::RestartAndSend) {
        leave_send_marker(data_dir);
    }
}

} // namespace etude::app
