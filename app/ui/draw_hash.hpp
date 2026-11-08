// SPDX-License-Identifier: GPL-3.0-or-later
// A hash of what the UI draws, for the idle gate (frame_picture.hpp).
#pragma once

#include "../picture_hash.hpp"

#include <imgui.h>

#include <cstdint>

namespace etude::app::ui {

// What the UI draws, for the idle gate (frame_picture.hpp): each list's vertices (positions rounded)
// and each command's clip, size and texture.
inline void hash_draw_data(const ImDrawData* data) {
    for (const ImDrawList* list : data->CmdLists) {
        picture::mix(static_cast<std::uint64_t>(list->VtxBuffer.Size));
        for (const ImDrawVert& v : list->VtxBuffer) {
            picture::mix(picture::pos(v.pos.x) ^ picture::pos(v.pos.y) << 24 ^
                         static_cast<std::uint64_t>(v.col) << 40);
            picture::mix(picture::pos(v.uv.x * 64.0f) ^ picture::pos(v.uv.y * 64.0f) << 24 ^
                         static_cast<std::uint64_t>(v.col >> 24));
        }
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            picture::mix(cmd.ElemCount ^ picture::pos(cmd.ClipRect.x) << 20 ^
                         picture::pos(cmd.ClipRect.y) << 40);
            picture::mix(static_cast<std::uint64_t>(cmd.TexRef._TexID) ^ picture::ptr(cmd.TexRef._TexData));
            picture::mix(picture::pos(cmd.ClipRect.z) ^ picture::pos(cmd.ClipRect.w) << 24);
        }
    }
}

} // namespace etude::app::ui
