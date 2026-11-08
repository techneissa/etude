// SPDX-License-Identifier: GPL-3.0-or-later
#include "medal_atlas.hpp"
#include "../crash_thread.hpp"

#include "../gfx.hpp"

#include <algorithm>
#include <cstring>

namespace etude::app::ui {

namespace {

ImTextureRef texture_ref(SDL_Texture* t) {
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(t)));
}

// The flat stand-in's colour for each material, until the atlas is there.
constexpr std::uint32_t kFlat[etude::kMaterialCount] = {0x8A5A2B, 0x3E424B, 0xB87238, 0xC8CFDA,
                                                          0xF2BC38, 0xC4D2EC, 0x8FD6F6, 0x1C1626};

} // namespace

MedalAtlas::~MedalAtlas() {
    if (worker_.joinable()) {
        worker_.join();
    }
    if (texture_ != nullptr) {
        gfx::destroy_texture(texture_);
    }
}

void MedalAtlas::drop_renderer() {
    if (texture_ != nullptr) {
        gfx::destroy_texture(texture_);
        texture_ = nullptr;
    }
}

// Every material large in the top row, small in the row under it.
void MedalAtlas::render() {
    const etude::MedalFrame large = etude::medal_frame(etude::MedalSize::Large);
    const etude::MedalFrame small = etude::medal_frame(etude::MedalSize::Small);
    width_ = kColumns * large.width;
    height_ = large.height + small.height;
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U, 0);
    const auto put = [&](const etude::MedalPixels& m, int x0, int y0) {
        for (int y = 0; y < m.height; ++y) {
            std::memcpy(&pixels_[(static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x0)) * 4U],
                        &m.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) * 4U],
                        static_cast<std::size_t>(m.width) * 4U);
        }
    };
    for (int i = 0; i < kColumns; ++i) {
        const auto material = static_cast<etude::Material>(i);
        put(etude::render_medal(material, etude::MedalSize::Large), i * large.width, 0);
        put(etude::render_medal(material, etude::MedalSize::Small), i * large.width, large.height);
    }
}

void MedalAtlas::step() {
    if (texture_ != nullptr || renderer_ == nullptr) {
        return;
    }
    if (!started_) {
        started_ = true;
        worker_ = spawn_thread([this] {
            render();
            done_.store(true);
        });
        return;
    }
    if (!done_.load()) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    texture_ = gfx::create_texture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width_, height_);
    if (texture_ == nullptr) {
        return;
    }
    gfx::update_texture(texture_, pixels_.data(), width_ * 4);
    gfx::set_texture_blend(texture_, SDL_BLENDMODE_BLEND);
    gfx::set_texture_linear(texture_);
}

void MedalAtlas::draw(ImDrawList* dl, etude::Material material, bool large, ImVec2 centre, float d,
                      ImVec4 tint) const {
    const etude::MedalFrame f = etude::medal_frame(large ? etude::MedalSize::Large : etude::MedalSize::Small);
    const float scale = d / (2.0f * f.radius);
    const ImU32 tinted = ImGui::ColorConvertFloat4ToU32(tint);
    if (texture_ == nullptr) { // until the atlas is there: a flat disc
        const std::uint32_t c = kFlat[static_cast<std::size_t>(material)];
        const ImVec4 flat{static_cast<float>((c >> 16) & 0xFF) / 255.0f * tint.x,
                          static_cast<float>((c >> 8) & 0xFF) / 255.0f * tint.y,
                          static_cast<float>(c & 0xFF) / 255.0f * tint.z, tint.w};
        dl->AddCircleFilled(centre, d * 0.5f, ImGui::ColorConvertFloat4ToU32(flat));
        return;
    }
    const etude::MedalFrame big = etude::medal_frame(etude::MedalSize::Large);
    const float x0 = static_cast<float>(static_cast<int>(material) * big.width);
    const float y0 = large ? 0.0f : static_cast<float>(big.height);
    const ImVec2 uv0{x0 / static_cast<float>(width_), y0 / static_cast<float>(height_)};
    const ImVec2 uv1{(x0 + static_cast<float>(f.width)) / static_cast<float>(width_),
                     (y0 + static_cast<float>(f.height)) / static_cast<float>(height_)};
    const ImVec2 p0{centre.x - f.centre_x * scale, centre.y - f.centre_y * scale};
    const ImVec2 p1{p0.x + static_cast<float>(f.width) * scale, p0.y + static_cast<float>(f.height) * scale};
    dl->AddImage(texture_ref(texture_), p0, p1, uv0, uv1, tinted);
}

} // namespace etude::app::ui
