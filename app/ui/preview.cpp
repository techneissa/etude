// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview.hpp"

#include "../gfx.hpp"
#include "icon_audit.hpp"

#include "../frame_log.hpp"
#include "../song_controller.hpp" // kGradeShowNs
#include "../ui.hpp"              // display_px_per_mm
#include "etude/midi_event.hpp"
#include "etude/screen_layout.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace etude::app::ui {

namespace {

// One pass of the loop, in seconds: a rising C major line, then the chord, then a rest.
struct Step {
    double at;
    std::uint8_t note;
    std::uint8_t velocity; // 0 releases
};
constexpr Step kLoop[] = {
    {0.00, 60, 92}, {0.62, 60, 0},   {0.40, 64, 96}, {1.02, 64, 0},  {0.80, 67, 100},
    {1.42, 67, 0},  {1.20, 72, 108}, {1.90, 72, 0},  {2.10, 60, 88}, {2.10, 64, 84},
    {2.10, 67, 90}, {3.20, 60, 0},   {3.20, 64, 0},  {3.20, 67, 0},
};
constexpr double kLoopS = 4.0;
// The song loop: notes falling onto C3..C5, both hands and one with no hand, white and black keys,
// soft to hard, in three octaves. One pass is kSongLoopS; a note starts at `at` (when it reaches
// the keys) and lasts `len`.
struct SongNote {
    double at;
    double len;
    std::uint8_t key;
    std::uint8_t velocity;
    Hand hand;
};
constexpr SongNote kSong[] = {
    {0.00, 0.90, 48, 40, Hand::Left},      {0.25, 0.50, 60, 70, Hand::Right},
    {0.75, 0.50, 63, 85, Hand::Right},     {1.00, 0.90, 51, 55, Hand::Left},
    {1.25, 0.50, 66, 100, Hand::Right},    {1.50, 0.70, 58, 50, Hand::Unknown},
    {1.75, 0.50, 69, 115, Hand::Right},    {2.00, 0.90, 55, 70, Hand::Left},
    {2.25, 0.60, 72, 127, Hand::Right},    {2.90, 0.80, 64, 96, Hand::Right},
    {2.90, 0.80, 68, 110, Hand::Right},    {3.00, 0.90, 53, 60, Hand::Left},
};
constexpr double kSongLoopS = 4.0;
// The song loop shows this many seconds above the keys, so a note is on screen for a second or so.
constexpr float kSongVisibleS = 1.6f;
// The grade words: one every kGradeEveryS in the order Perfect, Good, Early, Late, Miss, each on
// a key the loop plays and shown for as long as in play (kGradeShowNs), so two or three overlap
// in time and the placement has something to do.
constexpr double kGradeEveryS = 0.5;
constexpr std::uint8_t kGradeKeys[5] = {64, 67, 62, 65, 69};
constexpr NoteGrade kGradeOrder[5] = {NoteGrade::Perfect, NoteGrade::Good, NoteGrade::Early,
                                      NoteGrade::Late, NoteGrade::Missed};
constexpr double kGradeCycleS = kGradeEveryS * 6.0; // five words and a rest
// The opening loop: a rest after the last note, and the seconds shown above the keys.
constexpr double kOpeningRestS = 1.2;
// The widest target the banded preview renders, in pixels.
constexpr float kBandedMaxPx = 1600.0f;
constexpr float kOpeningVisibleS = 2.4f;
constexpr float kAspect = 6.0f / 16.0f;
constexpr float kShortAspect = 5.0f / 16.0f; // on a short screen, so the page still fits

} // namespace

LivePreview::LivePreview(SDL_Renderer* renderer) : renderer_(renderer) {
    last_press_.fill(kNeverPressed);
    KeyViewFollower follower;
    follower.snap(layout_.range(), layout_.range());
    view_ = follower.frame();
}

LivePreview::~LivePreview() {
    if (target_ != nullptr) {
        gfx::destroy_texture(target_);
    }
}

void LivePreview::drop_renderer() {
    if (target_ != nullptr) {
        gfx::destroy_texture(target_);
        target_ = nullptr;
    }
    target_w_ = 0;
    target_h_ = 0;
    scene_.reset();
}

void LivePreview::strike(std::uint8_t key, std::uint8_t velocity) {
    if (key >= sounding_.size()) {
        return;
    }
    sounding_.set(key, velocity > 0);
    if (struck_count_ < struck_.size()) {
        struck_[struck_count_++] = {key, velocity};
    }
}

