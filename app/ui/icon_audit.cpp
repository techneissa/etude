// SPDX-License-Identifier: GPL-3.0-or-later
#include "icon_audit.hpp"

#include "../gfx.hpp"
#include "hit_check.hpp"
#include "icon_audit_internal.hpp"
#include "item_hook.hpp"
#include "tokens.hpp"

#include <imgui_internal.h> // the current window's name and clip rect

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace etude::app::ui::audit {

namespace {

bool g_on = false;
bool g_hidden = false;
bool g_labels_hidden = false;
bool g_quiet = false;
int g_unseen = 0; // inside an Unseen scope: nothing drawn is on screen
Forced g_forced = Forced::None;
float g_unit = 1.0f;
float g_pt_per_mm = 0.0f;
std::vector<Note> g_notes;
std::vector<Face> g_faces;
std::vector<Box2> g_inks;
std::vector<Box2> g_lives;
std::vector<Hit> g_hits;
std::vector<Label> g_labels;
std::vector<Chip> g_chips;
bool g_overlay = false;
ImVec2 g_overlay_min;
ImVec2 g_overlay_max;

// A rect an overlay window covers at all: not the audit's while it is set (`set_overlay`).
bool under_overlay(ImVec2 min, ImVec2 max) {
    return g_overlay && min.x < g_overlay_max.x && g_overlay_min.x < max.x && min.y < g_overlay_max.y &&
           g_overlay_min.y < max.y;
}

// A rect wholly inside the overlay's: a label whose capitals' band (the part the audit reads) only
// touches it is still the audit's.
bool inside_overlay(ImVec2 min, ImVec2 max) {
    return g_overlay && min.x >= g_overlay_min.x && max.x <= g_overlay_max.x &&
           min.y >= g_overlay_min.y && max.y <= g_overlay_max.y;
}

void on_item(ImGuiID id, const char* label, ImVec2 min, ImVec2 max) {
    (void)id;
    if (g_unseen > 0) {
        return;
    }
    const ImGuiWindow* w = ImGui::GetCurrentWindowRead();
    std::string name = w != nullptr ? w->Name : "";
    name += " ";
    name += label != nullptr ? label : "";
    bool cut = false;
    ImVec2 clip_min = min;
    ImVec2 clip_max = max;
    if (w != nullptr) {
        const ImRect& clip = w->ClipRect;
        cut = min.x < clip.Min.x - 0.01f || min.y < clip.Min.y - 0.01f ||
              max.x > clip.Max.x + 0.01f || max.y > clip.Max.y + 0.01f;
        clip_min = clip.Min;
        clip_max = clip.Max;
    }
    g_hits.push_back({std::move(name), min, max, cut, clip_min, clip_max,
                      w != nullptr ? w->ID : 0, g_quiet || under_overlay(min, max), false});
}

} // namespace

const char* forced_name(Forced f) {
    switch (f) {
    case Forced::Hover:
        return "hover";
    case Forced::Pressed:
        return "pressed";
    case Forced::Selected:
        return "selected";
    case Forced::Focus:
        return "focus";
    case Forced::None:
        break;
    }
    return "none";
}

bool on() { return g_on && g_unseen == 0; }
Unseen::Unseen() { ++g_unseen; }
Unseen::~Unseen() { --g_unseen; }
bool icons_hidden() { return g_hidden; }
bool labels_hidden() { return g_labels_hidden; }
void set_quiet(bool quiet) { g_quiet = quiet; }
void set_overlay(ImVec2 min, ImVec2 max) {
    g_overlay = true;
    g_overlay_min = min;
    g_overlay_max = max;
}
void clear_overlay() { g_overlay = false; }
bool quiet() { return g_quiet; }
Forced forced_state() { return g_forced; }
void start() {
    g_on = true;
    set_item_hook(on_item);
}
void stop() {
    g_on = false;
    g_hidden = false;
    g_labels_hidden = false;
    g_quiet = false;
    g_forced = Forced::None;
    set_item_hook(nullptr);
}
void begin_frame(bool hidden, Forced forced, bool labels_hidden) {
    g_hidden = hidden;
    g_labels_hidden = labels_hidden;
    g_forced = forced;
    g_notes.clear();
    g_faces.clear();
    g_inks.clear();
    g_lives.clear();
    g_hits.clear();
    g_labels.clear();
    g_chips.clear();
    clear_settings_fit();
}
const std::vector<Note>& notes() { return g_notes; }
const std::vector<Face>& faces() { return g_faces; }
const std::vector<Hit>& hits() { return g_hits; }
const std::vector<Box2>& inks() { return g_inks; }
const std::vector<Box2>& lives() { return g_lives; }
const std::vector<Label>& labels() { return g_labels; }
const std::vector<Chip>& chips() { return g_chips; }
void live(ImVec2 min, ImVec2 max) {
    if (on()) {
        g_lives.push_back({min, max});
    }
}
void ink(ImVec2 min, ImVec2 max) {
    if (on()) {
        g_inks.push_back({min, max});
    }
}
void set_unit(float u) { g_unit = u; }
float unit() { return g_unit; }
void set_pt_per_mm(float pt_per_mm) { g_pt_per_mm = pt_per_mm; }
float pt_per_mm() { return g_pt_per_mm; }

void resolve_hits() {
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g == nullptr) {
        return;
    }
    // The windows back to front: a hit is covered when a window above its own (that takes mouse
    // input) lies over most of it.
    const auto above = [&](ImGuiID id) {
        std::vector<const ImGuiWindow*> out;
        bool seen = false;
        for (const ImGuiWindow* w : g->Windows) {
            if (seen && w->Active && !w->Hidden && (w->Flags & ImGuiWindowFlags_NoInputs) == 0) {
                out.push_back(w);
            }
            seen = seen || w->ID == id;
        }
        return out;
    };
    for (Hit& h : g_hits) {
        h.covered = h.quiet;
        if (h.covered || h.window == 0) {
            continue;
        }
        const float x0 = std::max(h.min.x, h.clip_min.x);
        const float y0 = std::max(h.min.y, h.clip_min.y);
        const float x1 = std::min(h.max.x, h.clip_max.x);
        const float y1 = std::min(h.max.y, h.clip_max.y);
        const float area = std::max(0.0f, x1 - x0) * std::max(0.0f, y1 - y0);
        for (const ImGuiWindow* w : above(h.window)) {
            const float ox = std::min(x1, w->Rect().Max.x) - std::max(x0, w->Rect().Min.x);
            const float oy = std::min(y1, w->Rect().Max.y) - std::max(y0, w->Rect().Min.y);
            if (ox > 0.0f && oy > 0.0f && ox * oy > 0.5f * area) {
                h.covered = true;
                break;
            }
        }
    }
}

