// SPDX-License-Identifier: GPL-3.0-or-later
// Sheet music in the library's preview: the score of a song that is not open (a library song's or
// a catalogue song's, read for its preview), engraved by a sheet worker of its own, started the
// first time it is asked for. Its systems are drawn small on the paper, from the one the cursor is
// on, with the cursor following the preview's clock. At most one system's pictures are made a
// frame, and nothing is engraved, made or drawn while the sheet is not shown.
#pragma once

#include "../gfx.hpp"
#include "sheet_worker.hpp"

#include <imgui.h>
#include <SDL3/SDL_render.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app {

class SheetPreview {
  public:
    explicit SheetPreview(SDL_Renderer* renderer);
    ~SheetPreview();
    SheetPreview(const SheetPreview&) = delete;
    SheetPreview& operator=(const SheetPreview&) = delete;

    enum class Status : std::uint8_t { None, Waiting, Ready, NoSheet };

    // The song to show, engraved `width_px` wide with `staff_space_px` between staff lines. The
    // same song at about the same size changes nothing; another song or size starts again.
    void want(const std::shared_ptr<const LoadedSong>& song, float width_px, float staff_space_px);
    // Takes the worker's results and makes at most one system's pictures. Each frame it is shown.
    void update();
    Status status() const { return status_; }
    // Why there is no sheet music, when NoSheet.
    std::string_view reason() const { return reason_; }
    // Draws the paper over [a, b] and, while Ready, the systems from the cursor's on, as many as
    // fit, and the cursor at `opening_s` seconds after the song's first note.
    void draw(ImDrawList* dl, ImVec2 a, ImVec2 b, double opening_s, float rounding, float scale);
    // Forgets the song and its pictures.
    void clear();
    // The renderer is being replaced: the pictures go while the old one is alive, and are asked
    // again once the new one is adopted.
    void drop_renderer() { drop_pictures(); }
    void adopt_renderer(SDL_Renderer* renderer) { renderer_ = renderer; }

  private:
    struct Picture {
        int system = -1;
        SDL_Texture* base = nullptr;
        SDL_Texture* notes = nullptr;
        int width = 0;
        int height = 0;
    };
    void drop_pictures();
    const Picture* picture_of(int system) const;

    SDL_Renderer* renderer_;
    std::unique_ptr<SheetWorker> worker_;
    std::shared_ptr<const LoadedSong> song_;
    std::uint32_t serial_ = 0;
    std::uint64_t generation_ = 0;
    EngraveOptions options_;
    Status status_ = Status::None;
    std::string reason_;
    SheetLayout layout_;
    SheetMap map_;
    double first_note_s_ = 0.0;
    // A new width waits until it has held still (a window being dragged is not engraved at
    // every size it passes).
    float resize_px_ = -1.0f;
    std::chrono::steady_clock::time_point resize_since_{};
    std::vector<Picture> pictures_;
    std::vector<int> wanted_;
    int top_ = 0;      // the system drawn first
    int failures_ = 0; // pictures that came back empty, in a row
};

} // namespace etude::app
