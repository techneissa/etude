// SPDX-License-Identifier: GPL-3.0-or-later
// Text drawing with the embedded font through SDL3_ttf. Each piece of text keeps its own cached
// texture in a TextSlot, so a label is only rasterised again when its text or size changes. If
// the font cannot be opened, text falls back to SDL's built-in 8-pixel debug font, scaled up.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <string_view>

typedef struct TTF_Font TTF_Font;

namespace etude::app {

// One cached line of text. Owned by the caller, one per place text is drawn.
struct TextSlot {
    TextSlot() = default;
    TextSlot(const TextSlot&) = delete;
    TextSlot& operator=(const TextSlot&) = delete;
    ~TextSlot();

    std::string text;
    float size = 0.0f; // pixel height the texture was made for
    bool bold = false;
    SDL_Texture* texture = nullptr;
    SDL_Texture* outline = nullptr; // the same text drawn with an outline, see prepare_outline()
    // gfx::epoch() when the slot last saw a renderer. Past a replacement its textures are freed
    // already: the next call on it forgets them and rasterises the same text again, instead of
    // destroying them.
    std::uint32_t epoch = 0;
    int outline_px = 0;
    float width = 0.0f; // of the drawn text, in pixels
    float height = 0.0f;
    std::uint64_t used = 0; // the frame it was last drawn in, for a cache that drops the oldest
    // Inputs of the last prepare_fit(), so an unchanged label is not measured again.
    float fit_size = 0.0f;
    float fit_width = 0.0f;
    bool hidden = false; // prepare_fit() found no readable size
};

enum class Align { Left, Centre };

class Text {
  public:
    explicit Text(SDL_Renderer* renderer);
    ~Text();
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;

    // False when the font could not be opened and the debug font is used.
    bool has_font() const { return font_ != nullptr; }

    // The renderer was replaced (gfx::replace_renderer): later textures are made for `renderer`.
    void set_renderer(SDL_Renderer* renderer) { renderer_ = renderer; }

    // Updates `slot` for `text` at `size` pixels and returns its width and height. Only rasterises
    // when the text, size or weight changed. `bold` asks for the font's synthesized bold style.
    SDL_FPoint prepare(TextSlot& slot, std::string_view text, float size, bool bold = false);

    // Like prepare(), but at exactly `size` pixels (rounded to a whole pixel) instead of the
    // nearest step, for text whose size was planned to the pixel.
    SDL_FPoint prepare_exact(TextSlot& slot, std::string_view text, float size, bool bold = false);

    // True when prepare_exact() of the same arguments, then prepare_outline() of `outline_px`,
    // would rasterise nothing: the slot already holds that text, size and outline. A negative
    // `outline_px` accepts whatever outline the slot has.
    static bool is_prepared(const TextSlot& slot, std::string_view text, float size, int outline_px,
                            bool bold = false);

    // The width and height `text` takes at `size` pixels in the face prepare() uses, from the
    // font's own metrics (TTF_GetStringSize). Nothing is rasterised; call it when a label
    // changes, not every frame. Without a font, the debug font's size.
    SDL_FPoint measure(std::string_view text, float size, bool bold = false);

    // The face's vertical metrics as shares of the font size, from the font itself: how tall a
    // capital is, how far its top is below the top of a rendered line, and the line's height.
    struct VerticalMetrics {
        float cap_em = 0.7f;
        float cap_top_em = 0.2f;
        float line_em = 1.2f;
    };
    VerticalMetrics vertical_metrics();

    // Like prepare(), but shrinks the text to fit `max_width`. When that would go below
    // `min_size` the slot is hidden and draw() does nothing. Cached on its inputs.
    SDL_FPoint prepare_fit(TextSlot& slot, std::string_view text, float size, float max_width,
                           float min_size);

    // Draws the prepared slot with its top edge at `y`, left-aligned at `x` or centred on it.
    void draw(TextSlot& slot, float x, float y, Align align, SDL_FColor colour);

    // Adds a `px`-wide outline copy of the prepared slot (cached until the text changes). Call
    // after prepare().
    void prepare_outline(TextSlot& slot, int px);

    // Draws only the outline of the prepared slot (see prepare_outline), at the position draw()
    // would put the text. Nothing when the slot has no outline.
    void draw_outline(TextSlot& slot, float x, float y, Align align, SDL_FColor outline);

    // A slot made at another size standing in for one not made yet: the fill is stretched to the
    // rectangle `box`, and the outline copy to the same rectangle grown by `outline_px` on every
    // side, so nothing is drawn outside the box and its outline.
    void draw_stand_in(TextSlot& slot, SDL_FRect box, SDL_FColor colour);
    void draw_stand_in_outline(TextSlot& slot, SDL_FRect box, float outline_px, SDL_FColor outline);

    // Like draw(), with a thin outline in `outline` under the fill: a stroke around the glyphs,
    // no box behind them. Falls back to draw() when the slot has no outline.
    void draw_outlined(TextSlot& slot, float x, float y, Align align, SDL_FColor fill,
                       SDL_FColor outline);

    // Like draw_outlined(), the fill shading from `top` at the top edge of the text to `bottom` at
    // its foot (one textured quad with a colour at each corner: nothing allocated).
    void draw_outlined_gradient(TextSlot& slot, float x, float y, Align align, SDL_FColor top,
                                SDL_FColor bottom, SDL_FColor outline);

  private:
    SDL_Renderer* renderer_;
    void refresh_stale(TextSlot& slot);
    // The font's size and style, and its fallback's.
    void set_font(float size, bool bold);
    TTF_Font* font_ = nullptr;
    TTF_Font* music_font_ = nullptr; // the flat and sharp signs, a fallback of font_
};

} // namespace etude::app
