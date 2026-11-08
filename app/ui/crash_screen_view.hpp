// SPDX-License-Identifier: GPL-3.0-or-later
// What the crash screen says and offers (the app owns the words, crash_screen.hpp): a title, a
// paragraph and the buttons in order. Drawn by crash_screen.cpp, laid out by crash_screen_layout.hpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace etude::app::ui {

enum class CrashButton : std::uint8_t {
    Restart,        // starts the app normally
    RestartAndSend, // starts the app normally and opens the report sheet there
    TryAgain,       // the update's data was not put back: starts the app, which tries the restore again
    SendReport,     // the report to read, then the code (on this screen)
};

struct CrashScreenView {
    std::string title;
    std::string detail;                // a paragraph; the screen wraps it
    std::vector<CrashButton> buttons;  // the first is the main one; at most two
};

// The words on a button, and the id the audit and a scripted tap name it by.
inline const char* crash_button_words(CrashButton b) {
    switch (b) {
    case CrashButton::Restart:
        return "Restart";
    case CrashButton::RestartAndSend:
        return "Restart and send report";
    case CrashButton::TryAgain:
        return "Try again";
    case CrashButton::SendReport:
        return "Send report";
    }
    return "";
}

inline const char* crash_button_id(CrashButton b) {
    switch (b) {
    case CrashButton::Restart:
        return "##crash_screen_restart";
    case CrashButton::RestartAndSend:
        return "##crash_screen_restart_send";
    case CrashButton::TryAgain:
        return "##crash_screen_try";
    case CrashButton::SendReport:
        return "##crash_screen_send";
    }
    return "##crash_screen";
}

} // namespace etude::app::ui
