// SPDX-License-Identifier: GPL-3.0-or-later
// The frame's stages of the Ui (app/ui.hpp) and Ui::draw, which runs them in order: the ImGui
// frame's start, the kit's per-frame numbers and style, the top row, the bar, the gear, the
// library's button and the frame's end. The other stages are in host_status.cpp, host_bar_reveal.cpp,
// host_band.cpp, host_panels.cpp and host_overlays.cpp.
#include "../touch_device.hpp"
#include "../ui.hpp"

#include "bar.hpp"
#include "bar_metrics.hpp"
#include "host_impl.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "kit_colour.hpp"
#include "kit_core.hpp"
#include "kit_icon_toggle.hpp"
#include "kit_motion.hpp"
#include "kit_tip.hpp"
#include "new_frame.hpp"
#include "osk.hpp"
#include "sections_ui.hpp"
#include "tokens.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront

#include <string>

namespace etude::app {

namespace {

using namespace ui;

// The bar's hold times after the last use (a finger holds it longer than the mouse, and it goes
// sooner while the player is at the keys) are in core `bar_reveal.hpp`.
constexpr double kTouchHoldS = 4.0; // the gear stays bright this long after a touch

void apply_style(const Kit& k) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = {0.0f, 0.0f};
    s.WindowRounding = 0.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.PopupBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    // Items one after another are a dead gap apart: stacked rows and controls beside each other
    // keep `kHitGap` (2 mm) between their hit areas.
    s.ItemSpacing = {k.s(tok::kHitGap), k.s(tok::kHitGap)};
    s.ItemInnerSpacing = {k.s(6.0f), k.s(tok::kS1)};
    s.ScrollbarSize = k.s(tok::kS1);
    s.ScrollbarRounding = k.s(2.0f);
    s.FrameRounding = k.s(tok::kRadiusMd);
    s.TouchExtraPadding = {0.0f, 0.0f};
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col::kText;
    c[ImGuiCol_TextDisabled] = col::kText3;
    c[ImGuiCol_WindowBg] = {0, 0, 0, 0};
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = col::kFill;
    c[ImGuiCol_FrameBgHovered] = col::kFill;
    c[ImGuiCol_FrameBgActive] = col::kPressed;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = {1, 1, 1, 0.16f};
    c[ImGuiCol_ScrollbarGrabHovered] = {1, 1, 1, 0.24f};
    c[ImGuiCol_ScrollbarGrabActive] = {1, 1, 1, 0.3f};
    c[ImGuiCol_TextSelectedBg] = with_alpha(col::kAccent, 0.4f);
    c[ImGuiCol_ModalWindowDimBg] = col::kScrim;
    c[ImGuiCol_NavCursor] = {0, 0, 0, 0};
    c[ImGuiCol_InputTextCursor] = col::kAccent;
}

} // namespace

// The ImGui frame's start. False for a minimised or hidden window, whose frame stays empty.
bool Ui::Impl::begin_frame(const UiFrameInfo& info) {
    Impl& im = *this;
    new_imgui_frame();
    // The backend asks SDL for the display scale, which macOS reports as 1 on a high-density
    // window. The renderer's output size is what is drawn into, so take the scale from that.
    {
        ImGuiIO& frame_io = ImGui::GetIO();
        if (info.width_px > 0.0f && info.height_px > 0.0f && frame_io.DisplaySize.x > 0.0f &&
            frame_io.DisplaySize.y > 0.0f) {
            frame_io.DisplayFramebufferScale = {info.width_px / frame_io.DisplaySize.x,
                                                info.height_px / frame_io.DisplaySize.y};
        }
    }
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    // A minimised or hidden window reports a zero size, and every font size scaled from it
    // would be 0, which ImGui rejects. Nothing can be seen, so the frame stays empty.
    if (!(io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f && info.width_px > 0.0f &&
          info.height_px > 0.0f)) {
        ImGui::Render();
        im.slider_held = false;
        return false;
    }
    return true;
}