void LivePreview::reset(std::int64_t now_ns) {
    // What was sounding ends with the loop it belonged to.
    for (std::size_t key = 0; key < sounding_.size(); ++key) {
        if (sounding_.test(key)) {
            strike(static_cast<std::uint8_t>(key), 0);
        }
    }
    const KeyTransition released = keys_.clear();
    trail_.apply(released, now_ns);
    last_press_.fill(kNeverPressed);
    last_t_ = -1.0;
}

void LivePreview::set_opening(const SongOpening* opening, KeyRange keys) {
    const KeyRange shown = layout_.range();
    if (opening == opening_ &&
        (opening == nullptr || (keys.lowest == shown.lowest && keys.highest == shown.highest))) {
        return;
    }
    opening_ = opening;
    last_drawn_ = -1.0;
    start_ = -1.0;
    if (opening == nullptr) {
        return;
    }
    opening_bars_.clear();
    opening_bars_.reserve(opening->notes.size() * 2);
    layout_ = KeyboardLayout(keys, layout_.blocks());
    KeyViewFollower follower;
    follower.snap(layout_.range(), layout_.range());
    view_ = follower.frame();
    reset(0);
}

void LivePreview::feed(double t, std::int64_t now_ns) {
    const bool opening = loop_ == PreviewLoop::Opening && opening_ != nullptr;
    const double loop_s = opening ? opening_->length_s + kOpeningRestS
                          : loop_ == PreviewLoop::Song ? kSongLoopS
                                                       : kLoopS;
    const double phase = std::fmod(t, loop_s);
    const double prev = last_t_ < 0.0 ? -1.0 : std::fmod(last_t_, loop_s);
    const bool wrapped = last_t_ >= 0.0 && phase < prev;
    const auto play = [&](double at, std::uint8_t note, std::uint8_t velocity) {
        const bool due = wrapped ? (at > prev || at <= phase) : (at > prev && at <= phase);
        if (!due) {
            return;
        }
        MidiEvent ev;
        ev.time_ns = now_ns;
        ev.type = velocity > 0 ? MidiEventType::NoteOn : MidiEventType::NoteOff;
        ev.data1 = note;
        ev.data2 = velocity;
        const KeyTransition tr = keys_.apply(ev);
        trail_.apply(tr, now_ns);
        if (tr.type == KeyTransitionType::Pressed) {
            last_press_[tr.note] = now_ns;
            last_velocity_[tr.note] = tr.velocity;
        }
        if (tr.type == KeyTransitionType::Pressed || tr.type == KeyTransitionType::Released) {
            strike(tr.note, tr.type == KeyTransitionType::Pressed
                                ? static_cast<std::uint8_t>(std::max<int>(1, tr.velocity))
                                : std::uint8_t{0});
        }
    };
    if (opening) {
        for (const OpeningNote& n : opening_->notes) {
            play(std::fmod(static_cast<double>(n.at + n.len), loop_s), n.key, 0);
        }
        for (const OpeningNote& n : opening_->notes) {
            play(n.at, n.key, n.velocity);
        }
    } else if (loop_ == PreviewLoop::Song) {
        // Releases first, so a key whose next note starts as the last one ends is played again.
        for (const SongNote& n : kSong) {
            play(std::fmod(n.at + n.len, loop_s), n.key, 0);
        }
        for (const SongNote& n : kSong) {
            play(n.at, n.key, n.velocity);
        }
    } else {
        for (const Step& st : kLoop) {
            play(st.at, st.note, st.velocity);
        }
    }
    last_t_ = t;
}

// Lays out the song loop's notes still falling at `t` (the keys are played by `feed`).
void LivePreview::feed_song(double t) {
    const double phase = std::fmod(t, kSongLoopS);
    song_bar_count_ = 0;
    for (const SongNote& n : kSong) {
        // This pass's note and the next pass's, which is already falling near the loop's end.
        for (const double at : {n.at, n.at + kSongLoopS}) {
            const auto bottom = static_cast<float>((at - phase) / kSongVisibleS);
            const auto top = static_cast<float>((at + n.len - phase) / kSongVisibleS);
            if (top <= 0.0f || bottom >= 1.0f || song_bar_count_ >= song_bars_.size()) {
                continue;
            }
            SongBar& bar = song_bars_[song_bar_count_++];
            bar = SongBar{};
            bar.key = n.key;
            bar.velocity = n.velocity;
            bar.hand = n.hand;
            bar.bottom = bottom;
            bar.top = top;
        }
    }
}

