// SPDX-License-Identifier: GPL-3.0-or-later
// The Ui (app/ui.hpp): ImGui setup, event routing, the gear, the library's button and Ui::draw,
// which runs the frame's stages in order. The stages are Ui::Impl members declared in
// host_impl.hpp: host_status.cpp (the status text), host_bar_reveal.cpp (when the bar shows and
// its form), host_band.cpp (the section band), host_panels.cpp (the library, settings sheet,
// editor and cards) and host_overlays.cpp (the count-in, beat pulse and toast regions).
#include "../dev_script.hpp"
#include "../frame_log.hpp"
#include "../music_font.hpp"
#include "host_fonts.hpp"
#include "../sheet/sheet_preview.hpp"
#include "../touch_device.hpp"
#include "../ui.hpp"

#include "bar.hpp"
#include "bar_metrics.hpp"
#include "brand_marks.hpp"
#include "clef_icon.hpp"
#include "draw_hash.hpp"
#include "editor.hpp"
#include "host_impl.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "kit.hpp"
#include "library_screen.hpp"
#include "library_screen_shared.hpp"
#include "new_frame.hpp"
#include "osk.hpp"
#include "pages.hpp"
#include "preview.hpp"
#include "results_card.hpp"
#include "run_card.hpp"
#include "section_band.hpp"
#include "sections_ui.hpp"
#include "settings_catalogue.hpp"
#include "sheet.hpp"
#include "status_fit.hpp"
#include "tempo.hpp"
#include "toast_regions.hpp"
#include "transport.hpp"
#include "transpose.hpp"

#include "etude/accidental_text.hpp"
#include "etude/bar_reveal.hpp"
#include "etude/finger_count.hpp"
#include "etude/pitch.hpp"
#include "etude/touch_gesture.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui_internal.h> // BringWindowToDisplayFront

#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace etude::app {

// Generated from the font files by cmake/EmbedFile.cmake.
std::span<const unsigned char> embedded_font();
std::span<const unsigned char> embedded_font_regular();
std::span<const unsigned char> embedded_icons();

namespace ui {

bool begin_layer(const char* id, ImVec2 pos, ImVec2 size, bool scroll) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    if (!scroll) {
        flags |= ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    }
    return ImGui::Begin(id, nullptr, flags);
}

bool begin_scrim(const char* id, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    return ImGui::Begin(id, nullptr,
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                            ImGuiWindowFlags_NoFocusOnAppearing |
                            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoBringToFrontOnFocus);
}

void block_input(const char* id) {
    const ImGuiWindow* root = ImGui::FindWindowByName(id);
    if (root == nullptr) {
        return;
    }
    for (ImGuiWindow* w : GImGui->Windows) {
        if (w->RootWindow == root) {
            w->Flags |= ImGuiWindowFlags_NoMouseInputs;
        }
    }
}

bool tapped_outside(OutsideTap& guard, bool shown, const Frame& f, ImVec2 min, ImVec2 max) {
    if (!guard.away(shown, min, max) || !shown) {
        return false;
    }
    return tap_start().y >= f.row_max.y; // the bar and the gear handle their own taps
}

} // namespace ui

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

std::unique_ptr<BarLook> make_bar(BarStyle style) {
    switch (style) {
    case BarStyle::Groups:
        return make_groups_bar();
    case BarStyle::Dock:
        return make_dock_bar();
    case BarStyle::Strip:
        break;
    }
    return make_strip_bar();
}

} // namespace

// Renders the frame's draw data (and hashes it for the idle gate).
void render_draw_data(SDL_Renderer* renderer) {
    ImGui::Render();
    if (picture::on()) {
        hash_draw_data(ImGui::GetDrawData());
    }
    // This backend leaves the scaling to SDL's render scale: ImGui's points become output pixels.
    const ImVec2 fb = ImGui::GetDrawData()->FramebufferScale;
    gfx::set_scale(renderer, fb.x, fb.y);
    {
        // A new glyph or font size grows the atlas, and the backend uploads it here.
        bool atlas_changed = false;
        for (const ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
            atlas_changed = atlas_changed || texture->Status != ImTextureStatus_OK;
        }
        const FrameDetailScope atlas(FrameDetail::Atlas, atlas_changed);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    }
    gfx::set_scale(renderer, 1.0f, 1.0f);
}

