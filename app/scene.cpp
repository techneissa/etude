// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene.hpp"

#include "scene_internal.hpp"

#include "frame_log.hpp"
#include "etude/beat_lines.hpp"
#include "etude/grade_palette.hpp"
#include "etude/grade_words.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

namespace etude::app {

using namespace scene_detail;

namespace {

// More cached note names than this means the sizes changed a lot (a resize drag): start again.
constexpr std::size_t kMaxCachedNames = 256;
// One font, one answer: every Scene (the screen's and the settings preview's) measures its name
// pieces into this table, so a piece is measured once. The renderer is single-threaded.
PieceWidths& shared_piece_widths() {
    static PieceWidths widths;
    return widths;
}
// Time one frame spends measuring name pieces for names the settings just asked for, and, once the
// scene has drawn a frame, ahead of time for every piece a naming setting may bring. A slice is a
// time, not a count, so a slow board takes more frames for the same work and no frame more time.
constexpr std::int64_t kNameSliceNs = 2'000'000;
constexpr std::int64_t kWarmSliceNs = 1'000'000;
// Most cached name textures dropped in one frame while there are too many.
constexpr std::size_t kEvictPerFrame = 8;
// The scene draws this many frames before it starts measuring ahead, so the first frames are not
// delayed.
constexpr int kWarmAfterFrames = 2;

} // namespace

Scene::Scene(SDL_Renderer* renderer, const LookSettings& settings)
    : renderer_(renderer), settings_(settings.clamped()), text_(renderer), textures_(renderer),
      widths_(shared_piece_widths()) {
    name_in_.reserve(kMaxNameCandidates);
    fingers_.reserve(kMaxNameCandidates);
    name_colour_.reserve(kMaxNameCandidates);
    name_out_.resize(kMaxNameCandidates);
    name_slots_.resize(kMaxNameCandidates);
    name_ready_.resize(kMaxNameCandidates);
    hit_shapes_.resize(HitEffects::kMaxShapes);
    seen_press_.fill(kNeverPressed);
    // The first frame names its notes: wait for the pieces they need, with no limit.
    rebuild_names();
    if (names_pending_) {
        measure_pieces(next_names_.wanted, -1);
        apply_names();
    }
    update_recipe();
}

void Scene::drop_renderer() { textures_.drop(); }

void Scene::adopt_renderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
    text_.set_renderer(renderer);
    textures_.make(renderer);
}

void Scene::set_settings(const LookSettings& settings) {
    const LookSettings next = settings.clamped();
    if (next.effective_hit_effect() != settings_.effective_hit_effect()) {
        hits_.clear();
    }
    settings_ = next;
    geometry_valid_ = false;
    rebuild_names();
    update_recipe();
}

void Scene::begin_frame(bool budgeted) {
    RasterBudget& budget = raster_budget();
    budget.framed = budgeted;
    budget.spent_ns = 0;
}

void Scene::reserve_notes(std::size_t notes) {
    // Past this many notes in view the blocks are slivers; a view denser than that, or a "Notes ahead"
    // raised after the song loaded, grows once and keeps the capacity.
    constexpr std::size_t kMaxReservedNotes = 1500;
    notes = std::min(notes, kMaxReservedNotes);
    painter_.reserve(notes);
    black_painter_.reserve(notes / 2 + 1);
    mesh_.reserve(notes * 41, notes * 180);
    outline_.reserve(notes * 21, notes * 60);
    glow_.reserve(notes * 8, notes * 12);
}

void Scene::set_pinned_bar(const PinnedBar& bar) {
    if (bar.pinned != pinned_bar_.pinned || bar.style != pinned_bar_.style ||
        bar.has_song != pinned_bar_.has_song || bar.lane != pinned_bar_.lane) {
        pinned_bar_ = bar;
        geometry_valid_ = false;
    }
}

