// SPDX-License-Identifier: GPL-3.0-or-later
// Looks over a version folder the helper itself has just unpacked, before it can become `current`:
// the second line of defence behind core read_tar, from the file system's side. Part of the root
// update helper (helper/main.cpp).
#pragma once

#include <string>

namespace etude::helper {

// "" when `root` is fit to run as an app version, else why not. Every entry must be a folder, a
// plain file or a symbolic link; belong to root; carry no set-uid, set-gid or sticky bit and no
// group or other write bit; a plain file has one hard link; a link must resolve (`realpath`) to
// something inside `root`. `bin/etude` must be an executable plain file and `share/etude` a folder.
std::string tree_problem(const std::string& root);

} // namespace etude::helper
