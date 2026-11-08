// SPDX-License-Identifier: GPL-3.0-or-later
#include "upload_page.hpp"

#include <span>

namespace etude::app {

// Generated from app/assets/upload/index.html by cmake/EmbedFile.cmake.
std::span<const unsigned char> embedded_upload_page();

std::string_view upload_page_html() {
    const std::span<const unsigned char> bytes = embedded_upload_page();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace etude::app
