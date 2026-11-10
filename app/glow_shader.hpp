// SPDX-License-Identifier: GPL-3.0-or-later
// The Neon glow as a shader (app/shaders/neon_glow.*, built by scripts/build-shaders.sh): the glow
// nine-slice mesh drawn with a fragment shader that adds a tight, white-hot core to the soft glow
// sprite. On the gpu renderer it is an SDL GPU render state around the same draw call; on the
// opengl and opengles2 renderers it is a pass of our own, run after SDL_FlushRenderer, that saves
// and restores the GL state it changes. A renderer of any other kind has none, and the mesh is
// drawn as before. Part of the drawing layer: nothing outside it knows which kind it is.
#pragma once

#include "mesh.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_render.h>

namespace etude::app {

class GlowShader {
  public:
    virtual ~GlowShader() = default;
    // Draws `mesh` additively over the glow sprite `glow`, clipped as the renderer's clip rect
    // says. False (nothing drawn, SDL_GetError says why) when the pass cannot: that is a failure,
    // not an empty mesh, and the caller draws the plain mesh and drops the shader for the session.
    virtual bool draw(SDL_Renderer* renderer, const Mesh& mesh, SDL_Texture* glow) = 0;
};

// The shader for `renderer`, made on first use; null when the renderer has none, or it failed to
// make it (one warning is logged). Start-up and frame thread only.
GlowShader* glow_shader(SDL_Renderer* renderer);
// Gives up the shader for the rest of the session after a draw failed: logs one warning with
// SDL's error, frees it, and `glow_shader` returns null from then on.
void drop_glow_shader(SDL_Renderer* renderer);
// Frees what glow_shader made for `renderer`: call before the renderer is destroyed.
void release_glow_shader(SDL_Renderer* renderer);

// The mesh of a layer of glow sprites. `use_shader(true)` draws it through the glow shader when
// the renderer has one; otherwise, and when it is off, it is a Mesh.
class GlowLayer : public Mesh {
  public:
    void use_shader(bool on) { shader_ = on; }
    void draw(SDL_Renderer* renderer, SDL_BlendMode mode, SDL_Texture* texture) const;

  private:
    bool shader_ = false;
};

} // namespace etude::app
