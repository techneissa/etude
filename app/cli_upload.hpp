// SPDX-License-Identifier: GPL-3.0-or-later
// The developer modes that run the phone upload server and the folder watcher with no window.
#pragma once

#include "window_options.hpp"

#include <string>

namespace etude::app {

// --upload-serve: starts the upload server on the settings folder's Songs, prints its PIN and
// addresses, and runs until it turns itself off after ten idle minutes or Ctrl-C. 0 on a clean
// end, 1 when it cannot start.
int run_upload_serve(const WindowOptions& opt);

// --watch-log DIR: prints what the folder watcher reports for DIR until Ctrl-C.
int run_watch_log(const std::string& dir);

} // namespace etude::app