void Scene::set_px_per_mm(float px_per_mm) {
    if (px_per_mm != px_per_mm_set_) {
        px_per_mm_set_ = px_per_mm;
        geometry_valid_ = false;
    }
}

void Scene::set_ui_grow(float grow) {
    if (grow != ui_grow_) {
        ui_grow_ = grow;
        geometry_valid_ = false;
    }
}

void Scene::set_falling_area(const RectF& area, bool chord_label) {
    chord_label_shown_ = chord_label;
    if (!falling_set_ || area.x != falling_area_.x || area.y != falling_area_.y ||
        area.w != falling_area_.w || area.h != falling_area_.h) {
        falling_area_ = area;
        falling_set_ = true;
        geometry_valid_ = false;
    }
}

void Scene::set_effects_level(EffectsLevel level) {
    if (level != level_) {
        level_ = level;
        update_recipe();
    }
}

void Scene::update_recipe() {
    budget_ = effects_budget(level_);
    recipe_ = degraded(style_recipe(settings_), budget_);
    black_recipe_ = degraded(fuller_recipe(style_recipe(settings_)), budget_);
    glow_.use_shader(budget_.shaders && settings_.note_style == NoteStyle::Neon);
}

bool Scene::measure_pieces(std::span<const PieceWidths::Index> pieces, std::int64_t budget_ns) {
    if (widths_.all_complete(pieces)) {
        return true;
    }
    const FrameDetailScope timing(FrameDetail::Text);
    const std::int64_t deadline = frame_clock_ns() + budget_ns;
    return widths_.measure(
        pieces,
        [this](const std::string& piece, std::size_t px) {
            return text_.measure(piece, static_cast<float>(px)).x;
        },
        [budget_ns, deadline] { return budget_ns >= 0 && frame_clock_ns() >= deadline; });
}

// The colours depend only on the settings, so they are made once, not per frame. The names are
// queued: they replace the drawn ones when the width of each piece is known at every size
// (apply_names), at once when a warm-up measured the pieces already.
void Scene::rebuild_names() {
    colours_ = NoteColourTable(settings_);
    {
        // The chord label's hand colours: the hands' own, whatever scheme the blocks use.
        LookSettings hands = settings_;
        hands.color_scheme = ColorScheme::ByHand;
        chord_colours_[0] = to_fcolor(note_color(hands, 60, 100, Hand::Left));
        chord_colours_[1] = to_fcolor(note_color(hands, 60, 100, Hand::Right));
    }
    chord_view_.invalidate();
    for (int note = 0; note < 128; ++note) {
        const auto i = static_cast<std::size_t>(note);
        fill_[i] = to_fcolor(colours_.colour(note, 100, kFreePlayHand));
        label_colour_[i] = to_fcolor(colours_.label(note, 100, kFreePlayHand));
        key_label_colour_[i] = to_fcolor(colours_.key_label(note, 100, kFreePlayHand));
    }
    if (!has_metrics_) {
        name_metrics_ = text_.vertical_metrics();
        has_metrics_ = true;
    }
    NameKey key;
    key.mode = settings_.name_mode;
    key.flats = settings_.names_spelling == NameSpelling::Flats;
    key.octave = settings_.names_show_octave;
    key.c_markers = settings_.c_markers;
    key.numbering = settings_.octave_numbering;
    if (has_names_key_ && key == names_key_) {
        return; // the same names as already drawn or already queued
    }
    names_key_ = key;
    has_names_key_ = true;
    NameSet& next = next_names_;
    next.wanted.clear();
    const auto table_of = [&](const std::string& piece) -> std::span<const float> {
        if (piece.empty()) {
            return {};
        }
        const PieceWidths::Index i = widths_.add(piece);
        if (std::find(next.wanted.begin(), next.wanted.end(), i) == next.wanted.end()) {
            next.wanted.push_back(i);
        }
        return widths_.table(i);
    };
    for (int note = 0; note < 128; ++note) {
        const auto i = static_cast<std::size_t>(note);
        next.names[i] = note_label(note, settings_);
        next.parts[i] = note_name_parts(note, settings_);
        next.accidental[i] = accidental_kind(next.parts[i].accidental);
        next.c_names[i] = c_marker_label(note, settings_);
        next.widths[i] = {table_of(next.parts[i].letter), table_of(next.parts[i].octave)};
    }
    names_pending_ = true;
    if (widths_.all_complete(next.wanted)) {
        apply_names();
        return;
    }
    // Until the widths are in, the names drawn are the old ones: leave out every one that reads
    // differently now rather than show it in its old form.
    for (std::size_t i = 0; i < 128; ++i) {
        name_stale_[i] = names_[i] != next.names[i];
        c_stale_[i] = c_names_[i] != next.c_names[i];
    }
}

