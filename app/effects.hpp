// SPDX-License-Identifier: GPL-3.0-or-later
// The note styles and effects on top of Mesh: the textures they share (made
// once), the painter that turns an etude::StyleRecipe into a few batched layers, the hit effects'
// shapes and the drifting background light. Nothing here allocates per frame once the buffers have
// grown to the scene's size, and nothing does per-pixel work after construction.
#pragma once

#include "mesh.hpp"
#include "etude/hit_effects.hpp"
#include "etude/note_style.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

namespace etude::app {

// Two small textures, made once per renderer: a soft glow sprite (white, alpha falling off from
// its centre lines, so it works both as a whole-texture blob and as a nine-slice around a
// rectangle) and a tiling grain for the frosted style.
class EffectTextures {
  public:
    static constexpr int kSize = 64;

    explicit EffectTextures(SDL_Renderer* renderer);
    ~EffectTextures();
    // Destroys the textures (while their renderer is alive), and makes them again for `renderer`.
    void drop();
    void make(SDL_Renderer* renderer);
    EffectTextures(const EffectTextures&) = delete;
    EffectTextures& operator=(const EffectTextures&) = delete;

    SDL_Texture* glow() const { return glow_; }
    SDL_Texture* grain() const { return grain_; }

  private:
    SDL_Texture* glow_ = nullptr;
    SDL_Texture* grain_ = nullptr;
};

// `c` pushed away from its grey by `s` (1 keeps it).
SDL_FColor saturated(SDL_FColor c, float s);

// Paints note blocks and lit keys from one recipe, in layers that each become one draw call:
// body, inner shade and glow, extras, grain, rim. Shapes in one painter must not overlap each
// other (blocks in their lanes, white keys, black keys); a layer drawn over another shape's body
// would show. begin() starts a batch, add() queues a shape, draw() sends it.
class StylePainter {
  public:
    void begin(const StyleRecipe& recipe, float unit);
    // `fill` is the note colour at the far end, `key_end` at the key end (held notes are lighter
    // there), both with any press flash applied; `alpha` fades the whole shape. `underlay`, when
    // its alpha is above 0, is painted opaque under a see-through body, so a lit key shows the
    // style as its block does over the dark background.
    void add(const SDL_FRect& rect, float radius, SDL_FColor fill, SDL_FColor key_end, float alpha,
             SDL_FColor underlay = {0.0f, 0.0f, 0.0f, 0.0f});
    // A beat line across a block (`line` is its full rectangle): dark, or the note's colour
    // lightened, as the recipe says; drawn over the body with the rim.
    void add_beat_line(const SDL_FRect& line, SDL_FColor note, float alpha);
    void draw(SDL_Renderer* renderer, const EffectTextures& textures) const;
    // Room for `shapes` shapes of the fullest style in every layer, so adding them allocates
    // nothing. Never shrinks.
    void reserve(std::size_t shapes);

  private:
    StyleRecipe recipe_;
    float unit_ = 1.0f;
    Mesh body_;
    Mesh inner_;
    Mesh extras_;
    Mesh grain_;
    Mesh rim_;
};

// Adds one hit effect shape (core hit_effects.hpp) in `colour`, its alpha and whitening applied:
// soft blobs and spark streaks to `soft` (drawn with EffectTextures::glow()), rings to `lines`
// (drawn untextured). Only a ring's upper half is added: the hit line is its lower edge.
void add_hit_shape(const HitShape& shape, SDL_FColor colour, float unit, Mesh& soft, Mesh& lines);

// The motion background: a few large, faint blobs of coloured light drifting slowly over `area`,
// one glow-sprite quad each. `seconds` is a steady clock.
void build_motion_background(Mesh& mesh, const SDL_FRect& area, double seconds);

} // namespace etude::app