void face(const char* name, ImVec2 face_min, ImVec2 face_max, ImVec2 area_min, ImVec2 area_max,
          const ImDrawList* dl) {
    if (on()) {
        g_faces.push_back({name, face_min, face_max, area_min, area_max, dl});
    }
}

void label(const ImDrawList* dl, const char* text, ImVec2 slot_min, ImVec2 slot_max,
           ImVec2 area_min, ImVec2 area_max, ImVec2 band_min, ImVec2 band_max, float cap_h,
           LabelAlign align, bool descenders, bool standing) {
    if (!on() || g_quiet || inside_overlay(band_min, band_max)) {
        return;
    }
    // The clip of the list the label is drawn on (not the current window's: the toast is drawn
    // after every window, and touching the current window then would open ImGui's own). Judged on
    // the capitals' band, the part the audit reads; a descender or the line box's air cut off is
    // no reason to leave the label out.
    const ImVec2 cmin = dl->GetClipRectMin();
    const ImVec2 cmax = dl->GetClipRectMax();
    const bool cut = band_min.x < cmin.x || band_min.y < cmin.y || band_max.x > cmax.x ||
                     band_max.y > cmax.y;
    const bool out = band_max.x <= cmin.x || band_max.y <= cmin.y || band_min.x >= cmax.x ||
                     band_min.y >= cmax.y;
    g_labels.push_back({text, slot_min, slot_max, area_min, area_max, band_min, band_max, cap_h,
                        align, descenders, standing, cut, out, dl});
}

void chip(const char* name, ImVec2 face_min, ImVec2 face_max, bool words, bool bar) {
    if (on() && !g_quiet && !under_overlay(face_min, face_max)) {
        g_chips.push_back({name, face_min, face_max, words, bar});
    }
}