void Scene::apply_names() {
    NameSet& next = next_names_;
    names_ = std::move(next.names);
    name_parts_ = std::move(next.parts);
    name_accidental_ = next.accidental;
    name_widths_ = next.widths;
    c_names_ = std::move(next.c_names);
    names_pending_ = false;
    name_stale_.fill(false);
    c_stale_.fill(false);
    geometry_valid_ = false; // the key names' size follows from the names
}

// Measures what the next frame's names are waiting for, a slice at a time, and swaps them in when
// they are all there; the names drawn until then stay. With nothing waiting, measures the pieces
// no setting has asked for yet, so that turning on solfege or flats later finds them done.
void Scene::pump_names() {
    if (names_pending_) {
        if (measure_pieces(next_names_.wanted, kNameSliceNs)) {
            apply_names();
        }
        return;
    }
    if (warmed_ || ++warm_frames_ <= kWarmAfterFrames) {
        return;
    }
    if (warm_pieces_.empty()) {
        for (const std::string& piece : possible_name_pieces()) {
            warm_pieces_.push_back(widths_.add(piece));
        }
    }
    warmed_ = measure_pieces(warm_pieces_, kWarmSliceNs);
}

SDL_FColor Scene::fill_for(int note, int velocity) const {
    if (outside_scale(note)) {
        return kWrongRed; // a played note outside the scale: the same red as a wrong press
    }
    if (settings_.color_scheme == ColorScheme::ByVelocity) {
        return to_fcolor(
            colours_.colour(note, static_cast<std::uint8_t>(velocity & 127), kFreePlayHand));
    }
    return fill_[static_cast<std::size_t>(note_index(note))];
}

SDL_FColor Scene::key_label_colour_for(int note, int velocity) const {
    if (settings_.color_scheme == ColorScheme::ByVelocity) {
        return to_fcolor(
            colours_.key_label(note, static_cast<std::uint8_t>(velocity & 127), kFreePlayHand));
    }
    return key_label_colour_[static_cast<std::size_t>(note_index(note))];
}

SDL_FColor Scene::label_colour_for(int note, int velocity) const {
    if (settings_.color_scheme == ColorScheme::ByVelocity) {
        return to_fcolor(
            colours_.label(note, static_cast<std::uint8_t>(velocity & 127), kFreePlayHand));
    }
    return label_colour_[static_cast<std::size_t>(note_index(note))];
}

