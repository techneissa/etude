// SPDX-License-Identifier: GPL-3.0-or-later
// The Ui (app/ui.hpp): ImGui setup, event routing, the renderer swap and the small forwarders. The
// frame's stages and Ui::draw are in host_frame.cpp; the others are Ui::Impl members declared in
// host_impl.hpp: host_status.cpp (the status text), host_bar_reveal.cpp (when the bar shows and
// its form), host_band.cpp (the section band), host_panels.cpp (the library, settings sheet,
// editor and cards) and host_overlays.cpp (the count-in, beat pulse and toast regions).
#include "../dev_script.hpp"
#include "../frame_log.hpp"
#include "../music_font.hpp"
#include "../sheet/sheet_preview.hpp"
#include "../touch_device.hpp"
#include "../ui.hpp"
#include "host_fonts.hpp"

#include "bar.hpp"
#include "brand_marks.hpp"
#include "clef_icon.hpp"
#include "draw_hash.hpp"
#include "editor.hpp"
#include "host_impl.hpp"
#include "kit_core.hpp"
#include "kit_text.hpp"
#include "kit_tip.hpp"
#include "library_screen.hpp"
#include "library_screen_shared.hpp"
#include "osk.hpp"
#include "pages.hpp"
#include "preview.hpp"
#include "results_card.hpp"
#include "run_card.hpp"
#include "section_band.hpp"
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

std::unique_ptr<ui::BarLook> make_bar(BarStyle style) {
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

} // namespace etude::app
