// SPDX-License-Identifier: GPL-3.0-or-later
// The page the server answers "/" with: app/assets/upload/index.html, compiled in as one string
// (cmake/EmbedFile.cmake), so the Pi needs no outside files and the page loads no script, style or
// font from anywhere else.
#pragma once

#include <string_view>

namespace etude::app {

std::string_view upload_page_html();

} // namespace etude::app