void Scene::update_geometry(const KeyboardLayout& layout, const KeyViewFrame& view, float width,
                            float height) {
    const KeyRange& range = layout.range();
    if (geometry_valid_ && width == width_ && height == height_ && range.lowest == range_.lowest &&
        range.highest == range_.highest && layout.blocks() == blocks_ && view.pan == pan_ &&
        view.span_whites == span_) {
        return;
    }
    // A new size or keyboard range starts the name cache again; a look, a pan or a zoom does not
    // (a slot is keyed by its text and size, and tinted when drawn).
    const bool restart = width != width_ || height != height_ || range.lowest != range_.lowest ||
                         range.highest != range_.highest;
    geometry_valid_ = true;
    width_ = width;
    height_ = height;
    range_ = range;
    blocks_ = layout.blocks();
    pan_ = view.pan;
    span_ = view.span_whites;
    unit_ = std::max(height / 1080.0f, 0.25f);
    white_width_ = width / std::max(view.span_whites, 1.0f);
    // Pixels per millimetre (sets the top row and the physical text sizes): the UI's, else 96 dpi.
    px_per_mm_ = kDefaultPxPerMm;
    if (SDL_Window* window = gfx::window_of(renderer_); window != nullptr) {
        const float density = SDL_GetWindowPixelDensity(window);
        if (density > 0.0f) {
            px_per_mm_ = density * kDefaultPxPerMm;
            pixel_density_ = density;
        }
    }
    px_per_mm_ = px_per_mm_set_ > 0.0f ? px_per_mm_set_ : px_per_mm_; // `set_px_per_mm`
    // The top row is always reserved (the UI draws the status, gear and bar in it).
    layout_ = compute_layout(width, height, px_per_mm_, settings_, pinned_bar_, 1.0f, ui_grow_);
    if (falling_set_) {
        layout_.note_area = falling_area_;
    }
    min_name_px_ = min_name_font_px(px_per_mm_);
    min_small_name_px_ = min_small_name_font_px(px_per_mm_);
    for (int note = kMidiNoteMin; note <= kMidiNoteMax; ++note) {
        const KeyRect k = view.to_window(layout.key(note), layout.white_key_count());
        keys_[static_cast<std::size_t>(note)] = {k.x * width, k.width * width, k.black,
                                                 range.contains(note) && k.width > 0.0f};
        cols_[static_cast<std::size_t>(note)] = block_column(layout, view, note, width);
    }
    ++keys_epoch_;
    if (restart) {
        name_cache_.clear();
    }

    // One font size for all white keys and one for all black keys, so names do not jump between
    // sizes along the keyboard: each fits the longest name of its kind.
    white_name_px_ = 0.0f;
    black_name_px_ = 0.0f;
    c_marker_px_ = 0.0f;
    if (layout_.c_markers_visible) {
        // The same size for every C: it fits the longest marker ("C-1") on a white key.
        int chars = 0;
        for (int note = range.lowest; note <= range.highest; ++note) {
            const auto i = static_cast<std::size_t>(note);
            if (keys_[i].in_range) {
                chars = std::max(chars, static_cast<int>(c_names_[i].size()));
            }
        }
        const float gap = std::max(1.0f, 1.6f * unit_);
        c_marker_px_ = fit_label_font_px(layout_.c_marker_font_px, white_width_ - gap, chars);
    }
    if (layout_.key_labels_visible) {
        int white_chars = 0;
        int black_chars = 0;
        float black_width = 0.0f;
        for (int note = range.lowest; note <= range.highest; ++note) {
            const auto i = static_cast<std::size_t>(note);
            if (!keys_[i].in_range) {
                continue;
            }
            const int chars = static_cast<int>(names_[i].size());
            if (keys_[i].black) {
                black_chars = std::max(black_chars, chars);
                // The narrowest black key (they differ a little with EvenLanes).
                black_width =
                    black_width > 0.0f ? std::min(black_width, keys_[i].width) : keys_[i].width;
            } else {
                white_chars = std::max(white_chars, chars);
            }
        }
        const float gap = std::max(1.0f, 1.6f * unit_);
        white_name_px_ =
            fit_label_font_px(layout_.key_label_font_px, white_width_ - gap, white_chars);
        black_name_px_ = fit_label_font_px(layout_.key_label_font_px, black_width, black_chars);
    }
}

