// SPDX-License-Identifier: GPL-3.0-or-later
// The face and caption an icon control draws, kept inside the band it sits in.
#pragma once

#include "face_layout.hpp"
#include "kit_core.hpp"

#include <imgui.h>

namespace etude::app::ui {

// Keeps the faces and captions of the icon controls drawn in its scope inside `top`..`bottom` (a
// group's card, the pill under the bar), `kFaceInset` clear of its edges (face_layout.hpp).
class FaceBand {
  public:
    FaceBand(Kit& k, float top, float bottom) : k_(k), top_(k.face_top), bottom_(k.face_bottom) {
        k.face_top = top;
        k.face_bottom = bottom;
    }
    ~FaceBand() {
        k_.face_top = top_;
        k_.face_bottom = bottom_;
    }
    FaceBand(const FaceBand&) = delete;
    FaceBand& operator=(const FaceBand&) = delete;

  private:
    Kit& k_;
    float top_;
    float bottom_;
};

// An icon control's face and caption (face_layout.hpp) for a control centred on `centre` with a
// `hit` area: inside the current `FaceBand`, or the hit area without one. The face is asked
// `face_w` by `face_h` (square when they are equal); `caption` (may be null) is measured, drawn
// smaller when its cell is too narrow, and dropped when it cannot fit.
struct FacePlan {
    FaceLayout lay;
    float caption_size = 0.0f; // 0 when there is no caption
};
FacePlan plan_face(const Kit& k, ImVec2 centre, ImVec2 hit, float face_w, float face_h,
                   const char* caption);
// The area `plan_face` lays a control out in.
Box face_area(const Kit& k, ImVec2 centre, ImVec2 hit);

} // namespace etude::app::ui