void note(const char* name, ImVec2 min, ImVec2 max, InkKind kind, bool check_y) {
    if (!on() || g_quiet || under_overlay(min, max)) {
        return;
    }
    // A button cut by its window's clip (a row scrolled half out of view) shows only part of its
    // icon: there is nothing to measure.
    if (kind != InkKind::Target) {
        const ImDrawList* dl = ImGui::GetWindowDrawList();
        if (min.x < dl->GetClipRectMin().x || min.y < dl->GetClipRectMin().y ||
            max.x > dl->GetClipRectMax().x || max.y > dl->GetClipRectMax().y) {
            return;
        }
    }
    g_notes.push_back({name, min, max, kind, check_y});
}

} // namespace etude::app::ui::audit

namespace etude::app {

namespace {

constexpr long kMinFrames = 60;       // frames before the UI is taken as settled
constexpr Uint64 kSettleMs = 2200;    // and the wall time since the last tap: toasts and tweens end
// A tap whose button is not on screen this many frames after the last action is given up on: the
// run fails naming it.
constexpr long kTapGiveUpFrames = 600;
// The frames the verify phase takes the settled frame again before it calls the UI unsettled.
constexpr long kVerifyFrames = 240;

SDL_Surface* read_frame(SDL_Renderer* renderer) {
    SDL_Surface* raw = gfx::read_pixels(renderer, nullptr);
    if (raw == nullptr) {
        return nullptr;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    return rgba;
}

ui::PixelView view_of(const SDL_Surface* s) {
    return {static_cast<const std::uint8_t*>(s->pixels), s->w, s->h, s->pitch};
}

constexpr ui::audit::Forced kStates[] = {ui::audit::Forced::Hover, ui::audit::Forced::Pressed,
                                         ui::audit::Forced::Selected, ui::audit::Forced::Focus};
constexpr int kStateCount = 4;
// A pixel counts as changed by a state when a channel moves more than this (of 255): below it is
// the noise of a gradient redrawn.
constexpr int kStateNoise = 6;
// The antialiased edge of a fill reaches this far past its shape (pixels).
constexpr float kEdgePx = 1.0f;
// Around a pixel that moved on its own, this many pixels are left out too.
constexpr int kMoveMarginPx = 2;

} // namespace

void IconAudit::snapshot_settled() {
    ui::audit::resolve_hits();
    settled_ = ui::audit::notes();
    settled_hits_ = ui::audit::hits();
    settled_labels_ = ui::audit::labels();
    settled_faces_ = ui::audit::faces();
    settled_chips_ = ui::audit::chips();
    settled_cuts_ = ui::audit::cuts();
    settled_scrolls_ = ui::audit::page_scrolls();
}

void IconAudit::write_state(std::FILE* out, const SDL_Surface* after) const {
    const ui::audit::Forced state = kStates[state_];
    char kind[32];
    std::snprintf(kind, sizeof kind, "state-%s", ui::audit::forced_name(state));
    const float fb = std::max(0.01f, ImGui::GetIO().DisplayFramebufferScale.x);
    const std::vector<ui::audit::Face>& faces = forced_faces_;
    const SDL_Surface* forced = forced_;
    if (state_ == 0) {
        write_faceless(out); // once per run: the faces and hits do not depend on the state
    }
    // Every face inside its area.
    int outside = 0;
    for (const ui::audit::Face& f : faces) {
        const bool in = f.min.x >= f.area_min.x - kPointSlack &&
                        f.min.y >= f.area_min.y - kPointSlack &&
                        f.max.x <= f.area_max.x + kPointSlack && f.max.y <= f.area_max.y + kPointSlack;
        if (!in) {
            ++outside;
            std::fprintf(out,
                         "%s\t%s\tface-area\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\tFAIL\n",
                         label_.c_str(), f.name.c_str(), static_cast<double>(f.min.x),
                         static_cast<double>(f.min.y), static_cast<double>(f.max.x),
                         static_cast<double>(f.max.y), static_cast<double>(f.area_min.x),
                         static_cast<double>(f.area_min.y), static_cast<double>(f.area_max.x),
                         static_cast<double>(f.area_max.y));
        }
    }
    // Every changed pixel inside a face; the ones outside are put down to the nearest face.
    struct Spill {
        int pixels = 0;
        float far = 0.0f;
        int x0 = 1 << 30; // where the pixels outside lie, in pixels
        int y0 = 1 << 30;
        int x1 = -1;
        int y1 = -1;
    };
    std::vector<Spill> spill(faces.size());
    int stray = 0; // changed with no face on screen at all
    const ui::PixelView a = view_of(ref_);
    const ui::PixelView b = view_of(forced);
    const ui::PixelView c2 = view_of(after);
    const int w = std::min({a.width, b.width, c2.width});
    const int h = std::min({a.height, b.height, c2.height});
    const auto differs = [](const ui::PixelView& p, const ui::PixelView& q, int x, int y) {
        const std::uint8_t* rp = p.data + static_cast<std::ptrdiff_t>(y) * p.stride + x * 4;
        const std::uint8_t* rq = q.data + static_cast<std::ptrdiff_t>(y) * q.stride + x * 4;
        int diff = 0;
        for (int c = 0; c < 3; ++c) {
            diff = std::max(diff, std::abs(static_cast<int>(rp[c]) - static_cast<int>(rq[c])));
        }
        return diff > kStateNoise;
    };
    // What moved on its own between the plain frames before and after, and a margin round it.
    std::vector<std::uint8_t> moving(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!differs(a, c2, x, y)) {
                continue;
            }
            for (int yy = std::max(0, y - kMoveMarginPx); yy <= std::min(h - 1, y + kMoveMarginPx);
                 ++yy) {
                for (int xx = std::max(0, x - kMoveMarginPx);
                     xx <= std::min(w - 1, x + kMoveMarginPx); ++xx) {
                    moving[static_cast<std::size_t>(yy) * static_cast<std::size_t>(w) +
                           static_cast<std::size_t>(xx)] = 1;
                }
            }
        }
    }
    const float edge = kEdgePx / fb;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (moving[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                       static_cast<std::size_t>(x)] != 0 ||
                !differs(a, b, x, y)) {
                continue;
            }
            const float px = (static_cast<float>(x) + 0.5f) / fb;
            const float py = (static_cast<float>(y) + 0.5f) / fb;
            bool in_ink = false;
            for (const ui::audit::Box2& i : forced_inks_) {
                in_ink = in_ink || (px >= i.min.x - edge && px <= i.max.x + edge &&
                                    py >= i.min.y - edge && py <= i.max.y + edge);
            }
            if (in_ink) {
                continue;
            }
            float best = 1e9f;
            std::size_t nearest = faces.size();
            for (std::size_t i = 0; i < faces.size(); ++i) {
                const ui::audit::Face& f = faces[i];
                const float dx = std::max({f.min.x - edge - px, 0.0f, px - f.max.x - edge});
                const float dy = std::max({f.min.y - edge - py, 0.0f, py - f.max.y - edge});
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < best) {
                    best = d;
                    nearest = i;
                }
            }
            if (best <= 0.0f) {
                continue;
            }
            if (nearest == faces.size()) {
                ++stray;
                continue;
            }
            Spill& sp = spill[nearest];
            sp.pixels += 1;
            sp.far = std::max(sp.far, best * fb);
            sp.x0 = std::min(sp.x0, x);
            sp.y0 = std::min(sp.y0, y);
            sp.x1 = std::max(sp.x1, x);
            sp.y1 = std::max(sp.y1, y);
        }
    }
    int failed = 0;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (spill[i].pixels == 0) {
            continue;
        }
        ++failed;
        const ui::audit::Face& f = faces[i];
        // After the verdict: the box of the pixels outside, in pixels.
        std::fprintf(out, "%s\t%s\t%s\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%.2f\t0\t0\tFAIL\t%d\t%d\t%d\t%d\n",
                     label_.c_str(), f.name.c_str(), kind, static_cast<double>(f.min.x),
                     static_cast<double>(f.min.y), static_cast<double>(f.max.x),
                     static_cast<double>(f.max.y), spill[i].pixels, static_cast<double>(spill[i].far),
                     spill[i].x0, spill[i].y0, spill[i].x1, spill[i].y1);
    }
    // One line for the whole frame: the faces checked, and whether anything changed with no face.
    std::fprintf(out, "%s\t(%zu faces)\t%s\t0\t0\t0\t0\t%d\t%d\t0\t0\t%s\n", label_.c_str(),
                 faces.size(), kind, stray, outside,
                 failed == 0 && stray == 0 && outside == 0 ? "ok" : "FAIL");
}

