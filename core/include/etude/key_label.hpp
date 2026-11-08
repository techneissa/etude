// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "etude/screen_layout.hpp"

namespace etude {

// The song's key shown faintly at the bottom left of the note area, just above the keyboard
// ("G major 1#"). The words come from key_signature.hpp; this is where the label sits, how faint it
// is and how it fades when the key changes. Pure: the renderer measures the text and draws it.

// How opaque the label is, as a share of full: 35 to 45 percent, faint enough not to distract.
inline constexpr float kKeyLabelOpacity = 0.40f;
// While notes pass behind it the label is this share of its opacity, so it never hides a block.
inline constexpr float kKeyLabelBehindShare = 0.45f;
// A change of key cross-fades over this long; the label also dims and returns over `kKeyLabelDimNs`.
inline constexpr std::int64_t kKeyLabelFadeNs = 400'000'000;
inline constexpr std::int64_t kKeyLabelDimNs = 250'000'000;

// The text size, in pixels at a screen unit of `unit` (pixels per 1080p pixel, so the window's
// size sets it): `kKeyLabelFontPx` at 1080p, never under `kMinKeyLabelFontPx` or over
// `kMaxKeyLabelFontPx`, whole pixels.
inline constexpr float kKeyLabelFontPx = 26.0f;
inline constexpr float kMinKeyLabelFontPx = 12.0f;
inline constexpr float kMaxKeyLabelFontPx = 40.0f;
float key_label_font_px(float unit);

// The space round the text: this share of its height from the area's left and bottom edges.
inline constexpr float kKeyLabelMarginEm = 0.6f;
// The gap kept from a rect the label dodges.
inline constexpr float kKeyLabelGapPx = 2.0f;

struct KeyLabelPlacement {
    bool visible = false;
    RectF rect; // the text's box; empty when hidden
};

// Puts a label of `width` by `height` pixels in the bottom-left corner of `area` (the note area,
// or what sheet music leaves of it), `margin` pixels from its left and bottom edges. A rect of
// `avoid` (other things the layout keeps in that corner: the hand-split line, a toast) that it
// would touch moves it above that rect; it is hidden when it then leaves the area or is wider than
// the area. An empty `area`, a bad size or an empty text gives nothing.
KeyLabelPlacement place_key_label(const RectF& area, float width, float height, float margin,
                                  std::span<const RectF> avoid = {});

// What the label shows over time. Call `update` once a frame. The label wanted is named by `id`
// (any number, equal ids are the same label); a new id cross-fades: the new label rises and the old
// one sinks together over `kKeyLabelFadeNs`, each in a slot of its own (0 or 1) that the caller
// fills with the label's words when `update` says a new label took a slot. `shown` false fades
// both out. `behind` says notes pass behind the label this frame: its opacity eases to
// `kKeyLabelBehindShare` of the usual and back. `instant` (reduced effects) skips every fade.
class KeyLabelFade {
  public:
    struct Update {
        bool new_label = false; // `slot` was given a new label: fill it
        int slot = 0;
    };
    struct Layer {
        std::uint64_t id = 0;
        float presence = 0.0f; // 0..1
    };

    Update update(std::int64_t now_ns, bool shown, std::uint64_t id, bool behind, bool instant);

    const Layer& layer(int slot) const { return layers_[slot & 1]; }
    // The label's opacity this frame before a layer's presence: `kKeyLabelOpacity`, less while
    // notes pass behind.
    float opacity() const;
    // Whether anything is on screen.
    bool visible() const { return layers_[0].presence > 0.0f || layers_[1].presence > 0.0f; }
    // Forgets everything: the label is gone at once.
    void reset();

  private:
    Layer layers_[2];
    int current_ = 0;
    bool has_current_ = false;
    float dim_ = 0.0f; // 0 not behind, 1 behind
    std::int64_t last_ns_ = 0;
    bool started_ = false;
};

} // namespace etude