void Ui::Impl::swipe_event(const SDL_Event& e, ImVec2 display, std::int64_t now_ns) {
    const double t = static_cast<double>(now_ns) * 1e-9;
    swipe_clock_s = std::max(swipe_clock_s, t);
    const auto finger_id = [&] { return static_cast<std::int64_t>(e.tfinger.fingerID); };
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        // A trackpad's fingers are not on the screen: its pointer events cover it.
        if (!is_screen_touch(e.tfinger.touchID)) {
            return;
        }
        last_finger_s = swipe_clock_s;
        const float x = e.tfinger.x * display.x;
        const float y = e.tfinger.y * display.y;
        const bool scripted = e.tfinger.touchID == kScriptedTouchId;
        const long frame = ImGui::GetFrameCount();
        if (e.type == SDL_EVENT_FINGER_DOWN) {
            // A second finger takes the press from the first: no control reads it as a tap.
            fingers.down(scripted, frame);
            if (fingers.total() > 1) {
                tap_extra_finger();
            }
            // The only finger on the screen: a touch the recogniser still holds lost its lift.
            swipe_input.down(finger_id(), x, y, swipe_clock_s, fingers.total() > 1);
        } else if (e.type == SDL_EVENT_FINGER_MOTION) {
            fingers.moved(frame);
            swipe_input.move(finger_id(), x, y, swipe_clock_s);
        } else if (e.type == SDL_EVENT_FINGER_UP) {
            fingers.up(scripted, frame);
            swipe_input.up(finger_id(), x, y, swipe_clock_s);
        } else {
            fingers.cancelled(frame);
            tap_cancel();
            tap_fingers_clear();
            swipe_input.cancel();
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID &&
            swipe_clock_s - last_finger_s > kIgnoreMouseAfterFingerS) {
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                swipe_input.down(0, e.button.x, e.button.y, swipe_clock_s);
            } else {
                swipe_input.up(0, e.button.x, e.button.y, swipe_clock_s);
            }
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if ((e.motion.state & SDL_BUTTON_LMASK) != 0 && e.motion.which != SDL_TOUCH_MOUSEID &&
            swipe_clock_s - last_finger_s > kIgnoreMouseAfterFingerS) {
            swipe_input.move(0, e.motion.x, e.motion.y, swipe_clock_s);
        }
        break;
    default:
        break;
    }
}

Ui::Ui(SDL_Window* window, SDL_Renderer* renderer) : impl_(std::make_unique<Impl>()) {
    impl_->window = window;
    impl_->renderer = renderer;
    impl_->preview = std::make_unique<LivePreview>(renderer);
    impl_->editor = std::make_unique<SectionEditor>(renderer);
    impl_->kit.preview = impl_->preview.get();
    impl_->medals = std::make_unique<MedalAtlas>(renderer);
    impl_->kit.medals = impl_->medals.get();
    impl_->library_preview = std::make_unique<LivePreview>(renderer);
    impl_->kit.library_preview = impl_->library_preview.get();
    for (std::size_t i = 0; i < impl_->config_previews.size(); ++i) {
        impl_->config_previews[i] = std::make_unique<LivePreview>(renderer);
        impl_->kit.config_previews[i] = impl_->config_previews[i].get();
    }
    impl_->library_sheet = std::make_unique<SheetPreview>(renderer);
    impl_->kit.library_sheet = impl_->library_sheet.get();
    impl_->kit.renderer = renderer;
    impl_->bar = make_bar(impl_->bar_style);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // the settings file is ours, not imgui.ini
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    add_ui_fonts(impl_->kit);
}