bool IconAudit::parse(const char* option, const char* value) {
    if (std::strcmp(option, "--icon-audit") == 0) {
        tsv_ = value;
        return true;
    }
    if (std::strcmp(option, "--audit-label") == 0) {
        label_ = value;
        return true;
    }
    if (std::strcmp(option, "--audit-png") == 0) {
        png_ = value;
        return true;
    }
    if (std::strcmp(option, "--audit-tap") == 0) {
        std::string rest = value;
        std::size_t at = 0;
        while (at <= rest.size()) {
            const std::size_t comma = rest.find(',', at);
            const std::string name = rest.substr(at, comma == std::string::npos ? comma : comma - at);
            if (!name.empty()) {
                taps_.push_back(name);
            }
            if (comma == std::string::npos) {
                break;
            }
            at = comma + 1;
        }
        return !taps_.empty();
    }
    if (std::strcmp(option, "--audit-wheel") == 0) {
        char* end = nullptr;
        const float v = std::strtof(value, &end);
        if (end == value || !std::isfinite(v)) {
            return false;
        }
        wheel_ = v;
        return true;
    }
    if (std::strcmp(option, "--ui-scale") == 0) {
        char* end = nullptr;
        const float v = std::strtof(value, &end);
        if (end == value || !(v >= 1.0f && v <= 4.0f)) {
            return false;
        }
        ui_scale_ = v;
        return true;
    }
    return false;
}

