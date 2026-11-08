// SPDX-License-Identifier: GPL-3.0-or-later
#include "brand_marks.hpp"

#include "../gfx.hpp"
#include "../icon_assets.hpp"
#include "icon_audit.hpp"
#include "kit.hpp"

#include <SDL3/SDL.h>
#include <lunasvg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>

namespace etude::app {
std::span<const unsigned char> embedded_brand_github();
std::span<const unsigned char> embedded_brand_coffee();
} // namespace etude::app

namespace etude::app::ui {

namespace {

// The Buy Me a Coffee tile's corners, as a share of its side (the app's own links to it round it
// the same way).
constexpr float kCoffeeRound = 0.22f;

struct Cached {
    SDL_Texture* texture = nullptr;
    SDL_Renderer* renderer = nullptr;
    int px = 0;
};
std::array<Cached, 2> g_cache;
std::array<Cached, etude::kIconColourCount * 2> g_icon_cache; // a colour, then its slot

struct Picture {
    std::vector<std::uint32_t> pixels; // ARGB, row after row
    int w = 0;
    int h = 0;
};

Picture decode_png(std::span<const unsigned char> bytes) {
    Picture out;
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    SDL_Surface* loaded = io == nullptr ? nullptr : SDL_LoadPNG_IO(io, true);
    if (loaded == nullptr) {
        return out;
    }
    SDL_Surface* argb = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(loaded);
    if (argb == nullptr) {
        return out;
    }
    out.w = argb->w;
    out.h = argb->h;
    out.pixels.resize(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h));
    for (int row = 0; row < out.h; ++row) {
        const auto* src = reinterpret_cast<const std::uint8_t*>(argb->pixels) +
                          static_cast<std::size_t>(row) * static_cast<std::size_t>(argb->pitch);
        std::copy_n(src, static_cast<std::size_t>(out.w) * 4,
                    reinterpret_cast<std::uint8_t*>(out.pixels.data()) +
                        static_cast<std::size_t>(row) * static_cast<std::size_t>(out.w) * 4);
    }
    SDL_DestroySurface(argb);
    return out;
}

// The GitHub mark: white with the drawing's coverage as alpha, so the draw call's colour tints it.
Picture render_mark(std::span<const unsigned char> svg, int px) {
    Picture out;
    const auto doc = lunasvg::Document::loadFromData(
        reinterpret_cast<const char*>(svg.data()), svg.size());
    if (!doc) {
        return out;
    }
    const lunasvg::Bitmap bitmap = doc->renderToBitmap(px, px);
    if (bitmap.isNull()) {
        return out;
    }
    out.w = px;
    out.h = px;
    out.pixels.resize(static_cast<std::size_t>(px) * static_cast<std::size_t>(px));
    const std::uint8_t* src = bitmap.data();
    for (int row = 0; row < px; ++row) {
        for (int col = 0; col < px; ++col) {
            const std::uint8_t alpha = src[row * bitmap.stride() + col * 4 + 3];
            out.pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(px) +
                       static_cast<std::size_t>(col)] =
                (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
        }
    }
    return out;
}

// `p` made `px` across by area averaging when it is larger; a smaller picture is kept as it is.
Picture fitted(Picture p, int px) {
    if (p.w > px && p.h > px) {
        p.pixels = downscale_argb(p.pixels, p.w, p.h, px, px);
        p.w = px;
        p.h = px;
    }
    return p;
}

Picture picture_for(Brand brand, int px) {
    if (brand == Brand::GitHub) {
        return render_mark(embedded_brand_github(), px);
    }
    return fitted(decode_png(embedded_brand_coffee()), px);
}

Picture icon_picture(etude::IconColour colour, int px) {
    return fitted(decode_png(icon_png(colour, 256)), px);
}

void drop(Cached& c) {
    if (c.texture != nullptr) {
        gfx::destroy_texture(c.texture);
    }
    c = Cached{};
}

// The texture in `c` for `px`, made from `make(px)` when it is not there yet.
template <class Make> SDL_Texture* texture_in(Cached& c, SDL_Renderer* renderer, int px, Make make) {
    if (c.renderer == renderer && c.px == px) {
        return c.texture;
    }
    // A texture of another renderer was freed with it: forget it rather than destroy it. The size
    // is recorded first, so a failure is tried once per size, not every frame.
    if (c.renderer != renderer) {
        c.texture = nullptr;
    }
    drop(c);
    c.renderer = renderer;
    c.px = px;
    const Picture p = make(px);
    if (p.pixels.empty()) {
        return nullptr;
    }
    SDL_Texture* tex = gfx::create_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STATIC, p.w, p.h);
    if (tex == nullptr) {
        return nullptr;
    }
    gfx::update_texture(tex, p.pixels.data(), p.w * 4);
    gfx::set_texture_blend(tex, SDL_BLENDMODE_BLEND);
    c.texture = tex;
    return tex;
}

