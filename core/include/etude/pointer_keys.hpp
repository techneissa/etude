// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/keyboard.hpp"
#include "etude/keyboard_view.hpp"
#include "etude/midi_event.hpp"
#include "etude/screen_layout.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace etude {

// Playing the on-screen keys by mouse or touch.

// Velocity from where along the key the press lands: the back (top) edge of the key is
// `kPointerMinVelocity` and the front (bottom) edge is `kPointerMaxVelocity`, linear in between,
// rounded. A black key counts along its own length. Nearer the front is louder, like a real key
// pressed far from its pivot.
inline constexpr int kPointerMinVelocity = 24;
inline constexpr int kPointerMaxVelocity = 127;

// A sliding pointer leaves the key it is on only after it passes that key's edge by this much, so
// a resting finger that drifts a little does not sound the neighbour.
inline constexpr float kKeySlideMarginMm = 1.5f;

// Where the keys are on screen, as drawn this frame.
struct PointerGeometry {
    KeyboardLayout layout; // built from `view.layout_range`
    KeyViewFrame view;     // where the window sits on the layout
    RectF key_area;        // window pixels; keys are drawn, and hit, only here
    // `kKeySlideMarginMm` in window pixels (the display's pixels per mm times it); 0 changes key at
    // the edge itself. A pointer keeps its key while its distance to that key (straight line to the
    // nearest point of the key's shape, corners included) is at most this, and leaves it when the
    // distance is more.
    float slide_margin_px = 0.0f;
};

struct KeyHit {
    int note = 0;
    std::uint8_t velocity = 0;
};

// The key under the point (x, y) in window pixels, black keys first in their upper part, or nullopt
// when the point is outside `key_area` or on no key. The key area's right and bottom edges are
// outside it.
std::optional<KeyHit> hit_test_key(const PointerGeometry& geometry, float x, float y);

// What a finger event does to the on-screen keys.
enum class FingerPhase : std::uint8_t { Down, Motion, Up, Cancelled };
enum class FingerAction : std::uint8_t {
    Ignore,
    Press,  // PointerKeys::down
    Move,   // PointerKeys::move
    Release // PointerKeys::up
};

// The rules for finger events:
//  * only a touch screen plays (`direct_touch`); a trackpad's fingers are not on the keys, so
//    every phase from one is ignored;
//  * a lift or a cancel always releases, whatever the UI is doing, so no note keeps sounding and
//    no slot stays taken; a cancelled touch is a lift;
//  * a touch that begins where the UI has it (`ui_owns_touch`: the settings sheet, its backdrop, a
//    control) is the UI's and never presses a key;
//  * without key geometry nothing presses or moves.
FingerAction finger_action(FingerPhase phase, bool direct_touch, bool ui_owns_touch,
                           bool have_geometry);

// Mouse and fingers playing keys. Each pointer (a finger id, or the mouse) holds at most one note.
//  * down on a key presses it; down elsewhere is ignored, and so is everything that pointer does
//    until it is lifted;
//  * moving onto another key releases the old key and presses the new one (a glissando), with the
//    velocity of the place it entered; the move counts only once the pointer is more than
//    `slide_margin_px` away from the old key (exactly the margin still keeps it), so drift near an
//    edge or a corner keeps the note;
//  * moving off the keys releases the note (also only past the margin); coming back onto a key
//    while still down presses again;
//  * up releases it. `release_all` lets go of everything (focus lost, quitting).
// Two pointers on the same key share one note: it sounds until both let go.
// Events are appended to `out` with source `InputSource::Pointer`.
class PointerKeys {
  public:
    using PointerId = std::uint64_t;
    static constexpr std::size_t kMaxPointers = 16;

    void down(PointerId id, float x, float y, const PointerGeometry& geometry, std::int64_t time_ns,
              std::vector<MidiEvent>& out);
    void move(PointerId id, float x, float y, const PointerGeometry& geometry, std::int64_t time_ns,
              std::vector<MidiEvent>& out);
    // A pointer that is not down does nothing.
    void up(PointerId id, std::int64_t time_ns, std::vector<MidiEvent>& out);
    void release_all(std::int64_t time_ns, std::vector<MidiEvent>& out);

    // Pointers that went down on a key and are still down, whether or not a key is under them.
    int active_pointers() const;
    // Notes sounding.
    int held_notes() const;

  private:
    struct Pointer {
        bool used = false;
        PointerId id = 0;
        int note = -1; // -1: down on a key earlier, now over none
    };

    Pointer* find(PointerId id);
    const Pointer* find(PointerId id) const;
    void press(int note, int velocity, std::int64_t time_ns, std::vector<MidiEvent>& out);
    void release(int note, std::int64_t time_ns, std::vector<MidiEvent>& out);

    std::array<Pointer, kMaxPointers> pointers_{};
    std::array<std::uint8_t, 128> shared_{};
};

} // namespace etude
