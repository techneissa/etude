// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen's place in the Ui (app/ui.hpp): its button beside the library's, and opening and
// closing it.
#include "host_impl.hpp"

#include "icons.hpp"

namespace etude::app {

void Ui::set_learn(LearnPort* port) { impl_->learn_port = port; }

void Ui::open_learn() {
    if (impl_->learn_port == nullptr) {
        return;
    }
    impl_->show_learn(impl_->kit);
}

void Ui::open_learn_panel(PanelTab tab) {
    open_learn();
    if (!impl_->learn.is_open()) {
        return;
    }
    if (tab == PanelTab::LearnScales) {
        impl_->learn.open_scales();
    } else if (tab == PanelTab::LearnChords) {
        impl_->learn.open_chords();
    } else if (tab == PanelTab::LearnEar) {
        impl_->learn.open_ear();
    } else if (tab == PanelTab::LearnMelodies) {
        impl_->learn.open_melodies();
    }
}

bool is_learn_panel_tab(PanelTab tab) {
    return tab == PanelTab::Learn || tab == PanelTab::LearnScales || tab == PanelTab::LearnChords ||
           tab == PanelTab::LearnEar || tab == PanelTab::LearnMelodies;
}

bool Ui::learn_open() const { return impl_->learn.is_open(); }

bool Ui::learn_listening() const { return impl_->learn.listening(); }

void Ui::learn_note_on(int note) { impl_->learn.note_on(note); }
void Ui::set_ear_seed(std::uint64_t seed) { impl_->learn.set_ear_seed(seed); }

// Learn's button beside the library's: a tap opens the Learn screen, and while it is open the
// button is its close cross, as the library's is the library's.
void Ui::Impl::draw_learn_button(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    if (!d.learn_button) {
        return;
    }
    const ImVec2 min = to_points(lay.learn.x, lay.learn.y, d.scale);
    const ImVec2 max = to_points(lay.learn.right(), lay.learn.bottom(), d.scale);
    const float hit = max.y - min.y;
    begin_layer("##learn-button-layer", min, {max.x - min.x, hit});
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    const bool up = im.learn.is_open();
    const ImVec2 c{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    // A hit area of the least size in the middle of its cell, a dead gap from the library's.
    const ImVec2 button_hit{(max.x - min.x) * kTopRowHitShare, hit * kTopRowHitShare};
    if (icon_toggle(k, "##learn-button", up ? icon::kClose : icon::kLearn, "Learn", c, button_hit,
                    k.s(tok::kFace), false, true)) {
        if (up) {
            im.learn.close();
        } else {
            im.show_learn(k);
        }
    }
    ImGui::End();
}

// Opens the Learn screen over whatever else is up.
void Ui::Impl::show_learn(Kit& k) {
    open = false;
    top_menu.close();
    bar->close_popovers();
    band.close_popovers();
    if (editor->is_open()) {
        editor->close(k);
    }
    library.close();
    learn.open();
}

} // namespace etude::app
