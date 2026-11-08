// SPDX-License-Identifier: GPL-3.0-or-later
// A hash of what a frame draws, so the loop can tell that a frame looks like the one before (core
// IdleGate). The drawing code mixes in its geometry and its textures' places; positions are rounded
// to a sixteenth of a pixel and colours to 8 bits, so an easing that has stopped changing the
// picture reads as still. Frame thread only. Off (a bool test per call) while the song plays.
#pragma once

#include "picture_hash.hpp"

#include <SDL3/SDL.h>

namespace etude::app::picture {

inline void colour(const SDL_FColor& c) {
    mix(unit(c.r) | unit(c.g) << 8 | unit(c.b) << 16 | unit(c.a) << 24);
}

inline void rect(const void* texture, const SDL_FRect& r, const SDL_FColor& c) {
    if (!g_on) {
        return;
    }
    mix(ptr(texture));
    mix(pos(r.x) ^ pos(r.y) << 20 ^ pos(r.w) << 40);
    mix(pos(r.h));
    colour(c);
}

inline void vertices(const void* texture, const SDL_Vertex* v, int count, int indices) {
    if (!g_on) {
        return;
    }
    mix(ptr(texture));
    mix(static_cast<std::uint64_t>(count) << 32 | static_cast<std::uint64_t>(indices));
    for (int i = 0; i < count; ++i) {
        mix(pos(v[i].position.x) ^ pos(v[i].position.y) << 24);
        colour(v[i].color);
        mix(pos(v[i].tex_coord.x * 64.0f) ^ pos(v[i].tex_coord.y * 64.0f) << 24);
    }
}

// SDL_RenderGeometry and SDL_RenderTexture, with the call mixed into the hash first.
inline void render_geometry(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Vertex* v,
                            int count, const int* indices, int index_count) {
    vertices(texture, v, count, index_count);
    SDL_RenderGeometry(renderer, texture, v, count, indices, index_count);
}
inline void render_texture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect& dst,
                           const SDL_FColor& colour) {
    rect(texture, dst, colour);
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
}

} // namespace etude::app::picture