// The kit's per-frame numbers: the scale, the pointer and finger state, the style. Returns the scale
// from output pixels to ImGui points. A screen with no model of its own (the crash screen's) stops here.
float Ui::Impl::prepare_kit_frame(const UiFrameInfo& info, bool reduced_motion, bool high_contrast) {
    Impl& im = *this;
    const ImGuiIO& io = ImGui::GetIO();
    // ImGui works in window points; the layout is in output pixels.
    const float scale = info.width_px > 0.0f ? io.DisplaySize.x / info.width_px : 1.0f;
    Kit& k = im.kit;
    // The one scale (core ui_scale): the density, grown with the window and floored at
    // `kUiMinScale` points; in ImGui points.
    k.u =
        ui_scale(info.width_px, info.height_px, info.px_per_mm, info.pixel_density, info.ui_grow) *
        scale;
    audit::set_unit(k.u);
    audit::set_pt_per_mm((info.real_px_per_mm > 0.0f ? info.real_px_per_mm : info.px_per_mm) *
                         scale);
    audit::set_quiet(osk::modal_open());
    k.dt = io.DeltaTime;
    k.now = static_cast<double>(info.now_ns) * 1e-9;
    k.now_ns = info.now_ns;
    k.touch = io.MouseSource == ImGuiMouseSource_TouchScreen;
    tap_frame(k.u);
    // A finger event that never came (a lost lift) must not leave the count high. A finger that
    // rests sends no event, so time alone proves nothing: with the pointer up and no finger event
    // for a few frames the count asks SDL what is really down, and forgets the real fingers only
    // when it says none (scripted fingers are never forgotten by time).
    if (im.fingers.needs_check(ImGui::IsMouseDown(ImGuiMouseButton_Left), ImGui::GetFrameCount())) {
        im.fingers.settle(real_fingers_down());
        if (im.fingers.total() == 0) {
            tap_fingers_clear();
        }
    }
    k.fb_scale = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
    k.short_screen = io.DisplaySize.y < k.s(600.0f);
    k.reduced_motion = reduced_motion;
    k.high_contrast = high_contrast;
    k.start_pending_toast();
    k.toast_regions = ToastRegions{};
    set_reduced_motion(k.reduced_motion);
    apply_style(k);
    return scale;
}

// The kit's per-frame sources: the ports, the hand colours, the bar's style.
float Ui::Impl::prepare_kit(UiModel& model, SongPort& song, SectionsPort& sections,
                            InputPort& input, const UiFrameInfo& info) {
    Impl& im = *this;
    Kit& k = im.kit;
    k.input = &input;
    k.midi_ports = im.midi_ports;
    k.song = &song;
    k.sections = &sections;
    k.profiles = info.profiles;
    k.hand_left = {static_cast<float>(model.look.left_color.r) / 255.0f,
                   static_cast<float>(model.look.left_color.g) / 255.0f,
                   static_cast<float>(model.look.left_color.b) / 255.0f, 1.0f};
    k.hand_right = {static_cast<float>(model.look.right_color.r) / 255.0f,
                    static_cast<float>(model.look.right_color.g) / 255.0f,
                    static_cast<float>(model.look.right_color.b) / 255.0f, 1.0f};
    const float scale = im.prepare_kit_frame(info, model.look.reduced_effects, model.look.high_contrast);

    if (model.app.bar_style != im.bar_style) {
        im.bar_style = model.app.bar_style;
        im.bar = make_bar(im.bar_style);
    }
    return scale;
}

// The top row and the gear in ImGui points.
void Ui::Impl::lay_out_top_row(DrawFrame& d) {
    const ImGuiIO& io = ImGui::GetIO();
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    Frame& f = d.f;
    const float scale = d.scale;
    f.display = io.DisplaySize;
    f.row_min = to_points(lay.top_row.x, lay.top_row.y, scale);
    f.row_max = to_points(lay.top_row.right(), lay.top_row.bottom(), scale);
    f.top = f.row_max.y + k.s(tok::kS2);
    k.top_row_bottom = f.row_max.y;
    f.keyboard_top = lay.keyboard.y * scale;
    const RectF& side = d.info.sheet_button;
    f.side_button = {side.x * scale, side.y * scale, side.w * scale, side.h * scale};
    d.gear_min = to_points(lay.gear.x, lay.gear.y, scale);
    d.gear_max = to_points(lay.gear.right(), lay.gear.bottom(), scale);
}

// The bar, while it shows, and what it asked the host to open.
void Ui::Impl::draw_bar(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    Frame& f = d.f;
    if (f.reveal > 0.001f && d.info.replay != nullptr) {
        // The replay's bar takes the place of the bar and the section band.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, f.reveal);
        const RectF row{0.0f, f.row_min.y, f.display.x, f.row_max.y - f.row_min.y};
        const RectF controls{f.bar_min.x, f.bar_min.y, f.bar_max.x - f.bar_min.x,
                             f.bar_max.y - f.bar_min.y};
        const RectF under = d.band_shown ? d.band_place.band : RectF{};
        const ReplayChoice tapped =
            im.replay_bar.draw(k, *d.info.replay, row, controls, under, f.display.x);
        if (tapped.action != ReplayAction::None) {
            im.replay_choice = tapped;
        }
        ImGui::PopStyleVar();
        return;
    }
    if (f.reveal > 0.001f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, f.reveal);
        if (begin_layer("##bar", {0.0f, f.row_min.y},
                        {f.display.x, f.row_max.y - f.row_min.y + k.s(tok::kS1)})) {
            im.bar->draw(k, d.model, d.song, f);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        Page wanted = Page::Notes;
        if (im.bar->wants_open(&wanted)) {
            im.open = true;
            im.bar->close_popovers();
            im.band.close_popovers();
            im.sheet.open_at(wanted);
        }
    }
}

