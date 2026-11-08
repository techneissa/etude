// SPDX-License-Identifier: GPL-3.0-or-later
// Measuring where an icon's ink sits in its button, from pixels (no SDL, no ImGui). The icon audit
// (`etude --icon-audit`, app/ui/icon_audit.*) renders each frame twice, once with the icons and
// once without; the pixels that differ are the icons' ink. This file turns those two images into an
// ink box with sub-pixel edges and judges its centre against the button's.
//
// The rule: the ink box's centre is within `kCentreTolerancePx` of the button's centre on both
// axes. A play triangle is the exception: its ink box sits right of centre by `kPlayOpticalShare`
// of its width, which is what makes a triangle look centred.
//
// Text is placed the same way (see "Labels" below): by its ink, never by its font's line box. The
// line box is ascent plus descent, so it holds room for descenders and the font's own spacing, and
// a word without descenders ("All", a number) sits high in it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace etude::app::ui {

inline constexpr float kCentreTolerancePx = 0.5f;
// Text is placed on whole pixels, so a button centred on a half pixel leaves its icon exactly half
// a pixel off at best; the edges read from antialiasing are good to a few hundredths. A centre
// within the tolerance plus this much counts as within it.
inline constexpr float kMeasureSlackPx = 0.05f;
inline constexpr float kPlayOpticalShare = 0.08f; // of the triangle's width, to the right
// A label's ink centre is within this of its slot's centre on each axis (the letters' own shapes
// and a whole pixel of placement leave a little more room than an icon needs).
inline constexpr float kLabelTolerancePx = 1.0f;

// An 8-bit RGBA (or BGRA: only differences count) image: `stride` bytes a row, 4 bytes a pixel.
struct PixelView {
    const std::uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

struct PixelRect {
    int x0 = 0; // inclusive
    int y0 = 0;
    int x1 = 0; // exclusive
    int y1 = 0;
};

// The ink found between two renders of the same rect. Edges are in pixels with the fraction of a
// pixel the ink covers taken from the antialiasing, so a stroke from x = 10.25 to 17.75 reads
// 10.25 and 17.75.
struct InkBox {
    bool found = false;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float width() const { return right - left; }
    float height() const { return bottom - top; }
    float cx() const { return (left + right) * 0.5f; }
    float cy() const { return (top + bottom) * 0.5f; }
};

// The ink box of the pixels inside `area` (clipped to the image) that differ between `with` and
// `without`. A difference under `noise` (0..1 of full scale) is ignored; the strongest difference
// in the area counts as full coverage. `found` is false when nothing differs, or the strongest
// difference is itself under the noise.
InkBox measure_ink(const PixelView& with, const PixelView& without, PixelRect area,
                   float noise = 0.04f);

// The same for a glyph bitmap: `alpha` is the first byte of the alpha channel (`bytes_per_pixel` 1
// for an alpha-only atlas, 4 for RGBA at its alpha byte), the coverage is the alpha itself, and
// the box is in the bitmap's pixels.
InkBox measure_alpha(const std::uint8_t* alpha, int stride, int bytes_per_pixel, int width,
                     int height, PixelRect area, float noise = 0.02f);

enum class InkKind : std::uint8_t {
    Centred,      // the ink box is centred on the button
    PlayTriangle, // right of centre by kPlayOpticalShare of its width
    Target,       // not an icon button: only something the audit can tap
};

// How far the ink centre is from where the rule wants it, in pixels (positive: right, down), and
// whether both axes are within the tolerance. `check_y` false leaves the vertical axis out (an
// icon above a caption in a rail).
struct Offset {
    float dx = 0.0f;
    float dy = 0.0f;
    bool ok = false;
};
Offset judge_ink(const InkBox& ink, float button_cx, float button_cy, InkKind kind,
                 bool check_y = true);

// --- Labels --------------------------------------------------------------------------------------
// Where a label's pen goes so its ink lies where the layout wants it. Vertical: the middle of the
// cap-height band (the capitals' top to the baseline), so a word with no descenders sits in the
// middle of its control and one with them does not move. Horizontal: the visible letters (the
// advance less the side bearings of the first and last letter).
enum class LabelAlign : std::uint8_t {
    Left,   // the pen starts at the slot's left edge (rows line their text up there)
    Centre, // the ink is centred in the slot
    Right,  // the ink's right edge is at the slot's right edge
};

// A label's ink from its pen position, in the pixels (or points) it is drawn at.
struct LabelMetrics {
    float cap_top = 0.0f;   // the capitals' top, below the pen's y
    float baseline = 0.0f;  // the baseline, below the pen's y
    float ink_left = 0.0f;  // the first letter's ink, right of the pen's x
    float ink_right = 0.0f; // the last letter's ink, right of the pen's x
    // False when the font has no glyph for the first or the last letter: that side is then the
    // advance's edge, not the ink's. The answer is as stable as the font, so it is kept like any.
    bool sides_known = true;
};
struct LabelPen {
    float x = 0.0f;
    float y = 0.0f;
};
// The pen position for a label of metrics `m` in the slot `x0`..`x1`, its cap band centred on `cy`.
LabelPen place_label(const LabelMetrics& m, float x0, float x1, float cy, LabelAlign align);

// --- The labels' metrics store -------------------------------------------------------------------
// What `label_metrics` (ui/kit_text.cpp) keeps, so a screen of labels that did not change measures
// none of them again. The key is a 64-bit hash of the font, the size and the words as displayed
// (`screen_text`: F# is drawn F♯, a different width); the store is direct-mapped, a slot holds the
// one key that last wrote it, and a lookup answers only for that key, so a key that shares a slot
// with another never reads its width. The font is an address: a store outlives its fonts only until
// `clear`, which the kit calls whenever it builds or drops the fonts.
std::uint64_t label_metrics_key(const void* font, float size, std::string_view shown);

class LabelMetricsStore {
  public:
    // The metrics kept for `key`, else nullptr.
    const LabelMetrics* find(std::uint64_t key) const {
        const Slot& s = slots_[key % kSlots];
        return s.key == key ? &s.metrics : nullptr;
    }
    void put(std::uint64_t key, const LabelMetrics& metrics) { slots_[key % kSlots] = {key, metrics}; }
    void clear() { slots_.fill({}); }
    std::size_t used() const {
        std::size_t n = 0;
        for (const Slot& s : slots_) {
            n += s.key != 0 ? 1 : 0;
        }
        return n;
    }
    static constexpr std::size_t kSlots = 1024;

