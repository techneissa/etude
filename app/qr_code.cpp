// SPDX-License-Identifier: GPL-3.0-or-later
#include "qr_code.hpp"

#include <qrcodegen.hpp>

#include <exception>
#include <string>

namespace etude::app {

QrGrid make_qr(std::string_view text) {
    QrGrid grid;
    if (text.empty()) {
        return grid;
    }
    try {
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(
            std::string(text).c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        grid.size = code.getSize();
        grid.dark.reserve(static_cast<std::size_t>(grid.size) *
                          static_cast<std::size_t>(grid.size));
        for (int y = 0; y < grid.size; ++y) {
            for (int x = 0; x < grid.size; ++x) {
                grid.dark.push_back(code.getModule(x, y));
            }
        }
    } catch (const std::exception&) {
        return QrGrid(); // too long for any version
    }
    return grid;
}

} // namespace etude::app