// The gear, always on top, and the dialog that names a section.
void Ui::Impl::draw_gear(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ImVec2 gear_min = d.gear_min;
    const ImVec2 gear_max = d.gear_max;
    const ImVec2 gear_c{(gear_min.x + gear_max.x) * 0.5f, (gear_min.y + gear_max.y) * 0.5f};
    const bool recently = k.now - im.last_touch < kTouchHoldS;
    const float gear_alpha =
        tween(ImGui::GetID("##gear-alpha"),
              d.at_keys && !im.open && !im.revealed && !recently ? 0.38f : 1.0f, 0.6f);
    const float hit = gear_max.y - gear_min.y;
    begin_layer("##gear-layer", gear_min, {gear_max.x - gear_min.x, hit});
    // Always on top: the bar's window spans the row and is often created later, which would
    // put it over the gear and take its taps.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    // The dialog that names a section, asked for by the band, the lane or the editor: one
    // window holds it, every frame.
    section_rename_dialog(k);
    const bool replay = d.info.replay != nullptr;
    // The hit area is a touch target, not the whole row: the bar and the band keep their gaps.
    if (gear(k, gear_c, hit * kTopRowHitShare, im.open || replay, gear_alpha)) {
        if (replay) {
            im.replay_choice = {ReplayAction::Close, 0.0};
        } else if (im.editor->is_open()) {
            im.editor->close(k); // the gear leaves the editor for the settings
            im.open = true;
        } else {
            im.open = !im.open;
        }
        if (im.open) {
            im.bar->close_popovers();
            im.band.close_popovers();
            im.sheet.reopen();
        }
    }
    ImGui::End();
}

// The library's button beside the gear: a tap opens the song library, and while it is open
// the button is its close cross, as the gear is the settings'.
void Ui::Impl::draw_library_button(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    const float scale = d.scale;
    if (d.library_button) {
        const ImVec2 lib_min = to_points(lay.library.x, lay.library.y, scale);
        const ImVec2 lib_max = to_points(lay.library.right(), lay.library.bottom(), scale);
        const float hit = lib_max.y - lib_min.y;
        begin_layer("##library-layer", lib_min, {lib_max.x - lib_min.x, hit});
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        const bool up = im.library.is_open();
        const ImVec2 c{(lib_min.x + lib_max.x) * 0.5f, (lib_min.y + lib_max.y) * 0.5f};
        const ImVec2 lib_hit{(lib_max.x - lib_min.x) * kTopRowHitShare, hit * kTopRowHitShare};
        if (icon_toggle(k, "##library", up ? icon::kClose : icon::kLibrary, "Song library", c,
                        lib_hit, k.s(tok::kFace), false, true)) {
            if (up) {
                im.library.close();
            } else {
                im.show_library(k);
            }
        }
        ImGui::End();
    }
}

// Opens the song library over whatever else is up.
void Ui::Impl::show_library(Kit& k) {
    open = false;
    top_menu.close();
    bar->close_popovers();
    band.close_popovers();
    if (editor->is_open()) {
        editor->close(k);
    }
    learn.close();
    library.open();
}

// The frame's last steps: what the sections port and the closed screens leave to do, the
// font, the kit's end of frame and the draw data.
void Ui::Impl::end_frame(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    if (std::string said = d.sections.take_message(); !said.empty()) {
        k.say_free(said);
    }
    if (im.library_port != nullptr && im.library.showing() && !im.library.is_open()) {
        im.library.stop_input(); // closed from outside its own frame: the same
    }
    if (!im.open && im.open_p > 0.0f) {
        im.sheet.stop_input(); // sliding away: no input (the gear closes it after it was drawn)
    }
    ImGui::PopFont();
    finish_frame(k);
    im.slider_held = k.slider_held;
    k.slider_held = false;

    render_draw_data(im.renderer);
}

void Ui::draw(UiModel& model, SongPort& song, SectionsPort& sections, InputPort& input,
              const UiFrameInfo& info) {
    Impl& im = *impl_;
    if (!im.begin_frame(info)) {
        return;
    }
    const float scale = im.prepare_kit(model, song, sections, input, info);
    im.medals->step();
    DrawFrame d(model, song, sections, info, im.kit, song.view(), scale);
    im.lay_out_top_row(d);
    im.measure_status(d);
    im.decide_bar(d);
    im.swipe_bar(d);
    im.ease_bar(d);
    im.place_bar(d);
    im.place_band(d);
    if (++im.frames > kGlyphWarmAfterFrames) {
        warm_glyphs(d.k, im.glyph_warm, im.card_scale);
    }
    ImGui::PushFont(d.k.regular, d.k.s(tok::kBody));
    im.draw_panels(d);
    im.draw_cards(d);
    im.draw_sheet_helps(d);
    // The delete-profile confirm over a small window's top row leaves the row out: no status text,
    // icons or bar show through its card, and none can be tapped under it.
    const bool top_row = !im.profile.covers_top_row();
    if (top_row) {
        im.draw_bar(d);
        im.draw_band(d);
        im.draw_gear(d);
        im.draw_library_button(d);
        im.draw_learn_button(d);
        im.draw_menu_button(d);
    }
    im.draw_profile(d);
    if (top_row) {
        im.draw_status(d);
    }
    im.draw_overlays(d);
    im.draw_crash(d);
    im.place_toasts(d);
    im.end_frame(d);
}

} // namespace etude::app