SDL_Texture* texture_for(SDL_Renderer* renderer, Brand brand, int px) {
    return texture_in(g_cache[static_cast<std::size_t>(brand)], renderer, px,
                      [brand](int size) { return picture_for(brand, size); });
}

} // namespace

std::vector<std::uint32_t> downscale_argb(const std::vector<std::uint32_t>& src, int sw, int sh,
                                          int dw, int dh) {
    if (dw <= 0 || dh <= 0 || dw >= sw || dh >= sh ||
        src.size() != static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh)) {
        return src;
    }
    std::vector<std::uint32_t> out(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh));
    const double step_x = static_cast<double>(sw) / dw;
    const double step_y = static_cast<double>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        const double y0 = y * step_y;
        const double y1 = y0 + step_y;
        for (int x = 0; x < dw; ++x) {
            const double x0 = x * step_x;
            const double x1 = x0 + step_x;
            double a = 0, r = 0, g = 0, b = 0, weight = 0;
            for (int sy = static_cast<int>(y0); sy < sh && sy < y1; ++sy) {
                const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (int sx = static_cast<int>(x0); sx < sw && sx < x1; ++sx) {
                    const double w = wy * (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0));
                    const std::uint32_t p = src[static_cast<std::size_t>(sy) *
                                                    static_cast<std::size_t>(sw) +
                                                static_cast<std::size_t>(sx)];
                    const double pa = static_cast<double>(p >> 24) * w;
                    a += pa;
                    r += static_cast<double>((p >> 16) & 0xFF) * pa;
                    g += static_cast<double>((p >> 8) & 0xFF) * pa;
                    b += static_cast<double>(p & 0xFF) * pa;
                    weight += w;
                }
            }
            std::uint32_t px = 0;
            if (a > 0.0 && weight > 0.0) {
                const auto byte = [](double v) {
                    return static_cast<std::uint32_t>(std::clamp(std::lround(v), 0L, 255L));
                };
                px = (byte(a / weight) << 24) | (byte(r / a) << 16) | (byte(g / a) << 8) |
                     byte(b / a);
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) +
                static_cast<std::size_t>(x)] = px;
        }
    }
    return out;
}

void draw_brand(const Kit& k, ImDrawList* dl, Brand brand, ImVec2 centre, float side,
                ImVec4 tint) {
    if (audit::icons_hidden() || k.renderer == nullptr || !(side > 0.0f)) {
        return;
    }
    const float fb = std::max(1.0f, k.fb_scale);
    const int px = std::max(1, static_cast<int>(std::lround(side * fb)));
    SDL_Texture* tex = texture_for(k.renderer, brand, px);
    if (tex == nullptr) {
        return;
    }
    // Whole framebuffer pixels, so the picture is as sharp as the text beside it.
    const float edge = static_cast<float>(px) / fb;
    const float x0 = std::round((centre.x - edge * 0.5f) * fb) / fb;
    const float y0 = std::round((centre.y - edge * 0.5f) * fb) / fb;
    const ImTextureRef ref(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(tex)));
    const ImU32 colour = brand == Brand::GitHub ? u32(tint) : u32({1, 1, 1, 1});
    if (brand == Brand::BuyMeACoffee) {
        dl->AddImageRounded(ref, {x0, y0}, {x0 + edge, y0 + edge}, {0, 0}, {1, 1}, colour,
                            edge * kCoffeeRound);
    } else {
        dl->AddImage(ref, {x0, y0}, {x0 + edge, y0 + edge}, {0, 0}, {1, 1}, colour);
    }
}

void draw_app_icon(const Kit& k, ImDrawList* dl, etude::IconColour colour, IconSlot slot,
                   ImVec2 centre, float side, float alpha) {
    if (audit::icons_hidden() || k.renderer == nullptr || !(side > 0.0f)) {
        return;
    }
    const float fb = std::max(1.0f, k.fb_scale);
    const int px = std::max(1, static_cast<int>(std::lround(side * fb)));
    Cached& c = g_icon_cache[static_cast<std::size_t>(colour) * 2 + static_cast<std::size_t>(slot)];
    SDL_Texture* tex = texture_in(c, k.renderer, px, [colour](int size) {
        return icon_picture(colour, size);
    });
    if (tex == nullptr) {
        return;
    }
    const float edge = static_cast<float>(px) / fb;
    const float x0 = std::round((centre.x - edge * 0.5f) * fb) / fb;
    const float y0 = std::round((centre.y - edge * 0.5f) * fb) / fb;
    const ImTextureRef ref(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(tex)));
    const ImU32 tint = u32({1, 1, 1, alpha});
    dl->AddImage(ref, {x0, y0}, {x0 + edge, y0 + edge}, {0, 0}, {1, 1}, tint);
}

void release_brands() {
    for (Cached& c : g_cache) {
        drop(c);
    }
    for (Cached& c : g_icon_cache) {
        drop(c);
    }
}

} // namespace etude::app::ui