Ui::~Ui() {
    release_clef();
    release_brands();
    forget_fonts();
    lib_shared::forget_title_lines();
    impl_->editor.reset();
    impl_->preview.reset(); // its textures belong to the renderer, still alive here
    impl_->library_preview.reset();
    for (auto& preview : impl_->config_previews) {
        preview.reset();
    }
    impl_->library_sheet.reset();
    if (!impl_->renderer_dropped) { // a renderer that could not be replaced left the backend shut
        ImGui_ImplSDLRenderer3_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void Ui::drop_renderer() {
    release_clef();
    release_brands();
    impl_->editor->drop_renderer();
    impl_->preview->drop_renderer();
    impl_->library_preview->drop_renderer();
    for (auto& preview : impl_->config_previews) {
        preview->drop_renderer();
    }
    impl_->library_sheet->drop_renderer();
    impl_->medals->drop_renderer();
    // The backend destroys the atlas's texture; ImGui keeps its pixels and asks for it again.
    ImGui_ImplSDLRenderer3_Shutdown();
    impl_->renderer_dropped = true;
}

void Ui::adopt_renderer(SDL_Renderer* renderer) {
    impl_->renderer = renderer;
    impl_->kit.renderer = renderer;
    impl_->editor->adopt_renderer(renderer);
    impl_->preview->adopt_renderer(renderer);
    impl_->library_preview->adopt_renderer(renderer);
    for (auto& preview : impl_->config_previews) {
        preview->adopt_renderer(renderer);
    }
    impl_->library_sheet->adopt_renderer(renderer);
    impl_->medals->adopt_renderer(renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    impl_->renderer_dropped = false;
}

bool Ui::process_event(const SDL_Event& event, std::int64_t now_ns) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    // The editor reads the count before this event is added to it: it asks whether another finger
    // is already on the screen.
    // A finger that lands on the Undo of a toast drawn over the editor is the toast's.
    // So is one that lands while a naming dialog is up (it is modal: its keys, and the roll under
    // them, must not both read the touch).
    bool on_toast = false;
    if (event.type == SDL_EVENT_FINGER_DOWN && osk::dialog_open()) {
        on_toast = true;
    } else if (event.type == SDL_EVENT_FINGER_DOWN) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const RectF& chip = impl_->kit.toast_chip;
        const float px = event.tfinger.x * display.x;
        const float py = event.tfinger.y * display.y;
        on_toast = !chip.empty() && px >= chip.x && px <= chip.right() && py >= chip.y &&
                   py <= chip.bottom();
    }
    impl_->editor->on_event(event, ImGui::GetIO().DisplaySize, now_ns, impl_->fingers.total() > 0,
                            on_toast);
    impl_->swipe_event(event, ImGui::GetIO().DisplaySize, now_ns);
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_FINGER_DOWN:
        impl_->last_touch = static_cast<double>(now_ns) * 1e-9;
        break;
    default:
        break;
    }
    // Keys and typed text belong to a field that is open: ImGui's own word for it comes a frame
    // late, so the fields say it the moment they open.
    const bool key_or_text = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
                             event.type == SDL_EVENT_TEXT_INPUT;
    return osk::field_takes_event(key_or_text, ImGui::GetIO().WantTextInput,
                                  osk::text_field_open() || impl_->library.field_open());
}

int Ui::fingers_down() const { return impl_->fingers.total(); }

bool Ui::owns_point(float nx, float ny) const {
    const ImGuiContext* g = ImGui::GetCurrentContext();
    if (g == nullptr) {
        return false;
    }
    if (osk::dialog_open()) {
        return true; // a naming dialog is modal over the whole window
    }
    const ImVec2 p{nx * g->IO.DisplaySize.x, ny * g->IO.DisplaySize.y};
    for (const ImGuiWindow* w : g->Windows) {
        if (w->Active && !w->Hidden && (w->Flags & ImGuiWindowFlags_ChildWindow) == 0 &&
            (w->Flags & ImGuiWindowFlags_NoMouseInputs) == 0 && w->Rect().Contains(p)) {
            return true;
        }
    }
    return false;
}

bool Ui::blocks_key_presses() const { return impl_->scale_pill.open(); }

bool Ui::panel_open() const {
    return impl_->open || impl_->editor->is_open() || impl_->want_editor ||
           impl_->library.is_open() || impl_->learn.is_open();
}

bool Ui::slider_held() const { return impl_->slider_held; }
std::uint32_t Ui::info_bubble() const { return impl_->kit.info_id; }
void Ui::dismiss_toast() { impl_->kit.toast.state.dismiss(); }

void Ui::set_library(LibraryPort* port) { impl_->library_port = port; }

void Ui::set_midi_ports(const PortLists* lists) { impl_->midi_ports = lists; }