void IconAudit::before_frame(SDL_Window* window, long frame) {
    if (!active()) {
        return;
    }
    if (!ui::audit::on()) {
        ui::audit::start();
        last_action_ms_ = SDL_GetTicks();
    }
    seen_ = ui::audit::notes(); // the frame before: where to tap
    ui::audit::begin_frame(phase_ == Phase::Hidden,
                           phase_ == Phase::StateForced ? kStates[state_] : ui::audit::Forced::None,
                           phase_ == Phase::HiddenLabels);
    if (phase_ == Phase::Settling && next_tap_ >= taps_.size() && wheel_ != 0.0f && !wheeled_ &&
        frame >= last_action_frame_ + kTapGapFrames) {
        // The pointer goes to the middle of the window, the wheel turns there, and the pointer
        // leaves so nothing stays hovered.
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window, &w, &h);
        const SDL_WindowID id = SDL_GetWindowID(window);
        const float x = static_cast<float>(w) * 0.5f;
        const float y = static_cast<float>(h) * 0.5f;
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = id;
        e.motion.x = x;
        e.motion.y = y;
        SDL_PushEvent(&e);
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = id;
        e.wheel.y = wheel_;
        e.wheel.mouse_x = x;
        e.wheel.mouse_y = y;
        SDL_PushEvent(&e);
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = id;
        e.motion.x = 1.0f;
        e.motion.y = 1.0f;
        SDL_PushEvent(&e);
        wheeled_ = true;
        last_action_frame_ = frame;
        last_action_ms_ = SDL_GetTicks();
        return;
    }
    tap_next(window, frame);
}

namespace {

// Whether something differs between the two frames inside `area` (pixels) outside the live
// rectangles `lives` (pixels): what plays on its own is free to change, what is beside it is not.
bool moved_outside(const ui::PixelView& with, const ui::PixelView& again, const ui::PixelRect& area,
                   std::span<const ui::PixelRect> lives) {
    std::vector<ui::PixelRect> pieces{area};
    for (const ui::PixelRect& live : lives) {
        std::vector<ui::PixelRect> next;
        for (const ui::PixelRect& r : pieces) {
            if (live.x0 >= r.x1 || live.x1 <= r.x0 || live.y0 >= r.y1 || live.y1 <= r.y0) {
                next.push_back(r);
                continue;
            }
            // The parts of `r` the live rectangle leaves: above, below, left and right of it.
            const int top = std::max(r.y0, live.y0);
            const int bottom = std::min(r.y1, live.y1);
            if (live.y0 > r.y0) {
                next.push_back({r.x0, r.y0, r.x1, live.y0});
            }
            if (live.y1 < r.y1) {
                next.push_back({r.x0, live.y1, r.x1, r.y1});
            }
            if (live.x0 > r.x0) {
                next.push_back({r.x0, top, live.x0, bottom});
            }
            if (live.x1 < r.x1) {
                next.push_back({live.x1, top, r.x1, bottom});
            }
        }
        pieces = std::move(next);
    }
    return std::any_of(pieces.begin(), pieces.end(), [&](const ui::PixelRect& r) {
        return ui::measure_ink(with, again, r, 0.0f).found;
    });
}

} // namespace

