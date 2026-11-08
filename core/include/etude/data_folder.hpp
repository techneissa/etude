// SPDX-License-Identifier: GPL-3.0-or-later
// Where the app keeps its data (history.db, settings.conf), worked out the way SDL_GetPrefPath does
// for ("", kAppDataFolderName) on Linux, so the root update helper can find it for the app's user
// without running the app: `$XDG_DATA_HOME/io.github.techneissa.etude` when XDG_DATA_HOME is set, else
// `$HOME/.local/share/io.github.techneissa.etude`. The only source of XDG_DATA_HOME is the app's service
// (its Environment); an answer that cannot be told is an error, never a quiet fall back to the
// home folder. Pure text in and out.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// The folder's name inside the per-user data folder: a fixed ID that does not change with the app's
// display name, and the macOS bundle identifier too.
inline constexpr const char* kAppDataFolderName = "io.github.techneissa.etude";

// The data folder for a user whose home is `home`, when the app's environment has `xdg_data_home`
// (empty: not set). Empty when the folder cannot be told: `home` is not absolute (and nothing else
// is given) or `xdg_data_home` is set and is not an absolute path.
std::string app_data_folder(std::string_view home, std::string_view xdg_data_home);

enum class EnvKind : std::uint8_t {
    Unset,      // the text has no such variable
    Value,      // it has, and `value` is it
    Unreadable, // the text cannot be read as `Environment=` output, or the variable's value is not a
                // plain absolute path
};

struct EnvLookup {
    EnvKind kind = EnvKind::Unset;
    std::string value;
};

// The value of `key` in the text of an `Environment=` line as `systemctl show` prints it: words
// separated by spaces; a word that has spaces or quotes in it is wrapped in double quotes with
// `\"` and `\\` escapes. Any other escape, an unclosed quote, or a `key` whose value is empty or
// holds a space, a quote or a backslash after unquoting is `Unreadable`. The last assignment wins, as
// in systemd.
EnvLookup environment_lookup(std::string_view text, std::string_view key);

} // namespace etude