void Ui::set_upload(UploadPort* port) { impl_->kit.upload = port; }

void Ui::set_sources(SourcesPort* port) { impl_->kit.sources = port; }

void Ui::set_about(AboutPort* port) { impl_->kit.about = port; }
void Ui::set_icons(IconPort* port) { impl_->kit.icons = port; }

void Ui::set_library_query(std::string_view text) { impl_->library.set_query(text); }

void Ui::open_library() {
    if (impl_->library_port == nullptr) {
        return;
    }
    impl_->show_library(impl_->kit);
}

void Ui::open_progress(bool household) {
    open_library();
    impl_->library.open_progress(household);
}

void Ui::open_medals(int ladder_of) {
    open_library();
    impl_->library.open_medals(ladder_of);
}

void Ui::close_library() { impl_->library.close(); }

bool Ui::library_open() const { return impl_->library.is_open(); }

RectF Ui::toast_slot() const { return impl_->toast_slot; }

float Ui::hanging_band_bottom() const { return impl_->hanging_band_bottom; }

void Ui::library_page(int direction) { impl_->library.page(direction); }

void Ui::close_panel() {
    impl_->key_ring_demo.close();
    impl_->medal_demo.close();
    const bool over_library = impl_->open || impl_->editor->is_open() || impl_->want_editor;
    impl_->open = false;
    impl_->want_editor = false;
    if (impl_->editor->is_open()) {
        impl_->editor->close(impl_->kit);
    }
    // The library under them stays; alone, its innermost thing closes first.
    if (!over_library && impl_->library.is_open() && !impl_->library.back()) {
        impl_->library.close();
    }
    if (!over_library && impl_->learn.is_open() && !impl_->learn.back()) {
        impl_->learn.close();
    }
}

bool Ui::sheet_open() const { return impl_->open; }

bool Ui::editor_open() const { return impl_->editor->is_open(); }

bool Ui::lane_wanted() const { return impl_->band.lane(); }

bool Ui::device_check_up() const { return impl_->device_check.up(); }

bool Ui::offer_card_shown() const { return impl_->shift_offer.shown(); }

RectF Ui::offer_card_rect() const { return impl_->shift_offer.rect(); }

bool Ui::close_band_overlay() {
    // What the bar opened (the tempo popover and the transposition card it opens, the
    // accompaniment menu) is on top of the band's popovers.
    if (impl_->bar->holds_bar()) {
        impl_->bar->close_popovers();
        return true;
    }
    if (impl_->scale_pill.open()) {
        impl_->scale_pill.close();
        return true;
    }
    if (impl_->top_menu.open()) {
        impl_->top_menu.close();
        return true;
    }
    SectionBand& b = impl_->band;
    if (b.list_open() || b.menu_open()) {
        b.close_popovers();
        return true;
    }
    if (b.lane()) {
        b.set_lane(false);
        return true;
    }
    return false;
}

CardChoice Ui::take_results_choice() {
    const CardChoice c = impl_->results_choice;
    impl_->results_choice = {};
    return c;
}

DeviceCheckChoice Ui::take_device_check_choice() {
    const DeviceCheckChoice c = impl_->device_check_choice;
    impl_->device_check_choice = DeviceCheckChoice::None;
    return c;
}

RunCardAction Ui::take_run_choice() {
    const RunCardAction a = impl_->run_choice;
    impl_->run_choice = RunCardAction::None;
    return a;
}

ReviewChoice Ui::take_review_choice() {
    return std::exchange(impl_->review_choice, ReviewChoice{});
}

ReplayChoice Ui::take_replay_choice() {
    return std::exchange(impl_->replay_choice, ReplayChoice{});
}

void Ui::open_summary(bool board, bool runs) {
    impl_->want_summary = true;
    impl_->want_board = board;
    impl_->want_runs = runs;
}

void Ui::close_summary() {
    impl_->want_summary = false;
    impl_->results.close_summary();
}

bool Ui::card_ordering() const { return impl_->results.ordering(); }

void Ui::card_order_key(bool start) { impl_->results.order_key(start); }

void Ui::show_toast(std::string_view text, bool warning, TextSpan name, SongLines song) {
    impl_->kit.say_later(text, warning, name, song);
}

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