  private:
    struct Slot {
        std::uint64_t key = 0; // 0: empty (`label_metrics_key` is never 0)
        LabelMetrics metrics;
    };
    std::array<Slot, kSlots> slots_{};
};

// A label's ink from the pixels that differ between two renders of its area. `box` is all of it
// (the letters' sides give the horizontal centre); `baseline` is where the letters stand, read
// from the pixels: the foot of the ink, or, for a word with descenders (`descenders`, from its
// letters: g j p q y and the like), the foot of the lowest row that holds a real share
// (`kBaselineShare`) of the strongest row, so a descender's stem under the letters does not count
// (the bowls under "Arpeggio", three of its eight letters, reach a third of the strongest row).
// A word that says it has none but does reads too low and fails: the flag never hides an error.
inline constexpr float kBaselineShare = 0.36f;
struct LabelInk {
    bool found = false;
    InkBox box;
    float baseline = 0.0f;
};
LabelInk measure_label(const PixelView& with, const PixelView& without, PixelRect area,
                       bool descenders, float noise = 0.04f);

// How far a label's measured ink is from where its slot wants it, in pixels (positive: right,
// down), and whether it is within `kLabelTolerancePx` on each axis. Vertical: the capitals' band,
// `cap_h` tall and standing on the measured baseline, is centred on `slot_cy`; `check_y` false
// leaves that axis out (a dash or a dot has no baseline to stand on). Horizontal: the ink box is
// centred on `slot_cx`; `check_x` false leaves that axis out (a label that starts at its slot's
// left edge).
struct LabelOffset {
    float dx = 0.0f;
    float dy = 0.0f;
    bool ok = false;
};
LabelOffset judge_label(const LabelInk& ink, float cap_h, float slot_cx, float slot_cy,
                        bool check_x, bool check_y = true);

// Whether a label stays on the control it is drawn on: its ink's sides (`ink_left`, `ink_right`,
// read from the pixels) lie between the sides of the face that holds it (`face_left`,
// `face_right`), within `kLabelTolerancePx`, all in pixels. The face is what a person sees: a label
// as wide as its slot inside a face with padding passes, a label that runs past the face's edge
// (centred or left, however well its middle sits on the slot's) fails. The slot is not the
// measure: the kit sizes it from the text's own advance, a pixel or two off the ink.
bool label_inside(float ink_left, float ink_right, float face_left, float face_right);

// A control's face for `label_face`: its rectangle and the draw list it was drawn on (compared
// as an address, never read).
struct FaceBox {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    const void* list = nullptr;
};

// The face a label is drawn on: the index in `faces` of the smallest one on the label's own draw
// list (`list`) that holds the middle of its ink or either end of it, at the ink's height `ink_cy`
// (all in the faces' own unit; an end counts when it lies at least `kLabelTolerancePx` in from the
// face's edge, so a label that stops where a switch beside it starts is not on the switch), or -1 when none does. A face on another list is a control a card
// lies over, never the label's. Taking either end as well as the middle keeps a label that spills
// past its chip off the row behind it: the row would hold its middle and pass it.
int label_face(std::span<const FaceBox> faces, const void* list, float ink_left, float ink_right,
               float ink_cy);

} // namespace etude::app::ui
