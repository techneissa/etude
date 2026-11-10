// SPDX-License-Identifier: GPL-3.0-or-later
#include "window_icon.hpp"

#include "icon_assets.hpp"

#include <SDL3/SDL.h>

#include <span>

namespace etude::app {

namespace {

SDL_Surface* decode_png(std::span<const unsigned char> bytes) {
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    return io == nullptr ? nullptr : SDL_LoadPNG_IO(io, true);
}

} // namespace

void set_window_icon(SDL_Window* window, etude::IconColour colour) {
    SDL_Surface* big = decode_png(icon_png(colour, 256));
    if (big == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "window icon: %s", SDL_GetError());
        return;
    }
    if (SDL_Surface* small = decode_png(icon_png(colour, 64))) {
        // The surface keeps its own reference to the alternate image.
        SDL_AddSurfaceAlternateImage(big, small);
        SDL_DestroySurface(small);
    }
    if (!SDL_SetWindowIcon(window, big)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "window icon: %s", SDL_GetError());
    }
    SDL_DestroySurface(big);
}

} // namespace etude::app
