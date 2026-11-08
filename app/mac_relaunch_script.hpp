// SPDX-License-Identifier: GPL-3.0-or-later
// The script that finishes a Mac update after the app has quit: it moves the old bundle aside and
// the new one into place, starts it, and puts the old one back if the new one does not reach its
// first frame in time. The text is here so that the app can write it to the update folder; the
// arguments and the steps are described at its top. No SDL.
#pragma once

#include <string>
#include <string_view>

namespace etude::app {

std::string_view mac_relaunch_script();

} // namespace etude::app
