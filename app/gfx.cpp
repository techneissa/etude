// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx.hpp"

#include "glow_shader.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace etude::app::gfx {

namespace {

// The renderer SDL is asked for on each path, or null for SDL's own choice.
const char* driver_name(RendererPath path) {
    switch (path) {
    case RendererPath::Gpu:
        return "gpu";
    case RendererPath::Gl:
#if defined(__linux__)
        return "opengles2";
#else
        return "opengl";
#endif
    case RendererPath::Basic:
        break;
    }
    return nullptr;
}

// OpenGL's version and shading language version, when the renderer holds a GL context.
std::string gl_versions() {
    if (SDL_GL_GetCurrentContext() == nullptr) {
        return {};
    }
    using GetString = const unsigned char* (*)(unsigned int);
    constexpr unsigned int kVersion = 0x1F02;
    constexpr unsigned int kShadingLanguage = 0x8B8C;
    const auto get_string = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    if (get_string == nullptr) {
        return {};
    }
    const auto text = [&](unsigned int name) {
        const unsigned char* s = get_string(name);
        return s != nullptr ? std::string(reinterpret_cast<const char*>(s)) : std::string("?");
    };
    return ", GL " + text(kVersion) + ", GLSL " + text(kShadingLanguage);
}

// What the renderer is, in one log line: the SDL driver, and the GPU API or GL versions under it.
void log_renderer(SDL_Renderer* renderer, RendererPath path, double test_ms) {
    std::string extra;
    if (path == RendererPath::Gpu) {
        const SDL_PropertiesID props = SDL_GetRendererProperties(renderer);
        auto* device = static_cast<SDL_GPUDevice*>(
            SDL_GetPointerProperty(props, SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
        if (device != nullptr) {
            extra = std::string(", ") + SDL_GetGPUDeviceDriver(device);
        }
    } else {
        extra = gl_versions();
    }
    SDL_Log("renderer %s (SDL %s%s), test frame %.1f ms",
            std::string(renderer_path_name(path)).c_str(), SDL_GetRendererName(renderer),
            extra.c_str(), test_ms);
}

// Draws one red frame and reads a pixel back: a path that makes a renderer but draws nothing is
// as bad as one that cannot be made, and one that cannot be read back cannot be shown to draw.
bool draws_a_frame(SDL_Renderer* renderer) {
    clear(renderer, 255, 0, 0, 255);
    flush(renderer);
    const SDL_Rect pixel{0, 0, 1, 1};
    SDL_Surface* read = read_pixels(renderer, &pixel);
    if (read == nullptr) {
        return false;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);
    if (rgba == nullptr) {
        return false;
    }
    const auto* px = static_cast<const std::uint8_t*>(rgba->pixels);
    const bool red = px[0] > 200 && px[1] < 60 && px[2] < 60;
    SDL_DestroySurface(rgba);
    return red;
}

// The window and renderer of a named SDL renderer; false (nothing left made) when it cannot be.
bool create_named(const char* title, int w, int h, SDL_WindowFlags flags, RendererPath path,
                  SDL_Window** window, SDL_Renderer** renderer, double* test_ms) {
    // Hidden while the renderer is made, as SDL_CreateWindowAndRenderer does: the renderer may
    // recreate the window, and that should not flash.
    const bool hidden = (flags & SDL_WINDOW_HIDDEN) != 0;
    flags |= SDL_WINDOW_HIDDEN;
#if defined(__linux__)
    if (path == RendererPath::Gpu) {
        flags |= SDL_WINDOW_VULKAN;
    }
#endif
    *window = SDL_CreateWindow(title, w, h, flags);
    if (*window == nullptr) {
        *renderer = nullptr;
        return false;
    }
    *renderer = SDL_CreateRenderer(*window, driver_name(path));
    if (*renderer == nullptr) {
        SDL_DestroyWindow(*window);
        *window = nullptr;
        return false;
    }
    const std::uint64_t test_start = SDL_GetPerformanceCounter();
    const bool drew = draws_a_frame(*renderer);
    *test_ms = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - test_start) /
               static_cast<double>(SDL_GetPerformanceFrequency());
    if (!drew) {
        SDL_SetError("a test frame read back wrong or not at all");
        SDL_DestroyRenderer(*renderer);
        SDL_DestroyWindow(*window);
        *renderer = nullptr;
        *window = nullptr;
        return false;
    }
    if (!hidden) {
        SDL_ShowWindow(*window);
    }
    return true;
}

} // namespace

bool create_window_and_renderer(const char* title, int w, int h, SDL_WindowFlags flags,
                                RendererPath path, SDL_Window** window, SDL_Renderer** renderer,
                                RendererPath* used) {
    if (used != nullptr) {
        *used = RendererPath::Basic;
    }
    if (path != RendererPath::Basic) {
        double test_ms = 0.0;
        if (create_named(title, w, h, flags, path, window, renderer, &test_ms)) {
            log_renderer(*renderer, path, test_ms);
            if (used != nullptr) {
                *used = path;
            }
            return true;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "renderer %s failed (%s), using basic",
                    std::string(renderer_path_name(path)).c_str(), SDL_GetError());
    }
    return SDL_CreateWindowAndRenderer(title, w, h, flags, window, renderer);
}

void destroy_renderer(SDL_Renderer* renderer) {
    release_glow_shader(renderer);
    SDL_DestroyRenderer(renderer);
}

bool replace_renderer(SDL_Window* window, SDL_Renderer** renderer,
                      std::span<const RendererPath> tries, RendererPath* used) {
    if (*renderer != nullptr) {
        destroy_renderer(*renderer);
        *renderer = nullptr;
    }
    ++g_epoch;
    for (const RendererPath path : tries) {
        const std::string name(renderer_path_name(path));
        if ((g_refused_paths & path_bit(path)) != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "renderer %s refused by --device-lost",
                        name.c_str());
            continue;
        }
        SDL_Renderer* made = SDL_CreateRenderer(window, driver_name(path));
        if (made != nullptr && !draws_a_frame(made)) {
            SDL_SetError("a test frame read back wrong or not at all");
            SDL_DestroyRenderer(made);
            made = nullptr;
        }
        if (made != nullptr) {
            *renderer = made;
            *used = path;
            return true;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "renderer %s could not be made again (%s)",
                    name.c_str(), SDL_GetError());
    }
    return false;
}

} // namespace etude::app::gfx
