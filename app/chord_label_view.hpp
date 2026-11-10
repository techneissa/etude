// SPDX-License-Identifier: GPL-3.0-or-later
// The chord label of the scene (#272): for each hand that plays a full chord, a thin line in the
// hand's colour under the keys of the hand's chord notes, with a tick at each end, and the chord's
// name centred just above it, in the strip above the note area. The name is the root large, the
// quality smaller and raised to the root's capitals, the slash bass small, with the drawn ♯ and ♭
// (accidental_glyph.hpp). A change fades the old name and line out and the new ones in, with no
// gliding; the label fades out while the section band hangs over the strip. Names step aside from
// whatever shares the strip's height (chord_label_layout.hpp). Nothing is allocated in a steady
// frame: a name is laid out and rasterised once, when its label first shows.
#pragma once

#include "mesh.hpp"
#include "text.hpp"

#include "etude/block_column.hpp"
#include "etude/chord_label.hpp"
#include "etude/chord_label_fade.hpp"
#include "etude/chord_label_layout.hpp"
#include "etude/screen_layout.hpp"
#include "etude/transpose.hpp"

#include <SDL3/SDL_render.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace etude::app {

// What a frame gives the label.
struct ChordLabelFrame {
    RectF strip;          // ScreenLayout::chord_label
    float font_px = 0.0f; // the root's size, ScreenLayout::chord_label_font_px
    float ui_unit = 1.0f; // pixels of one design point
    bool visible = true;  // false while the band hangs over the strip
    bool reduced_effects = false;
    std::int64_t now_ns = 0;
    const HandChords* chords = nullptr; // null: no song or no chords
    std::uint32_t serial = 0;           // changes when `chords` is another timeline
    double time_s = 0.0;                // the playhead, song seconds
    std::span<const RectF> avoid; // what else may stand in the strip (combo, offer, key label)
    const std::array<BlockColumn, 128>* columns = nullptr; // where each key is, window pixels
    Transposition
        transposition; // what the blocks use to show a song pitch: the label's keys follow
    std::array<SDL_FColor, 2> colours{}; // the left hand's, the right hand's
};

class ChordLabelView {
  public:
    ChordLabelView();
    ~ChordLabelView();
    ChordLabelView(const ChordLabelView&) = delete;
    ChordLabelView& operator=(const ChordLabelView&) = delete;

    void draw(SDL_Renderer* renderer, Text& text, Mesh& mesh, const ChordLabelFrame& in);

    // Where each hand's label stands, name and line together, in window pixels (index 0 the left
    // hand, 1 the right; empty when that hand shows none): the toasts keep clear of it.
    std::span<const RectF> rects() const { return rects_; }
    // Forgets what is laid out (the font or the look changed).
    void invalidate();

  private:
    // A piece of a laid-out name: a run of text, or a drawn sign.
    struct Piece {
        enum Kind { Text, Sharp, Flat } kind = Text;
        std::unique_ptr<TextSlot> slot;
        float x = 0.0f;
        float top = 0.0f; // from the top of the root's line box
        float w = 0.0f;
        float h = 0.0f; // a sign's height
    };
    struct Art {
        bool valid = false;
        int hand = 0;
        int index = -1;
        int font = 0;
        float width = 0.0f;
        std::vector<Piece> pieces;
    };
    struct Shown {
        int hand = 0;
        int index = -1;
        float alpha = 0.0f;
        const HandChordLabel* label = nullptr;
        Art* art = nullptr;
        float x0 = 0.0f; // the keys the line spans
        float x1 = 0.0f;
    };

    void draw_names(SDL_Renderer* renderer, Text& text, Mesh& mesh, const ChordLabelFrame& in,
                    const ChordLabelGeometry& g, std::span<Shown> shown,
                    std::span<const ChordNamePlace> places);
    void lay(Text& text, Art& art, const ChordNameParts& name, float font);

    std::array<ChordLabelFade, 2> fades_;
    std::array<Art, 2 * (kChordMaxLeaving + 1)> arts_;
    std::uint32_t serial_ = 0;
    bool have_serial_ = false;
    bool metrics_known_ = false;
    Text::VerticalMetrics metrics_;
    std::array<RectF, 2> rects_{};
};

} // namespace etude::app