void Scene::draw(const FrameInput& in) {
    int w = 0;
    int h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    ++frame_;
    pump_names();
    update_geometry(in.layout, in.view, static_cast<float>(w), static_cast<float>(h));
    trim_name_cache(); // before any slot is used, so no texture in use is destroyed
    prepare_key_label(in);
    prepare_combo(in);

    if (!has_epoch_) {
        has_epoch_ = true;
        epoch_ns_ = in.now_ns;
    }

    gfx::set_blend(renderer_, SDL_BLENDMODE_BLEND);
    // Hit effects go beneath the blocks and the names over everything in the note area.
    for (const SceneLayer layer : kSceneLayers) {
        switch (layer) {
        case SceneLayer::Background:
            draw_background(in.now_ns);
            break;
        case SceneLayer::HitEffects:
            draw_hit_effects(in);
            break;
        case SceneLayer::Blocks:
            draw_notes(in);
            break;
        case SceneLayer::Keyboard:
            draw_keyboard(in);
            break;
        case SceneLayer::KeyLabel:
            draw_key_label();
            break;
        case SceneLayer::NoteText:
            draw_note_text(in);
            break;
        case SceneLayer::Combo:
            draw_combo();
            break;
        case SceneLayer::ChordLabel:
            draw_chord_labels(in);
            break;
        case SceneLayer::Prompt:
            draw_prompt(in);
            break;
        }
    }
}

// 1 at the note-on, falling to 0 over `PressEffects::flash_ns`; 0 with Key flash off.
float Scene::flash_at(std::int64_t now_ns, std::int64_t pressed_ns) const {
    return press_flash(press_state(), now_ns - pressed_ns, look_.press);
}

SDL_FColor Scene::hand_fill(int note, int velocity, Hand hand) const {
    if (hand == kFreePlayHand) {
        return fill_for(note, velocity);
    }
    return to_fcolor(
        colours_.colour(note_index(note), static_cast<std::uint8_t>(velocity & 127), hand));
}

Scene::NotePaint Scene::paint(int note, int velocity, bool held, float flash, Hand hand) const {
    NotePaint p;
    const SDL_FColor fill = hand_fill(note, velocity, hand);
    const PressPaint pp = press_paint(press_state(), held, flash, recipe_.glow_alpha, look_.press);
    const float whiten = pp.whiten;
    p.base = mix(fill, with_alpha(kWhite, fill.a), whiten);
    const SDL_FColor key_end = held ? mix(fill, kWhite, look_.held_highlight) : fill;
    p.lower = with_alpha(mix(key_end, kWhite, whiten), fill.a);
    // The style's own glow, more and wider while held (its own switch) and a pulse on each press.
    p.glow = pp.glow;
    p.glow_reach = pp.reach;
    p.wash = pp.wash;
    return p;
}

SDL_FPoint Scene::draw_name(const std::string& text, float size, float centre_x, float bottom,
                            SDL_FColor colour, bool bold) {
    TextSlot* slot = name_slot(text, size, bold);
    if (slot == nullptr) {
        return {0.0f, 0.0f};
    }
    text_.draw(*slot, centre_x, bottom - slot->height, Align::Centre, colour);
    return {slot->width, slot->height};
}

TextSlot* Scene::name_slot(const std::string& text, float size, bool bold) {
    // Even sizes, like the text cache rounds to, so the cache key is the size actually drawn.
    const int px = std::max(2, static_cast<int>(size) & ~1);
    auto it = name_cache_.find({text, px, bold});
    const bool made = it != name_cache_.end() &&
                      Text::is_prepared(it->second, text, static_cast<float>(px), -1, bold);
    if (!made && raster_budget_spent()) {
        return nullptr;
    }
    if (it == name_cache_.end()) {
        it = name_cache_
                 .emplace(std::piecewise_construct, std::forward_as_tuple(text, px, bold),
                          std::forward_as_tuple())
                 .first;
    }
    TextSlot& slot = it->second;
    slot.used = frame_;
    if (!made) {
        const std::int64_t start = frame_clock_ns();
        text_.prepare(slot, text, static_cast<float>(px), bold);
        raster_budget().spent_ns += frame_clock_ns() - start;
    }
    return &slot;
}

