// SPDX-License-Identifier: GPL-3.0-or-later
// Bar style Dock: a floating dock in the middle of the top row, play the one filled button, a
// hairline between the groups. The controls are every style's (bar_lineup.hpp).
#include "bar.hpp"
#include "bar_lineup.hpp"
#include "bar_metrics.hpp"
#include "tempo.hpp"

#include <algorithm>

namespace etude::app::ui {

namespace {

class DockBar final : public BarLook {
  public:
    float width(const Kit& k, const SongView& song, BarForm form, bool pin) const override {
        return k.u * bar_metrics::dock_plan(form, {song.has_song, song.beat_dotted, song.has_accompaniment}, pin).width();
    }
    bool centred() const override { return bar_metrics::centred(BarStyle::Dock); }
    void draw(Kit& k, UiModel& model, SongPort& song, const Frame& f) override;
    bool holds_bar() const override { return tempo_.open() || accomp_.open(); }
    void close_popovers() override {
        tempo_.close();
        accomp_.close();
    }
    bool wants_open(Page* p) override { return request_.take(p) || tempo_.wants_open(p); }

  private:
    TempoControls tempo_;
    AccompanimentMenu accomp_;
    OpenRequest request_;
};

// A hairline between two groups, the dock's height.
class Dividers final : public LineupFrame {
  public:
    Dividers(const Kit& k, ImDrawList* dl, float cy) : k_(k), dl_(dl), cy_(cy) {}
    void divider(float x) override {
        dl_->AddLine({x, cy_ - k_.s(tok::kS3)}, {x, cy_ + k_.s(tok::kS3)}, u32(col::kHairline));
    }

  private:
    const Kit& k_;
    ImDrawList* dl_;
    float cy_;
};

void DockBar::draw(Kit& k, UiModel& model, SongPort& song, const Frame& f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = f.row_max.y - f.row_min.y;
    const float slide = (1.0f - f.reveal) * -k.s(tok::kS3);
    const float dock_h = k.s(bar_metrics::dock_height(h / k.u));
    const float face = std::min(k.s(tok::kFace), dock_h - k.s(bar_metrics::kDockFaceInset));
    const float dx = f.bar_min.x;
    const float dock_w = f.bar_max.x - f.bar_min.x;
    const float dy = f.row_min.y + (h - dock_h) * 0.5f + slide;
    overlay_card(k, dl, {dx, dy}, {dx + dock_w, dy + dock_h}, k.s(tok::kRadiusCard), col::kSurface,
                 ImDrawFlags_RoundCornersAll, 1.0f);
    // Every face stays on the dock.
    const FaceBand band(k, dy, dy + dock_h);
    LineupLook look;
    look.cells = bar_metrics::kDock;
    look.song_face = look.switch_face = look.chip_face = face;
    look.prominent_play = true;
    Dividers frame(k, dl, dy + dock_h * 0.5f);
    draw_lineup(k, model, song, f, look, dx, f.row_min.y + slide, h, tempo_, accomp_,
                request_, frame);
    tempo_.popover(k, model, song, f, std::max(dy + dock_h, f.band_bottom) + k.s(tok::kS2),
                   !bar_has_metronome_toggle(f.form));
    accomp_.popover(k, model, song, f, std::max(dy + dock_h, f.band_bottom) + k.s(tok::kS2));
}

} // namespace

std::unique_ptr<BarLook> make_dock_bar() { return std::make_unique<DockBar>(); }

} // namespace etude::app::ui
