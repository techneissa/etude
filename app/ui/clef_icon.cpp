// SPDX-License-Identifier: GPL-3.0-or-later
#include "clef_icon.hpp"

#include "../gfx.hpp"
#include "kit.hpp"

#include "../sheet/sheet_job.hpp"

#include <SDL3/SDL.h>
#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

// Room round the glyph while its ink box is measured, in the font's units.
constexpr float kRoom = 2000.0f;

struct ClefCache {
    bool loaded = false;
    std::string group; // the glyph's <g> element from Bravura/E050.xml
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; // its ink box in the font's units
    SDL_Texture* texture = nullptr;
    SDL_Renderer* renderer = nullptr;
    int px_w = 0;
    int px_h = 0;
};
ClefCache g_clef;

std::string svg_around(const std::string& group, const std::string& view_box) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" + view_box + "\">" + group +
           "</svg>";
}

void load_once() {
    if (g_clef.loaded) {
        return;
    }
    g_clef.loaded = true;
    const std::filesystem::path file =
        std::filesystem::path(sheet_resource_dir()) / "Bravura" / "E050.xml";
    std::ifstream in(file);
    if (!in) {
        return;
    }
    std::stringstream text;
    text << in.rdbuf();
    g_clef.group = text.str();
    // The ink box: measured on a document with room round the glyph (the font's units, y up).
    const auto doc = lunasvg::Document::loadFromData(svg_around(g_clef.group, "-2000 -2000 4000 4000"));
    if (!doc) {
        g_clef.group.clear();
        return;
    }
    const lunasvg::Box box = doc->boundingBox();
    if (!(box.w > 0.0f && box.h > 0.0f)) {
        g_clef.group.clear();
        return;
    }
    // The box comes back relative to the view box's corner.
    g_clef.x = box.x - kRoom;
    g_clef.y = box.y - kRoom;
    g_clef.w = box.w;
    g_clef.h = box.h;
}

void make_texture(SDL_Renderer* renderer, int px_w, int px_h) {
    // A texture of another renderer was freed with it: forget it rather than destroy it. The size
    // and renderer are recorded first, so a failure below is tried once per size, not every frame.
    if (g_clef.renderer != renderer) {
        g_clef.texture = nullptr;
    }
    release_clef();
    g_clef.renderer = renderer;
    g_clef.px_w = px_w;
    g_clef.px_h = px_h;
    std::ostringstream box;
    box << g_clef.x << ' ' << g_clef.y << ' ' << g_clef.w << ' ' << g_clef.h;
    const auto doc = lunasvg::Document::loadFromData(svg_around(g_clef.group, box.str()));
    if (!doc) {
        return;
    }
    const lunasvg::Bitmap bitmap = doc->renderToBitmap(px_w, px_h);
    if (bitmap.isNull()) {
        return;
    }
    // White with the glyph's coverage as alpha: the draw call's colour tints it.
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(px_w) * static_cast<std::size_t>(px_h));
    const std::uint8_t* src = bitmap.data();
    for (int row = 0; row < px_h; ++row) {
        for (int col = 0; col < px_w; ++col) {
            const std::uint8_t alpha = src[row * bitmap.stride() + col * 4 + 3];
            pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(px_w) +
                   static_cast<std::size_t>(col)] =
                (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
        }
    }
    SDL_Texture* tex = gfx::create_texture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STATIC, px_w, px_h);
    if (tex == nullptr) {
        return;
    }
    gfx::update_texture(tex, pixels.data(), px_w * 4);
    gfx::set_texture_blend(tex, SDL_BLENDMODE_BLEND);
    g_clef.texture = tex;
}

} // namespace

float clef_aspect() {
    load_once();
    return g_clef.group.empty() ? 0.0f : g_clef.w / g_clef.h;
}

void draw_clef(const Kit& k, ImDrawList* dl, ImVec2 centre, float height, ImVec4 colour) {
    const float aspect = clef_aspect();
    if (aspect <= 0.0f || k.renderer == nullptr || !(height > 0.0f)) {
        return;
    }
    // One texel per framebuffer pixel, so the clef is as crisp as the text beside it.
    const float fb = std::max(1.0f, k.fb_scale);
    const int px_h = std::max(1, static_cast<int>(std::lround(height * fb)));
    const int px_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(px_h) * aspect)));
    if (g_clef.px_h != px_h || g_clef.px_w != px_w || g_clef.renderer != k.renderer) {
        make_texture(k.renderer, px_w, px_h);
    }
    if (g_clef.texture == nullptr) {
        return;
    }
    const float w = static_cast<float>(px_w) / fb;
    const float h = static_cast<float>(px_h) / fb;
    const float x0 = std::round((centre.x - w * 0.5f) * fb) / fb;
    const float y0 = std::round((centre.y - h * 0.5f) * fb) / fb;
    const ImTextureRef tex(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(g_clef.texture)));
    dl->AddImage(tex, {x0, y0}, {x0 + w, y0 + h}, {0.0f, 0.0f}, {1.0f, 1.0f}, u32(colour));
}

void release_clef() {
    if (g_clef.texture != nullptr) {
        gfx::destroy_texture(g_clef.texture);
        g_clef.texture = nullptr;
    }
    // Made again at the next draw, also for a new renderer that has the old one's address.
    g_clef.px_w = 0;
    g_clef.px_h = 0;
}

} // namespace etude::app::ui
