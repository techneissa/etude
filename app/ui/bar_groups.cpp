// SPDX-License-Identifier: GPL-3.0-or-later
// Bar style Groups: floating cards in the middle of the top row, one round each group of controls,
// with a caption under every control that names it. The controls are every style's
// (bar_lineup.hpp).
#include "bar.hpp"
#include "bar_lineup.hpp"
#include "bar_metrics.hpp"
#include "tempo.hpp"

#include <algorithm>

namespace etude::app::ui {

namespace {

class GroupsBar final : public BarLook {
  public:
    float width(const Kit& k, const SongView& song, BarForm form, bool pin) const override {
        return k.u * bar_metrics::groups_plan(form, {song.has_song, song.beat_dotted, song.has_accompaniment}, pin).width();
    }
    bool centred() const override { return bar_metrics::centred(BarStyle::Groups); }
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

// A card round each group.
class Cards final : public LineupFrame {
  public:
    Cards(Kit& k, ImDrawList* dl, float y0, float y1) : k_(k), dl_(dl), y0_(y0), y1_(y1) {}
    void group(bar_metrics::Group g, float x0, float x1) override {
        (void)g;
        overlay_card(k_, dl_, {x0, y0_}, {x1, y1_}, k_.s(tok::kRadiusCard), col::kSurface,
                     ImDrawFlags_RoundCornersAll, 0.7f);
    }

  private:
    Kit& k_;
    ImDrawList* dl_;
    float y0_;
    float y1_;
};

void GroupsBar::draw(Kit& k, UiModel& model, SongPort& song, const Frame& f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = f.row_max.y - f.row_min.y;
    const float slide = (1.0f - f.reveal) * -k.s(tok::kS3);
    const float group_h = h - k.s(bar_metrics::kGroupsCardInset);
    const float gy = f.row_min.y + (h - group_h) * 0.5f + slide;
    // Every face and caption stays on its group's card.
    const FaceBand band(k, gy, gy + group_h);
    LineupLook look;
    look.cells = bar_metrics::kGroups;
    look.song_face = k.s(bar_metrics::kGroupsTransportFace);
    look.switch_face = look.chip_face = k.s(bar_metrics::kGroupsFace);
    look.captions = true;
    Cards frame(k, dl, gy, gy + group_h);
    draw_lineup(k, model, song, f, look, f.bar_min.x, f.row_min.y + slide, h, tempo_, accomp_,
                request_, frame);
    tempo_.popover(k, model, song, f, std::max(gy + group_h, f.band_bottom) + k.s(tok::kS2),
                   !bar_has_metronome_toggle(f.form));
    accomp_.popover(k, model, song, f, std::max(gy + group_h, f.band_bottom) + k.s(tok::kS2));
}

} // namespace

std::unique_ptr<BarLook> make_groups_bar() { return std::make_unique<GroupsBar>(); }

} // namespace etude::app::ui