void Scene::trim_name_cache() {
    for (std::size_t dropped = 0; dropped < kEvictPerFrame && name_cache_.size() > kMaxCachedNames;
         ++dropped) {
        auto oldest = name_cache_.begin();
        for (auto it = name_cache_.begin(); it != name_cache_.end(); ++it) {
            if (it->second.used < oldest->second.used) {
                oldest = it;
            }
        }
        name_cache_.erase(oldest);
    }
}

// A key's name: its letter and octave as text, an accidental as the drawn glyph the block names
// use, centred on the key as one piece. The glyph goes into `mesh_`, which the caller draws.
void Scene::draw_key_name(int note, float size, float centre_x, float bottom, SDL_FColor colour) {
    const auto i = static_cast<std::size_t>(note);
    const AccidentalKind kind = name_accidental_[i];
    if (kind == AccidentalKind::None) {
        draw_name(names_[i], size, centre_x, bottom, colour);
        return;
    }
    const NameParts& parts = name_parts_[i];
    TextSlot* letter = name_slot(parts.letter, size, false);
    TextSlot* octave = parts.octave.empty() ? nullptr : name_slot(parts.octave, size, false);
    if (letter == nullptr || (!parts.octave.empty() && octave == nullptr)) {
        return; // the budget is spent: the whole name waits, never half of it
    }
    const float px = static_cast<float>(std::max(2, static_cast<int>(size) & ~1));
    const float glyph_h = accidental_glyph_height_px(name_metrics_.cap_em * px);
    const float glyph_w = accidental_glyph_width_px(kind, glyph_h);
    const float octave_w = octave != nullptr ? octave->width : 0.0f;
    const float left = std::round(centre_x - 0.5f * (letter->width + glyph_w + octave_w));
    const float top = bottom - letter->height;
    text_.draw(*letter, left, top, Align::Left, colour);
    add_glyph(mesh_, accidental_shape(kind, glyph_h), left + letter->width,
              top + std::round(name_metrics_.cap_top_em * px), 0.0f, colour);
    if (octave != nullptr) {
        text_.draw(*octave, left + letter->width + glyph_w, top, Align::Left, colour);
    }
}

void Scene::draw_background(std::int64_t now_ns) {
    mesh_.clear();
    if (settings_.effective_background() == BackgroundStyle::Plain) {
        mesh_.rect({0.0f, 0.0f, width_, height_}, look_.background_bottom, look_.background_bottom);
    } else {
        mesh_.rect({0.0f, 0.0f, width_, height_}, look_.background_top, look_.background_bottom);
    }
    // Faint lanes at every C and F, like the gaps between groups of black keys.
    const RectF& notes = layout_.note_area;
    const float line = std::max(1.0f, unit_);
    for (int note = range_.lowest + 1; note <= range_.highest; ++note) {
        const int pc = pitch_class(note);
        if (pc != 0 && pc != 5) {
            continue;
        }
        const BlockColumn& lane = cols_[static_cast<std::size_t>(note)];
        if (!lane.visible) {
            continue;
        }
        const SDL_FColor c = pc == 0 ? look_.guide : look_.guide_faint;
        mesh_.rect({lane.lane_x - line * 0.5f, notes.y, line, notes.h}, c, c);
    }
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);

    // Motion: slow drifting light behind the notes, never over the bars above them.
    if (settings_.effective_background() == BackgroundStyle::Motion && budget_.motion_background &&
        !notes.empty()) {
        fx_.clear();
        build_motion_background(fx_, {notes.x, notes.y, notes.w, notes.h},
                                static_cast<double>(now_ns - epoch_ns_) * 1e-9);
        const SDL_Rect clip = effects_clip();
        gfx::set_clip(renderer_, &clip);
        fx_.draw(renderer_, SDL_BLENDMODE_ADD, textures_.glow());
        gfx::clear_clip(renderer_);
    }
}

} // namespace etude::app