// Lays out the opening's notes still falling at `t`.
void LivePreview::feed_opening(double t) {
    opening_bars_.clear();
    if (opening_ == nullptr) {
        return;
    }
    const double loop_s = opening_->length_s + kOpeningRestS;
    const double phase = std::fmod(t, loop_s);
    for (const OpeningNote& n : opening_->notes) {
        for (const double at : {static_cast<double>(n.at), n.at + loop_s}) {
            const auto bottom = static_cast<float>((at - phase) / kOpeningVisibleS);
            const auto top = static_cast<float>((at + n.len - phase) / kOpeningVisibleS);
            if (top <= 0.0f || bottom >= 1.0f || opening_bars_.size() >= opening_bars_.capacity()) {
                continue;
            }
            SongBar bar;
            bar.key = n.key;
            bar.velocity = n.velocity;
            bar.hand = n.hand;
            bar.bottom = bottom;
            bar.top = top;
            opening_bars_.push_back(bar);
        }
    }
}

void LivePreview::feed_grades(double t) {
    const double show_s = static_cast<double>(kGradeShowNs) * 1e-9;
    const double phase = std::fmod(t, kGradeCycleS);
    grade_count_ = 0;
    for (std::size_t i = 0; i < 5; ++i) {
        const double age_s = phase - kGradeEveryS * static_cast<double>(i);
        if (age_s >= 0.0 && age_s < show_s) {
            grades_[grade_count_++] = {kGradeKeys[i], kGradeOrder[i],
                                       static_cast<float>(age_s / show_s)};
        }
    }
}

void LivePreview::draw(Kit& k, const LookSettings& look, float width) {
    const float aspect = k.short_screen ? kShortAspect : kAspect;
    draw(k, look, width, std::round(width * aspect), PreviewLoop::Played);
}

void LivePreview::draw(Kit& k, const LookSettings& look, float width, float height,
                       PreviewLoop loop) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy({width, height});
    if (!ImGui::IsItemVisible()) {
        return;
    }
    // It plays on its own: the UI audit leaves its pixels out of the state check.
    audit::live(pos, {pos.x + width, pos.y + height});
    const int w = std::max(16, static_cast<int>(std::lround(width * k.fb_scale)));
    const int h = std::max(1, static_cast<int>(std::lround(height * k.fb_scale)));
    const Rendered out = render(look, w, h, loop, k.now);
    if (out.texture == nullptr) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = k.s(tok::kRadiusMd);
    const ImTextureRef tex(
        static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(out.texture)));
    dl->AddImageRounded(tex, pos, {pos.x + width, pos.y + height}, {0.0f, out.crop}, {1.0f, 1.0f},
                        ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), r);
    dl->AddRect(pos, {pos.x + width, pos.y + height}, u32(col::kHairline), r, 0, 1.0f);
}

float LivePreview::draw_banded(Kit& k, const LookSettings& look, ImVec2 a, ImVec2 b, float band,
                               float rounding) {
    const float width = b.x - a.x;
    const float height = b.y - a.y - band;
    if (width < 1.0f || height < 1.0f) {
        return b.y - band;
    }
    audit::live(a, b);
    // A large card is drawn from a target at most kBandedMaxPx across, scaled up: the scene costs
    // by its pixels.
    const float px_scale = std::min(k.fb_scale, kBandedMaxPx / width);
    const int w = std::max(16, static_cast<int>(std::lround(width * px_scale)));
    const int h = std::max(1, static_cast<int>(std::lround(height * px_scale)));
    const Rendered out = render(look, w, h, PreviewLoop::Opening, k.now);
    if (out.texture == nullptr) {
        return b.y - band;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImTextureRef tex(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(out.texture)));
    // The target's visible rows (from `crop`) map onto `height` points: the notes above the keys,
    // the keys at the bottom, the band between them.
    const float visible = 1.0f - out.crop;
    const float keys_share = (out.keys - out.crop) / visible; // of `height`, where the keys start
    const float notes_h = std::round(height * keys_share); // whole points: the band's controls sit centred
    dl->AddImage(tex, a, {b.x, a.y + notes_h}, {0.0f, out.crop}, {1.0f, out.keys});
    dl->AddImageRounded(tex, {a.x, a.y + notes_h + band}, b, {0.0f, out.keys}, {1.0f, 1.0f},
                        ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), rounding,
                        ImDrawFlags_RoundCornersBottom);
    return a.y + notes_h;
}

