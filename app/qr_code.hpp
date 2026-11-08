// SPDX-License-Identifier: GPL-3.0-or-later
// A QR code for a short text, as a grid of dark and light modules, for the "Add songs" page: the
// phone's camera reads the address of the upload page from it. The encoder is Nayuki's QR Code
// generator (MIT), used in qr_code.cpp alone. No SDL.
#pragma once

#include <string_view>
#include <vector>

namespace etude::app {

struct QrGrid {
    int size = 0; // modules on a side; 0 when the text did not fit
    std::vector<bool> dark;

    bool empty() const { return size == 0; }
    // True for a dark module; false for a light one and outside the grid.
    bool at(int x, int y) const {
        return x >= 0 && y >= 0 && x < size && y < size &&
               dark[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                    static_cast<std::size_t>(x)];
    }
};

// Medium error correction, the smallest version that holds `text`. An empty grid for text over
// what the biggest version holds.
QrGrid make_qr(std::string_view text);

} // namespace etude::app
