// SPDX-License-Identifier: GPL-3.0-or-later
// `etude-update-helper apply`: one update, from the request the app dropped to the new version
// running (or the old one back). Part of the root update helper (helper/main.cpp).
#pragma once

#include "context.hpp"

#include <string>

namespace etude::helper {

// What an update needs once its files are in versions/<version>: the app's update folder (open), the
// version, the one running and the one before it.
struct InstallJob {
    const Context& c;
    int update_fd;
    std::string version;
    std::string running;
    std::string previous;
};

// Stop the app, back its data up, switch the links, start the new version and watch it (see
// settle.hpp); 0 when it ended well or the old version is back, 1 when it gave up before the switch
// (the app started again, the new version removed, the status written).
int install_staged(const InstallJob& job);

// Runs one update. 0 when there was nothing to do or the update ended well (the new version is
// confirmed, or the old one is back and says so in the status); 1 when it was refused or failed
// before anything was switched, with the status written for the About page.
int apply_update(const Context& c);

} // namespace etude::helper