// Whether `again` shows, inside every settled button and label, what the settled frame shows: where
// it differs, something is still moving. What plays on its own (a live preview) is free to change,
// but only inside its own rectangle.
bool IconAudit::frames_match(const SDL_Surface* again) const {
    const float fb = std::max(0.01f, ImGui::GetIO().DisplayFramebufferScale.x);
    std::vector<ui::PixelRect> lives;
    for (const ui::audit::Box2& b : ui::audit::lives()) {
        lives.push_back({static_cast<int>(std::floor(b.min.x * fb)) - 1,
                         static_cast<int>(std::floor(b.min.y * fb)) - 1,
                         static_cast<int>(std::ceil(b.max.x * fb)) + 1,
                         static_cast<int>(std::ceil(b.max.y * fb)) + 1});
    }
    const auto moved = [&](ImVec2 min, ImVec2 max) {
        const ui::PixelRect area{static_cast<int>(std::floor(min.x * fb)) - 2,
                                 static_cast<int>(std::floor(min.y * fb)) - 2,
                                 static_cast<int>(std::ceil(max.x * fb)) + 2,
                                 static_cast<int>(std::ceil(max.y * fb)) + 2};
        return moved_outside(view_of(with_), view_of(again), area, lives);
    };
    bool same = true;
    for (const Seen& s : settled_) {
        same = same && !moved(s.min, s.max);
    }
    for (const ui::audit::Label& l : settled_labels_) {
        same = same && !moved(l.area_min, l.area_max);
    }
    return same;
}