double LivePreview::run_opening(double now_s) {
    const auto now_ns = static_cast<std::int64_t>(now_s * 1e9);
    if (start_ < 0.0) {
        start_ = now_s;
    }
    if ((last_drawn_ >= 0.0 && now_s - last_drawn_ > 0.5) || loop_ != PreviewLoop::Opening) {
        reset(now_ns);
        start_ = now_s;
        loop_ = PreviewLoop::Opening;
    }
    last_drawn_ = now_s;
    const double t = now_s - start_;
    feed(t, now_ns);
    if (opening_ == nullptr) {
        return 0.0;
    }
    return std::fmod(t, static_cast<double>(opening_->length_s) + kOpeningRestS);
}

LivePreview::Rendered LivePreview::render(const LookSettings& look, int w, int visible_h,
                                          PreviewLoop loop, double now_s) {
    const auto now_ns = static_cast<std::int64_t>(now_s * 1e9);
    if (start_ < 0.0) {
        start_ = now_s;
    }
    if ((last_drawn_ >= 0.0 && now_s - last_drawn_ > 0.5) || loop != loop_) {
        // The page was away, or the other page's loop ran: start the loop again rather than
        // replay what was missed.
        reset(now_ns);
        start_ = now_s;
        loop_ = loop;
    }
    last_drawn_ = now_s;

    // The target is the image's size in output pixels plus the rows the scene keeps above its
    // note area (the top row, a chord row), which the image leaves out.
    const auto visible = static_cast<float>(visible_h);
    const float ppmm = display_px_per_mm(gfx::window_of(renderer_));
    float total = visible;
    ScreenLayout lay;
    for (int i = 0; i < 4; ++i) {
        LookSettings sized = look;
        sized.keyboard_height = kMaxKeyboardHeight;
        lay = compute_layout(static_cast<float>(w), total, ppmm, sized);
        total = visible + lay.note_area.y;
    }
    const int h = static_cast<int>(total);
    if (target_ == nullptr || w != target_w_ || h != target_h_) {
        const FrameDetailScope timing(FrameDetail::Texture);
        if (target_ != nullptr) {
            gfx::destroy_texture(target_);
        }
        target_ = gfx::create_texture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                      w, h);
        target_w_ = w;
        target_h_ = h;
    }
    if (target_ == nullptr) {
        return {};
    }
    // The look as set, but with fewer seconds on screen and taller keys: in a strip this small
    // the notes would otherwise hardly leave the keys. Style, colours and names are untouched.
    LookSettings shown = look;
    shown.visible_seconds = loop == PreviewLoop::Song      ? kSongVisibleS
                            : loop == PreviewLoop::Opening ? kOpeningVisibleS
                                                           : std::min(look.visible_seconds, 1.6f);
    shown.keyboard_height = kMaxKeyboardHeight;
    if (!scene_) {
        scene_ = std::make_unique<Scene>(renderer_, shown);
    } else if (!(scene_->settings() == shown)) {
        scene_->set_settings(shown);
    }

    const double t = now_s - start_;
    feed(t, now_ns);
    const std::int64_t span = scene_->trail_span_ns();
    trail_.prune(now_ns, span);
    if (loop == PreviewLoop::Opening) {
        feed_opening(t);
        song_bar_count_ = 0;
        if (look.rising_in_song) {
            trail_.visible(now_ns, span, bars_);
        } else {
            bars_.clear();
        }
        grade_count_ = 0;
    } else if (loop == PreviewLoop::Song) {
        feed_song(t);
        // The keys are played for the song: nothing rises from them unless the look asks for it,
        // as in a song.
        if (look.rising_in_song) {
            trail_.visible(now_ns, span, bars_);
        } else {
            bars_.clear();
        }
        grade_count_ = 0;
    } else {
        song_bar_count_ = 0;
        feed_grades(t);
        trail_.visible(now_ns, span, bars_);
    }

    SDL_Texture* previous = gfx::target(renderer_);
    gfx::set_target(renderer_, target_);
    if (layout_.blocks() != look.block_layout) {
        layout_ = KeyboardLayout(layout_.range(), look.block_layout);
    }
    FrameInput frame{layout_,        view_,  keys_, bars_,  last_press_,
                     last_velocity_, now_ns, false, nullptr};
    frame.grades = std::span<const GradePopup>(grades_.data(), grade_count_);
    frame.trail_alpha = rising_alpha(loop != PreviewLoop::Played, look.rising_in_song);
    frame.song_bars = loop == PreviewLoop::Opening
                          ? std::span<const SongBar>(opening_bars_)
                          : std::span<const SongBar>(song_bars_.data(), song_bar_count_);
    scene_->draw(frame);
    gfx::set_target(renderer_, previous);
    return {target_, lay.note_area.y / total, lay.keyboard.y / total};
}

} // namespace etude::app::ui
