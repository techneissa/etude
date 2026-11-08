// SPDX-License-Identifier: GPL-3.0-or-later
// The engraver behind sheet music: Verovio lays a MusicXML score out in systems and writes each as
// SVG, lunasvg draws the SVG into pictures. This header and its source are the only code that sees
// either library; what comes out is core's plain SheetLayout and RGBA pixels. Not thread-safe:
// one thread (the sheet worker) owns an engraver.
#pragma once

#include "etude/sheet_map.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace etude::app {

struct EngraveOptions {
    float width_px = 1000.0f;     // a system's width in pixels
    float staff_space_px = 10.0f; // between two lines of a staff, in pixels
    // Verovio's horizontal spacing (spacingLinear, spacingNonLinear); 0 is its own (0.25, 0.6).
    // Wider spacing, fewer bars per system: room across for note names.
    float spacing_linear = 0.0f;
    float spacing_non_linear = 0.0f;
};

// A note head's colour ring: its width at an engraved staff space, never under a pixel and a half.
float sheet_ring_px(float staff_space_px);

// One system drawn three times, each `width` x `height` RGBA8 pixels (R, G, B, A in memory order,
// alpha not premultiplied): the page without its note marks (`base`); the note marks alone in white
// (`notes`), which the view tints mark by mark; and in white a ring round each note head
// (`rings`, sheet_ring_px wide), round its outer edge only, so a half or whole note's hollow stays
// as engraved, which the view tints in the note's colour.
struct SystemPicture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> base;
    std::vector<std::uint8_t> notes;
    std::vector<std::uint8_t> rings;
};

class SheetEngraver {
  public:
    // `resource_dir` holds Verovio's fonts (the app's verovio-data folder).
    explicit SheetEngraver(std::string resource_dir);
    ~SheetEngraver();
    SheetEngraver(const SheetEngraver&) = delete;
    SheetEngraver& operator=(const SheetEngraver&) = delete;

    // Lays the score out. False, with an English `error`, when it cannot be read.
    bool engrave(const std::string& musicxml, const EngraveOptions& options, SheetLayout& out,
                 std::string& error);
    // Draws a system of the last layout. False when there is no such system.
    bool draw_system(int system, SystemPicture& out);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace etude::app
