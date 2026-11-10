// SPDX-License-Identifier: GPL-3.0-or-later
// The medals as one texture: the eight materials, large (with the ribbon) and small, drawn once by
// core's medal_art on a worker thread the first time a screen wants one, uploaded when ready, and only
// drawn from after that: no medal is ever painted per frame. Until the texture is there a flat disc
// in the material's colour stands in, so a screen never waits. Owned by the Ui; the renderer may be
// replaced (drop / adopt).
#pragma once

#include "etude/medal_art.hpp"

#include <SDL3/SDL_render.h>
#include <imgui.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace etude::app::ui {

class MedalAtlas {
  public:
    explicit MedalAtlas(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~MedalAtlas();
    MedalAtlas(const MedalAtlas&) = delete;
    MedalAtlas& operator=(const MedalAtlas&) = delete;

    // The texture is let go (the renderer is being replaced) and given a new renderer; the pixels
    // are kept, so nothing is drawn again.
    void drop_renderer();
    void adopt_renderer(SDL_Renderer* renderer) { renderer_ = renderer; }

    // Starts the render on first use; uploads the texture once it is done. Call every frame that
    // may draw a medal (cheap when there is nothing to do).
    void step();
    bool ready() const { return texture_ != nullptr; }

    // Draws the medal of `material`, the disc `d` across with its centre at `centre`; `large` has the
    // ribbon above the disc. `tint` multiplies the colours (a locked medal is greyed and faint).
    void draw(ImDrawList* dl, etude::Material material, bool large, ImVec2 centre, float d,
              ImVec4 tint = {1, 1, 1, 1}) const;

  private:
    void render();
    static constexpr int kColumns = static_cast<int>(etude::kMaterialCount);

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    std::thread worker_;
    std::atomic<bool> done_{false};
    bool started_ = false;
    std::vector<std::uint8_t> pixels_; // the atlas, kept for a new renderer
    int width_ = 0;
    int height_ = 0;
};

} // namespace etude::app::ui
