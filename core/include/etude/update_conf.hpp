// SPDX-License-Identifier: GPL-3.0-or-later
// The settings of the root update helper (helper/), read from /etc/etude-update.conf, which
// deploy/pi/setup.sh writes. The file is data, never shell: it is not sourced, and this parser takes
// only what is listed here. Pure text in, a checked struct out; the helper itself refuses a file that
// is not root's or that others can write.
//
//   PREFIX=/opt/etude            where versions/, current and update/ are (an absolute path)
//   APP_USER=etude               the user the app runs as; the data backup runs as this user
//   STATE_DIR=/var/lib/etude-update   the helper's own folder, root's alone
//   SYSTEMCTL=/usr/bin/systemctl   the program that stops and starts the app's service
//   SERVICE_TIMEOUT=90             seconds one systemctl call may take (stop, start, state)
//   READY_TIMEOUT=30               seconds a start has to reach its first frame
//   MAX_STARTS=2                   starts without `ready` after which the update is rolled back
//
// A line is `KEY=value` or `KEY="value"`; blank lines and lines starting with `#` are ignored. A key
// twice, a key not listed, a value with a character outside the plain set or a path that is not
// absolute and clean makes the whole file refused.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// What the settings file may ask for. The units' time limits (deploy/pi) are worked out for these
// maxima (helper/context.hpp), so a file that stays inside them never has systemd end a run in the
// middle; MAX_STARTS stays below the app units' StartLimitBurst (5), so the trial's own bound is
// always reached before the crash limit.
inline constexpr int kMaxServiceTimeoutS = 90;
inline constexpr int kMaxReadyTimeoutS = 120;
inline constexpr int kMaxStartsLimit = 4;

struct UpdateConf {
    std::string prefix = "/opt/etude";
    std::string app_user = "etude";
    std::string state_dir = "/var/lib/etude-update";
    std::string systemctl = "/usr/bin/systemctl";
    int service_timeout_s = 90;
    int ready_timeout_s = 30;
    int max_starts = 2;
};

enum class ConfError : std::uint8_t {
    None,
    TooLarge,   // over kMaxConfBytes
    BadLine,    // not blank, a comment or KEY=value
    UnknownKey, // a key this file does not have
    Duplicate,  // a key twice
    BadValue,   // a value that does not fit its key
};

struct ConfResult {
    UpdateConf conf;
    ConfError error = ConfError::None;
    int line = 0;    // 1-based, for an error
    std::string key; // the key at fault, when there is one

    bool ok() const { return error == ConfError::None; }
};

inline constexpr std::size_t kMaxConfBytes = 4096;

ConfResult parse_update_conf(std::string_view text);

// "line 3: PREFIX is not an absolute path" and the like, for the log and the status.
std::string conf_error_text(const ConfResult& result);

// A plain absolute path: at most 200 bytes of A-Za-z0-9 . _ - + @ : , and `/`, no empty, `.` or `..`
// part and no trailing `/` (but "/" itself).
bool plain_absolute_path(std::string_view path);

// A user name as useradd takes it: [a-z_][a-z0-9_-]{0,31}.
bool plain_user_name(std::string_view name);

} // namespace etude
