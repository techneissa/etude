// SPDX-License-Identifier: GPL-3.0-or-later
// The notice after a crash, over every other screen: its card in the note area, and the sheet that
// "Send report" opens over the whole window.
#include "host_impl.hpp"

namespace etude::app {

void Ui::Impl::draw_crash(DrawFrame& d) {
    const float scale = d.scale;
    const RectF area{d.lay.note_area.x * scale, d.lay.note_area.y * scale, d.lay.note_area.w * scale,
                     d.lay.note_area.h * scale};
    const CrashChoice tapped = crash.draw(d.k, d.info.crash, area, d.f.display, d.f.row_max.y);
    if (tapped != CrashChoice::None) {
        crash_choice = tapped;
    }
}

void Ui::open_crash_report() { impl_->crash.open_report(); }

CrashChoice Ui::take_crash_choice() {
    const CrashChoice c = impl_->crash_choice;
    impl_->crash_choice = CrashChoice::None;
    return c;
}

CrashScreenTap Ui::draw_crash_screen(const CrashScreenView& view, const CrashView* report,
                                     const UiFrameInfo& info) {
    Impl& im = *impl_;
    if (!im.begin_frame(info)) {
        return {};
    }
    im.prepare_kit_frame(info, false, false);
    Kit& k = im.kit;
    ImGui::PushFont(k.regular, k.s(tok::kBody));
    CrashScreenTap tap = im.crash_screen.draw(k, view, report, ImGui::GetIO().DisplaySize);
    if (tap.button == CrashButton::SendReport) {
        im.crash_screen.open_report();
    }
    ImGui::PopFont();
    finish_frame(k);
    k.slider_held = false;
    render_draw_data(im.renderer);
    return tap;
}

} // namespace etude::app
