// SPDX-License-Identifier: GPL-3.0-or-later
// Which of SDL's renderers draws the frame. Plain values only: the app maps each to a renderer.
#pragma once

#include <cstdint>
#include <string_view>

namespace etude {

// Basic is the renderer SDL picks itself (how the app drew before the choice existed). Gpu is
// SDL's GPU renderer (Metal on a Mac, Vulkan on Linux). Gl is OpenGL (OpenGL ES 2 on Linux). A
// path the machine cannot create falls back to Basic.
enum class RendererPath : std::uint8_t {
    Basic,
    Gpu,
    Gl,
};

inline constexpr std::string_view renderer_path_name(RendererPath path) {
    switch (path) {
    case RendererPath::Gpu:
        return "gpu";
    case RendererPath::Gl:
        return "gl";
    case RendererPath::Basic:
        break;
    }
    return "basic";
}

// One bit per path, for a set of them.
inline constexpr unsigned renderer_path_bit(RendererPath path) {
    return 1u << static_cast<unsigned>(path);
}

// "basic", "gpu" or "gl"; false (and `out` untouched) for anything else.
inline constexpr bool parse_renderer_path(std::string_view text, RendererPath* out) {
    for (const RendererPath path : {RendererPath::Basic, RendererPath::Gpu, RendererPath::Gl}) {
        if (text == renderer_path_name(path)) {
            *out = path;
            return true;
        }
    }
    return false;
}

} // namespace etude
