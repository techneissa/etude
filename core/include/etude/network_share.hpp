// SPDX-License-Identifier: GPL-3.0-or-later
// The network folder on the Pi: Samba shares the Inbox as "etude" (deploy/pi/setup.sh, step 7).
// The app only reads what the Pi says about it, so these are the words and the parsing of a
// smb.conf, with no file in them (the app reads the files, off the frame thread).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace etude {

inline constexpr const char* kShareName = "etude";
inline constexpr const char* kShareHost = "etude.local";
// The one command that sets the share up, run in the etude folder on the Pi.
inline constexpr const char* kShareSetupCommand = "sudo deploy/pi/setup.sh --share-only";

// Whether a smb.conf's text defines the share `name`: a "[name]" section (any case) that is not
// switched off with "available = no".
bool smb_conf_has_share(std::string_view conf, std::string_view name);

// The files a smb.conf includes ("include = /etc/samba/x.conf"); lines with a % in them (per-user
// files) are left out.
std::vector<std::string> smb_conf_includes(std::string_view conf);

// How a computer opens the share: "\\etude.local\etude" (Windows) and
// "smb://etude.local/etude" (Mac and Linux).
std::string share_windows_address();
std::string share_smb_address();

} // namespace etude
