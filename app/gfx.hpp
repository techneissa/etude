// SPDX-License-Identifier: GPL-3.0-or-later
// The drawing boundary: every call into SDL's render API that is not part of the drawing layer
// itself (Mesh, the style painter in effects.cpp, the text and texture helpers) goes through
// these functions, so the scene and the UI never call SDL_Render*, SDL_*Texture* or the renderer
// state functions directly. Each one is a thin inline wrapper with the semantics of the SDL call
// it names: it is used every frame and the build has no link-time optimisation, so a call into
// another .cpp would never be inlined. Only the start-up call that makes the window and the
// renderer lives in gfx.cpp. Which renderer draws is chosen behind these. What they draw is also
// mixed into the frame's picture hash (frame_picture.hpp), which the idle loop reads.
#pragma once

#include "frame_picture.hpp"

#include <SDL3/SDL.h>

#include "etude/renderer_path.hpp"

#include <cstdint>
#include <span>

namespace etude::app::gfx {

using etude::RendererPath;

// ---- Renderer and window ----------------------------------------------------------------------

// The window and the renderer of `path` on it. Basic is SDL's own choice
// (SDL_CreateWindowAndRenderer). Gpu and Gl name SDL's renderer on a window made here; on Linux the
// Gpu window gets SDL_WINDOW_VULKAN (the flag form of SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN),
// which KMSDRM needs before SDL_gpu finds a display. A path that cannot be created, or whose test
// frame (one red clear) does not read back red, logs one line and falls back to Basic; `used` (when
// given) is the path that was made. Start-up only.
bool create_window_and_renderer(const char* title, int w, int h, SDL_WindowFlags flags,
                                RendererPath path, SDL_Window** window, SDL_Renderer** renderer,
                                RendererPath* used = nullptr);

// Whether SDL lists the driver `path` asks for (Basic always). Making it may still fail.
bool driver_listed(RendererPath path);

// Frees what the drawing layer made for the renderer (the glow shader), then the renderer.
void destroy_renderer(SDL_Renderer* renderer);

// Counts the renderers a run has had, from 1. A texture belongs to the renderer of the epoch it
// was made in and goes with it: an owner that keeps textures across a replacement compares this
// number before it destroys or draws one, since a texture of an earlier epoch is already freed.
inline std::uint32_t g_epoch = 1;
inline std::uint32_t epoch() { return g_epoch; }

// Replaces the window's renderer after start-up (a lost device, a failing present). Every owner of
// textures has let go of them first (they are destroyed with the old renderer, still alive then);
// this destroys it, starts a new epoch, and makes a renderer of the first of `tries` that can be
// made and draws a test frame, on the same window. `used` is the path made; false (and no renderer)
// when none can be.
bool replace_renderer(SDL_Window* window, SDL_Renderer** renderer,
                      std::span<const RendererPath> tries, RendererPath* used);

// Developer option (--device-lost ...:refuse=PATH): the paths `replace_renderer` pretends it cannot
// make, one bit each, to see the fallback. Nothing is refused by default.
inline unsigned g_refused_paths = 0;
inline void refuse_paths(unsigned mask) { g_refused_paths = mask; }
inline unsigned path_bit(RendererPath path) { return etude::renderer_path_bit(path); }

inline SDL_Window* window_of(SDL_Renderer* renderer) { return SDL_GetRenderWindow(renderer); }

inline const char* renderer_name(SDL_Renderer* renderer) { return SDL_GetRendererName(renderer); }

inline bool set_vsync(SDL_Renderer* renderer, int vsync) {
    return SDL_SetRenderVSync(renderer, vsync);
}

// The size in pixels of what the renderer draws into (the window, or the current target).
inline bool output_size(SDL_Renderer* renderer, int* w, int* h) {
    return SDL_GetRenderOutputSize(renderer, w, h);
}

// ---- Frame ------------------------------------------------------------------------------------

inline void flush(SDL_Renderer* renderer) { SDL_FlushRenderer(renderer); }

// False when the present failed (a lost device, a surface that cannot be shown).
inline bool present(SDL_Renderer* renderer) { return SDL_RenderPresent(renderer); }

// Fills the whole target (ignoring the clip) with an opaque or transparent colour.
inline void clear(SDL_Renderer* renderer, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                  std::uint8_t a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderClear(renderer);
}

// The pixels of the current target, or of `area` of it (null: all); the caller frees the surface.
inline SDL_Surface* read_pixels(SDL_Renderer* renderer, const SDL_Rect* area) {
    return SDL_RenderReadPixels(renderer, area);
}

// ---- State ------------------------------------------------------------------------------------

inline void set_clip(SDL_Renderer* renderer, const SDL_Rect* clip) {
    SDL_SetRenderClipRect(renderer, clip);
}

inline void clear_clip(SDL_Renderer* renderer) { SDL_SetRenderClipRect(renderer, nullptr); }

inline void set_scale(SDL_Renderer* renderer, float sx, float sy) {
    SDL_SetRenderScale(renderer, sx, sy);
}

inline void set_blend(SDL_Renderer* renderer, SDL_BlendMode mode) {
    SDL_SetRenderDrawBlendMode(renderer, mode);
}

inline SDL_Texture* target(SDL_Renderer* renderer) { return SDL_GetRenderTarget(renderer); }

// Null draws to the window again.
inline void set_target(SDL_Renderer* renderer, SDL_Texture* texture) {
    SDL_SetRenderTarget(renderer, texture);
}

inline void set_colour(SDL_Renderer* renderer, float r, float g, float b, float a) {
    if (picture::on()) {
        picture::colour({r, g, b, a});
    }
    SDL_SetRenderDrawColorFloat(renderer, r, g, b, a);
}

inline void set_colour8(SDL_Renderer* renderer, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                        std::uint8_t a) {
    if (picture::on()) {
        picture::mix(std::uint64_t{r} | std::uint64_t{g} << 8 | std::uint64_t{b} << 16 |
                     std::uint64_t{a} << 24);
    }
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

// ---- Primitives, in the current colour and blend mode -----------------------------------------

inline void fill_rect(SDL_Renderer* renderer, const SDL_FRect& rect) {
    picture::rect(nullptr, rect, {});
    SDL_RenderFillRect(renderer, &rect);
}

inline void fill_rects(SDL_Renderer* renderer, const SDL_FRect* rects, int count) {
    for (int i = 0; i < count; ++i) {
        picture::rect(nullptr, rects[i], {});
    }
    SDL_RenderFillRects(renderer, rects, count);
}

inline void line(SDL_Renderer* renderer, float x1, float y1, float x2, float y2) {
    picture::rect(nullptr, {x1, y1, x2, y2}, {});
    SDL_RenderLine(renderer, x1, y1, x2, y2);
}

// Triangles with a colour at each corner, textured when `texture` is not null.
inline void geometry(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Vertex* vertices,
                     int vertex_count, const int* indices = nullptr, int index_count = 0) {
    picture::vertices(texture, vertices, vertex_count, index_count);
    SDL_RenderGeometry(renderer, texture, vertices, vertex_count, indices, index_count);
}

// The whole texture (null `src`) or a part of it, scaled to `dst` (null: the whole target).
inline void draw_texture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect* src,
                         const SDL_FRect* dst) {
    if (picture::on()) {
        picture::rect(texture, dst != nullptr ? *dst : SDL_FRect{}, {1.0f, 1.0f, 1.0f, 1.0f});
        picture::rect(texture, src != nullptr ? *src : SDL_FRect{}, {});
    }
    SDL_RenderTexture(renderer, texture, src, dst);
}

// ---- Textures ---------------------------------------------------------------------------------

inline SDL_Texture* create_texture(SDL_Renderer* renderer, SDL_PixelFormat format,
                                   SDL_TextureAccess access, int w, int h) {
    return SDL_CreateTexture(renderer, format, access, w, h);
}

inline void destroy_texture(SDL_Texture* texture) { SDL_DestroyTexture(texture); }

inline bool update_texture(SDL_Texture* texture, const void* pixels, int pitch) {
    return SDL_UpdateTexture(texture, nullptr, pixels, pitch);
}

inline void set_texture_blend(SDL_Texture* texture, SDL_BlendMode mode) {
    SDL_SetTextureBlendMode(texture, mode);
}

inline void set_texture_linear(SDL_Texture* texture) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
}

} // namespace etude::app::gfx
