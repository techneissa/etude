// SPDX-License-Identifier: GPL-3.0-or-later
// `etude --check-settings [--window WxH] [--song FILE]`: opens every settings page in a hidden
// window (headless, nothing on screen), lets each row of each page say which setting it draws
// (app/ui/settings_catalogue.hpp), and fails when a setting is drawn on two pages, on a page that
// is not its home, or on none. Each page is drawn twice, once with the default keyboard and once
// with "Manual" chosen, so the rows that only show then are seen too. Exit status 1 on a problem.
// `just test` runs it after the unit tests (scripts/settings-check.sh).
#pragma once

#include <string>

namespace etude::app {

int run_settings_check(int width, int height, const std::string& song_path);

} // namespace etude::app
