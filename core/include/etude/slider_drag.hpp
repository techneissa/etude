// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace etude {

// Which input is on a slider. A mouse presses where it means to be; a finger covers what it
// presses, so it drags from where the thumb is.
enum class SliderPointer : std::uint8_t { Mouse, Finger };

// What a press on a slider has turned into.
//  Undecided: a finger that has not yet moved past the slop.
//  Drag:      the slider owns the touch and follows it.
//  Scroll:    the touch is mostly vertical and belongs to the sheet's scroll; the slider never
//             changes the value for it.
enum class SliderMode : std::uint8_t { Idle, Undecided, Drag, Scroll };

// The track's two ends in the caller's units (points or pixels; all inputs use the same ones).
struct SliderTrack {
    float x0 = 0.0f;
    float x1 = 1.0f;
};

// Decides what a touch on a slider means. It has no screen and no clock, so the same input always
// gives the same answer.
//
// Rules:
// - A mouse press sets the value to the press point and the drag follows the pointer.
// - A finger press changes nothing. The finger drags the value from where it was by the
//   finger's horizontal movement (delta), so putting a finger on the thumb never moves it.
// - A finger that stays within `slop` of where it began is a tap. On release, a tap on the track
//   (not on the thumb) moves the thumb to the tap; a tap on the thumb changes nothing.
// - Once a finger is past the slop, the larger of its horizontal and vertical movement decides, for
//   good: vertical wins only when it is strictly larger. Horizontal makes it a Drag; vertical hands
//   the touch to the sheet scroll (Scroll), and the value never changes for that touch.
// - A drag that began as a finger drag measures the value change from the finger's start point,
//   slop included, and clamps to the track.
class SliderDrag {
  public:
    // `value` is the slider's position in 0..1; `thumb_reach` is how far from the thumb's centre a
    // touch still counts as on the thumb. Returns the new value when the press itself sets one.
    std::optional<float> press(SliderPointer pointer, float x, float y, SliderTrack track,
                               float value, float thumb_reach);
    // The pointer moved. Returns the new value when it changes one.
    std::optional<float> move(float x, float y, SliderTrack track, float slop);
    // The pointer lifted (or the touch was taken away: `cancelled`). Returns the new value for a
    // tap on the track.
    std::optional<float> release(float x, SliderTrack track, bool cancelled = false);

    SliderMode mode() const { return mode_; }
    // The slider is following this touch, so the sheet must not scroll with it.
    bool dragging() const { return mode_ == SliderMode::Drag; }
    bool scrolling() const { return mode_ == SliderMode::Scroll; }

  private:
    SliderPointer pointer_ = SliderPointer::Mouse;
    SliderMode mode_ = SliderMode::Idle;
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    float origin_value_ = 0.0f;
    bool on_thumb_ = false;
};

// 0..1 for x along the track, clamped; 0 for a track with no length.
float slider_value_at(SliderTrack track, float x);

} // namespace etude
