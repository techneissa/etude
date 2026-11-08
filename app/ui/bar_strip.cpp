// SPDX-License-Identifier: GPL-3.0-or-later
// Bar style Strip: a full-width translucent strip across the top, the controls against the gear
// with a hairline between their groups. The controls are every style's (bar_lineup.hpp).
#include "bar.hpp"
#include "bar_lineup.hpp"
#include "bar_metrics.hpp"
#include "tempo.hpp"

namespace etude::app::ui {

namespace {

class StripBar final : public BarLook {
  public:
    float width(const Kit& k, const SongView& song, BarForm form, bool pin) const override {
        return k.u * bar_metrics::strip_plan(form, {song.has_song, song.beat_dotted, song.has_accompaniment}, pin).width();
    }
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

// A hairline between two groups.
class Dividers final : public LineupFrame {
  public:
    Dividers(const Kit& k, ImDrawList* dl, float cy) : k_(k), dl_(dl), cy_(cy) {}
    void divider(float x) override {
        dl_->AddLine({x, cy_ - k_.s(tok::kS3)}, {x, cy_ + k_.s(tok::kS3)}, u32(col::kHairline),
                     1.0f);
    }

  private:
    const Kit& k_;
    ImDrawList* dl_;
    float cy_;
};

void StripBar::draw(Kit& k, UiModel& model, SongPort& song, const Frame& f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = f.row_max.y - f.row_min.y;
    const float slide = (1.0f - f.reveal) * -k.s(tok::kS3);
    // The strip: the top row; translucent, a hairline under it, no hard band.
    const float bottom = f.row_max.y + slide;
    dl->AddRectFilledMultiColor(
        {0.0f, f.row_min.y + slide}, {f.display.x, bottom}, u32(with_alpha(col::kSurface, 0.96f)),
        u32(with_alpha(col::kSurface, 0.96f)), u32(with_alpha(col::kSurface, 0.86f)),
        u32(with_alpha(col::kSurface, 0.86f)));
    dl->AddLine({0.0f, bottom - 0.5f}, {f.display.x, bottom - 0.5f}, u32(col::kHairline));

    LineupLook look;
    look.cells = bar_metrics::kStrip;
    look.song_face = look.switch_face = look.chip_face = k.s(tok::kFace);
    Dividers frame(k, dl, f.row_min.y + h * 0.5f + slide);
    draw_lineup(k, model, song, f, look, f.bar_min.x, f.row_min.y + slide, h, tempo_, accomp_,
                request_, frame);
    tempo_.popover(k, model, song, f, f.band_bottom + k.s(tok::kS2),
                   !bar_has_metronome_toggle(f.form));
    accomp_.popover(k, model, song, f, f.band_bottom + k.s(tok::kS2));
}

} // namespace

std::unique_ptr<BarLook> make_strip_bar() { return std::make_unique<StripBar>(); }

} // namespace etude::app::ui
