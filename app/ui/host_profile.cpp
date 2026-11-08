// SPDX-License-Identifier: GPL-3.0-or-later
// The profile switcher's part of the host: the chip after the library button, the panel and the
// sheet over everything else, and what the app asks of them (profile_ui.hpp).
#include "host_impl.hpp"

#include "bar.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront

namespace etude::app {

// The profile's chip after the library button, then the panel and the sheet over everything else.
void Ui::Impl::draw_profile(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    if (d.info.profiles == nullptr) {
        return;
    }
    if (d.profile_chip && !im.profile.covers_top_row()) {
        const Box& b = d.profile_box;
        begin_layer("##profile-layer", {b.x0, b.y0}, {b.w(), b.h()});
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        im.profile.draw_chip(k, *d.info.profiles, b);
        ImGui::End();
    }
    const float anchor = d.profile_chip ? d.profile_box.x0 : k.s(tok::kS3);
    im.profile.draw(k, *d.info.profiles, {0.0f, 0.0f, d.f.display.x, d.f.display.y}, d.f.row_max.y,
                    anchor);
}

ProfileChoice Ui::take_profile_choice() { return impl_->profile.take_choice(); }

bool Ui::close_profile() { return impl_->profile.close_top(); }

bool Ui::profile_open() const { return impl_->profile.panel_open(); }

} // namespace etude::app
