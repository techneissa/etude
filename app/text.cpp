// SPDX-License-Identifier: GPL-3.0-or-later
#include "text.hpp"

#include "frame_log.hpp"
#include "frame_picture.hpp"
#include "gfx.hpp"
#include "music_font.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>

namespace etude::app {

// Generated from the font file by cmake/EmbedFile.cmake.
std::span<const unsigned char> embedded_font();

namespace {
// Font sizes are rounded to this step so a slow window resize does not re-rasterise every frame.
constexpr float kSizeStep = 2.0f;
} // namespace

TextSlot::~TextSlot() {
    if (epoch != gfx::epoch()) {
        return; // the renderer it was made for took its textures with it
    }
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
    }
    if (outline != nullptr) {
        SDL_DestroyTexture(outline);
    }
}

Text::Text(SDL_Renderer* renderer) : renderer_(renderer) {
    if (!TTF_Init()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed, using the debug font: %s",
                    SDL_GetError());
        return;
    }
    const auto data = embedded_font();
    SDL_IOStream* io = SDL_IOFromConstMem(data.data(), data.size());
    font_ = io != nullptr ? TTF_OpenFontIO(io, true, 32.0f) : nullptr;
    if (font_ == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "font failed to open, using the debug font: %s",
                    SDL_GetError());
        return;
    }
    // Atkinson has no flat or sharp sign: the music font answers for U+266D and U+266F.
    const auto music = music_font_for_em();
    SDL_IOStream* music_io = SDL_IOFromConstMem(music.data(), music.size());
    music_font_ = music_io != nullptr ? TTF_OpenFontIO(music_io, true, 32.0f) : nullptr;
    if (music_font_ == nullptr || !TTF_AddFallbackFont(font_, music_font_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "the music font failed to load: %s",
                    SDL_GetError());
    }
}

Text::~Text() {
    if (font_ != nullptr) {
        TTF_CloseFont(font_); // the fallback leaves its list with it
    }
    if (music_font_ != nullptr) {
        TTF_CloseFont(music_font_);
    }
    TTF_Quit();
}

// A slot made under an earlier renderer holds textures that are freed already: forget them and
// rasterise the same text again, so a cache above (a laid-out label, a chord name) that never calls
// prepare() again still draws it. The slot keeps what it was made for.
void Text::refresh_stale(TextSlot& slot) {
    if (slot.epoch == gfx::epoch()) {
        return;
    }
    const std::string text = std::move(slot.text);
    const float size = slot.size;
    const bool bold = slot.bold;
    const int outline = slot.outline_px;
    const float fit_size = slot.fit_size;
    const float fit_width = slot.fit_width;
    const bool hidden = slot.hidden;
    slot.texture = nullptr;
    slot.outline = nullptr;
    slot.text.clear();
    slot.size = 0.0f;
    slot.outline_px = 0;
    slot.width = 0.0f;
    slot.height = 0.0f;
    slot.epoch = gfx::epoch();
    if (!text.empty() && size > 0.0f) {
        prepare_exact(slot, text, size, bold);
        prepare_outline(slot, outline);
    }
    slot.fit_size = fit_size;
    slot.fit_width = fit_width;
    slot.hidden = hidden;
}

void Text::set_font(float size, bool bold) {
    // A fallback is used at its own size and style, so it is set with the main font's; the music
    // font's signs are small for their em, so it is drawn `kSignScale` bigger (music_font.hpp),
    // as the UI's ImGui fonts draw them.
    const TTF_FontStyleFlags style = bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
    TTF_SetFontSize(font_, size);
    TTF_SetFontStyle(font_, style);
    if (music_font_ != nullptr) {
        TTF_SetFontSize(music_font_, size * kSignScale);
        TTF_SetFontStyle(music_font_, style);
    }
}

SDL_FPoint Text::prepare(TextSlot& slot, std::string_view text, float size, bool bold) {
    return prepare_exact(slot, text, std::max(kSizeStep, std::round(size / kSizeStep) * kSizeStep),
                         bold);
}

SDL_FPoint Text::measure(std::string_view text, float size, bool bold) {
    size = std::max(1.0f, std::round(size));
    if (font_ == nullptr || text.empty()) {
        return {static_cast<float>(text.size()) * size, text.empty() ? 0.0f : size};
    }
    set_font(size, bold);
    int w = 0;
    int h = 0;
    if (!TTF_GetStringSize(font_, text.data(), text.size(), &w, &h)) {
        return {0.0f, 0.0f};
    }
    return {static_cast<float>(w), static_cast<float>(h)};
}