bool IconAudit::after_draw(SDL_Renderer* renderer, long frame) {
    if (!active()) {
        return false;
    }
    if (phase_ == Phase::Settling) {
        if (next_tap_ < taps_.size() && frame >= last_action_frame_ + kTapGiveUpFrames) {
            // The button never showed up: a typo in the tap, or a screen that changed. Fail the
            // run naming it instead of waiting for ever.
            std::fprintf(stderr, "icon audit: tap \"%s\" never appeared on screen in %ld frames\n",
                         taps_[next_tap_].c_str(), kTapGiveUpFrames);
            if (std::FILE* out = std::fopen(tsv_.c_str(), "a")) {
                std::fprintf(out, "%s\t%s\ttap-missing\t0\t0\t0\t0\t0\t0\t0\t0\tFAIL\n",
                             label_.c_str(), taps_[next_tap_].c_str());
                std::fclose(out);
            }
            ui::audit::stop();
            return true;
        }
        if (frame < kMinFrames || next_tap_ < taps_.size() || (wheel_ != 0.0f && !wheeled_) ||
            SDL_GetTicks() - last_action_ms_ < kSettleMs) {
            return false;
        }
        with_ = read_frame(renderer);
        if (with_ == nullptr) {
            std::fprintf(stderr, "icon audit: reading the frame failed: %s\n", SDL_GetError());
            return true;
        }
        snapshot_settled();
        phase_ = Phase::Verifying;
        return false;
    }
    if (phase_ == Phase::Verifying) {
        // The same frame again, icons and all: where it differs inside a button, something is still
        // moving, so that frame becomes the one to compare with, up to a few seconds.
        SDL_Surface* again = read_frame(renderer);
        if (again == nullptr) {
            return true;
        }
        const bool same = frames_match(again);
        ++verify_frames_;
        if (same || verify_frames_ > kVerifyFrames) {
            unsettled_ = !same;
            SDL_DestroySurface(again);
            if (!png_.empty() && !SDL_SavePNG(with_, png_.c_str())) {
                std::fprintf(stderr, "icon audit: saving %s failed: %s\n", png_.c_str(),
                             SDL_GetError());
            }
            phase_ = Phase::Hidden;
            return false;
        }
        SDL_DestroySurface(with_);
        with_ = again;
        snapshot_settled();
        return false;
    }
    if (phase_ == Phase::StateRef) {
        if (ref_ != nullptr) {
            SDL_DestroySurface(ref_);
        }
        ref_ = read_frame(renderer);
        if (ref_ == nullptr) {
            ui::audit::stop();
            return true;
        }
        phase_ = Phase::StateForced;
        return false;
    }
    if (phase_ == Phase::StateForced) {
        forced_ = read_frame(renderer);
        forced_faces_ = ui::audit::faces();
        forced_hits_ = ui::audit::hits();
        forced_inks_ = ui::audit::inks();
        // What plays on its own counts as ink: free to change.
        for (const ui::audit::Box2& b : ui::audit::lives()) {
            forced_inks_.push_back(b);
        }
        if (forced_ == nullptr) {
            ui::audit::stop();
            return true;
        }
        phase_ = Phase::StateAfter;
        return false;
    }
    if (phase_ == Phase::StateAfter) {
        SDL_Surface* after = read_frame(renderer);
        if (after != nullptr) {
            if (std::FILE* out = std::fopen(tsv_.c_str(), "a")) {
                write_state(out, after);
                std::fclose(out);
            }
            SDL_DestroySurface(after);
        }
        SDL_DestroySurface(ref_);
        SDL_DestroySurface(forced_);
        ref_ = nullptr;
        forced_ = nullptr;
        if (after == nullptr || ++state_ >= kStateCount) {
            ui::audit::stop();
            return true;
        }
        phase_ = Phase::StateRef;
        return false;
    }
    if (phase_ == Phase::HiddenLabels) {
        // The same frame without the kit's labels: the difference is their ink.
        SDL_Surface* without = read_frame(renderer);
        bool ok = without != nullptr;
        if (ok) {
            std::FILE* out = std::fopen(tsv_.c_str(), "a");
            ok = out != nullptr;
            if (ok) {
                const float fb = std::max(0.01f, ImGui::GetIO().DisplayFramebufferScale.x);
                const ui::PixelView a = view_of(with_);
                const ui::PixelView b = view_of(without);
                write_labels(out, a, b, fb);
                std::fclose(out);
            } else {
                std::fprintf(stderr, "icon audit: cannot write %s\n", tsv_.c_str());
            }
        }
        SDL_DestroySurface(without);
        SDL_DestroySurface(with_);
        with_ = nullptr;
        if (!ok) {
            ui::audit::stop();
            return true;
        }
        phase_ = Phase::StateRef;
        state_ = 0;
        return false;
    }
    // The same frame without the icons: the difference is the icons' ink.
    SDL_Surface* without = read_frame(renderer);
    bool ok = without != nullptr;
    if (ok) {
        std::FILE* out = std::fopen(tsv_.c_str(), "a");
        ok = out != nullptr;
        if (ok) {
            const float fb = std::max(0.01f, ImGui::GetIO().DisplayFramebufferScale.x);
            const ui::PixelView a = view_of(with_);
            const ui::PixelView b = view_of(without);
            write_icons(out, a, b, fb);
            write_chips(out);
            write_hits(out);
            write_settings_fit(out);
            // How the settle went: the frames it took, and whether it ever came to rest.
            std::fprintf(out, "%s\t(settle)\tsettle\t0\t0\t0\t0\t%ld\t0\t0\t0\t%s\n", label_.c_str(),
                         verify_frames_, unsettled_ ? "unsettled" : "ok");
            std::fclose(out);
        } else {
            std::fprintf(stderr, "icon audit: cannot write %s\n", tsv_.c_str());
        }
    }
    SDL_DestroySurface(without);
    if (!ok) {
        SDL_DestroySurface(with_);
        with_ = nullptr;
        ui::audit::stop();
        return true;
    }
    // Then the same frame without the labels, for theirs.
    phase_ = Phase::HiddenLabels;
    return false;
}

} // namespace etude::app
