// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of history_store.cpp share. Not part of the app's interface.
#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_store.hpp"

#include "runs_file.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cctype>
#include <map>
#include <set>
#include <span>
#include <system_error>
#include <utility>
#include <variant>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace etude::app {

namespace fs = std::filesystem;

inline constexpr int kMaxCopies = 8; // history.db.damaged and .1 .. .7, runs.dat.bak and .1 .. .7

inline fs::path with_suffix(const fs::path& base, std::string_view suffix, int n) {
    fs::path p = base;
    p += suffix;
    if (n > 0) {
        p += "." + std::to_string(n);
    }
    return p;
}

// The first of base+suffix, base+suffix.1 ... that does not exist; empty when all are taken.
inline fs::path unused_name(const fs::path& base, std::string_view suffix) {
    std::error_code ec;
    for (int i = 0; i < kMaxCopies; ++i) {
        const fs::path candidate = with_suffix(base, suffix, i);
        if (!fs::exists(candidate, ec) && !fs::is_symlink(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

inline bool path_present(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) || fs::is_symlink(p, ec);
}

} // namespace etude::app