Text::VerticalMetrics Text::vertical_metrics() {
    VerticalMetrics m;
    if (font_ == nullptr) {
        return m;
    }
    constexpr float kRefPx = 64.0f;
    set_font(kRefPx, false);
    int max_y = 0;
    if (TTF_GetGlyphMetrics(font_, 'H', nullptr, nullptr, nullptr, &max_y, nullptr) && max_y > 0) {
        m.cap_em = static_cast<float>(max_y) / kRefPx;
        m.cap_top_em =
            std::max(0.0f, static_cast<float>(TTF_GetFontAscent(font_) - max_y) / kRefPx);
    }
    m.line_em = std::max(1.0f, static_cast<float>(TTF_GetFontHeight(font_)) / kRefPx);
    return m;
}

SDL_FPoint Text::prepare_exact(TextSlot& slot, std::string_view text, float size, bool bold) {
    refresh_stale(slot);
    size = std::max(1.0f, std::round(size));
    if (slot.size == size && slot.bold == bold && slot.text == text) {
        return {slot.width, slot.height};
    }
    slot.text.assign(text);
    slot.size = size;
    slot.bold = bold;
    slot.fit_size = 0.0f;
    slot.hidden = false;
    if (slot.texture != nullptr) {
        SDL_DestroyTexture(slot.texture);
        slot.texture = nullptr;
    }
    if (slot.outline != nullptr) {
        SDL_DestroyTexture(slot.outline);
        slot.outline = nullptr;
    }
    slot.outline_px = 0;
    if (font_ == nullptr || text.empty()) {
        const float scale = size / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        slot.width = static_cast<float>(text.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
        slot.height = text.empty() ? 0.0f : size;
        return {slot.width, slot.height};
    }
    const FrameDetailScope timing(FrameDetail::Text);
    set_font(size, bold);
    // White glyphs, so the colour and alpha come from the texture's modulation at draw time.
    SDL_Surface* surface =
        TTF_RenderText_Blended(font_, text.data(), text.size(), {255, 255, 255, 255});
    if (surface != nullptr) {
        slot.texture = SDL_CreateTextureFromSurface(renderer_, surface);
        slot.width = static_cast<float>(surface->w);
        slot.height = static_cast<float>(surface->h);
        SDL_DestroySurface(surface);
    }
    if (slot.texture == nullptr) {
        slot.width = 0.0f;
        slot.height = 0.0f;
    }
    return {slot.width, slot.height};
}

bool Text::is_prepared(const TextSlot& slot, std::string_view text, float size, int outline_px,
                       bool bold) {
    size = std::max(1.0f, std::round(size));
    if (slot.epoch != gfx::epoch() || slot.size != size || slot.bold != bold ||
        slot.text != text) {
        return false;
    }
    return outline_px < 0 ||
           (slot.outline_px == outline_px && (slot.outline != nullptr || outline_px == 0));
}

SDL_FPoint Text::prepare_fit(TextSlot& slot, std::string_view text, float size, float max_width,
                             float min_size) {
    refresh_stale(slot);
    if (slot.fit_size == size && slot.fit_width == max_width && slot.text == text) {
        return slot.hidden ? SDL_FPoint{0.0f, 0.0f} : SDL_FPoint{slot.width, slot.height};
    }
    SDL_FPoint extent = prepare(slot, text, size);
    if (extent.x > max_width && extent.x > 0.0f) {
        const float fitted = size * max_width / extent.x;
        // Rounding the size can overshoot by a step, so aim one step lower.
        extent = fitted - kSizeStep >= min_size ? prepare(slot, text, fitted - kSizeStep)
                                                : SDL_FPoint{0.0f, 0.0f};
    }
    slot.fit_size = size;
    slot.fit_width = max_width;
    slot.hidden = extent.x <= 0.0f && !text.empty();
    return slot.hidden ? SDL_FPoint{0.0f, 0.0f} : extent;
}

void Text::draw(TextSlot& slot, float x, float y, Align align, SDL_FColor colour) {
    refresh_stale(slot);
    if (slot.text.empty() || slot.hidden) {
        return;
    }
    const float left = align == Align::Centre ? x - slot.width * 0.5f : x;
    if (slot.texture != nullptr) {
        SDL_SetTextureColorModFloat(slot.texture, colour.r, colour.g, colour.b);
        SDL_SetTextureAlphaModFloat(slot.texture, colour.a);
        const SDL_FRect dst{std::round(left), std::round(y), slot.width, slot.height};
        picture::render_texture(renderer_, slot.texture, dst, colour);
        return;
    }
    // Debug-font fallback.
    const float scale = slot.size / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    if (picture::on()) {
        picture::mix(std::hash<std::string>{}(slot.text));
        picture::rect(nullptr, {left, y, scale, scale}, colour);
    }
    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_SetRenderDrawColorFloat(renderer_, colour.r, colour.g, colour.b, colour.a);
    SDL_RenderDebugText(renderer_, left / scale, y / scale, slot.text.c_str());
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
}

void Text::prepare_outline(TextSlot& slot, int px) {
    refresh_stale(slot);
    px = std::max(px, 0);
    if (slot.outline_px == px && (slot.outline != nullptr || px == 0)) {
        return;
    }
    if (slot.outline != nullptr) {
        SDL_DestroyTexture(slot.outline);
        slot.outline = nullptr;
    }
    slot.outline_px = px;
    if (px == 0 || font_ == nullptr || slot.texture == nullptr || slot.text.empty()) {
        return;
    }
    const FrameDetailScope timing(FrameDetail::Text);
    set_font(slot.size, slot.bold);
    TTF_SetFontOutline(font_, px);
    if (music_font_ != nullptr) {
        TTF_SetFontOutline(music_font_, px);
    }
    SDL_Surface* surface =
        TTF_RenderText_Blended(font_, slot.text.data(), slot.text.size(), {255, 255, 255, 255});
    TTF_SetFontOutline(font_, 0);
    if (music_font_ != nullptr) {
        TTF_SetFontOutline(music_font_, 0);
    }
    if (surface != nullptr) {
        slot.outline = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_DestroySurface(surface);
    }
}

void Text::draw_outline(TextSlot& slot, float x, float y, Align align, SDL_FColor outline) {
    refresh_stale(slot);
    if (slot.outline != nullptr && !slot.text.empty() && !slot.hidden) {
        // The outlined glyphs are `outline_px` larger on every side.
        const float left = align == Align::Centre ? x - slot.width * 0.5f : x;
        const auto px = static_cast<float>(slot.outline_px);
        float w = 0.0f;
        float h = 0.0f;
        SDL_GetTextureSize(slot.outline, &w, &h);
        SDL_SetTextureColorModFloat(slot.outline, outline.r, outline.g, outline.b);
        SDL_SetTextureAlphaModFloat(slot.outline, outline.a);
        const SDL_FRect dst{std::round(left) - px, std::round(y) - px, w, h};
        picture::render_texture(renderer_, slot.outline, dst, outline);
    }
}

void Text::draw_stand_in(TextSlot& slot, SDL_FRect box, SDL_FColor colour) {
    refresh_stale(slot);
    if (slot.texture != nullptr && !slot.text.empty() && !slot.hidden) {
        SDL_SetTextureColorModFloat(slot.texture, colour.r, colour.g, colour.b);
        SDL_SetTextureAlphaModFloat(slot.texture, colour.a);
        const SDL_FRect dst{std::round(box.x), std::round(box.y), box.w, box.h};
        picture::render_texture(renderer_, slot.texture, dst, colour);
    }
}

void Text::draw_stand_in_outline(TextSlot& slot, SDL_FRect box, float outline_px,
                                 SDL_FColor outline) {
    refresh_stale(slot);
    if (slot.outline != nullptr && !slot.text.empty() && !slot.hidden) {
        SDL_SetTextureColorModFloat(slot.outline, outline.r, outline.g, outline.b);
        SDL_SetTextureAlphaModFloat(slot.outline, outline.a);
        const SDL_FRect dst{std::round(box.x) - outline_px, std::round(box.y) - outline_px,
                            box.w + 2.0f * outline_px, box.h + 2.0f * outline_px};
        picture::render_texture(renderer_, slot.outline, dst, outline);
    }
}

void Text::draw_outlined(TextSlot& slot, float x, float y, Align align, SDL_FColor fill,
                         SDL_FColor outline) {
    draw_outline(slot, x, y, align, outline);
    draw(slot, x, y, align, fill);
}

void Text::draw_outlined_gradient(TextSlot& slot, float x, float y, Align align, SDL_FColor top,
                                  SDL_FColor bottom, SDL_FColor outline) {
    draw_outline(slot, x, y, align, outline);
    if (slot.texture == nullptr || slot.text.empty() || slot.hidden) {
        draw(slot, x, y, align, top);
        return;
    }
    const float left = std::round(align == Align::Centre ? x - slot.width * 0.5f : x);
    const float up = std::round(y);
    SDL_SetTextureColorModFloat(slot.texture, 1.0f, 1.0f, 1.0f);
    SDL_SetTextureAlphaModFloat(slot.texture, 1.0f);
    const SDL_Vertex quad[4] = {
        {{left, up}, top, {0.0f, 0.0f}},
        {{left + slot.width, up}, top, {1.0f, 0.0f}},
        {{left + slot.width, up + slot.height}, bottom, {1.0f, 1.0f}},
        {{left, up + slot.height}, bottom, {0.0f, 1.0f}},
    };
    static constexpr int kIndices[6] = {0, 1, 2, 0, 2, 3};
    picture::render_geometry(renderer_, slot.texture, quad, 4, kIndices, 6);
}

} // namespace etude::app
