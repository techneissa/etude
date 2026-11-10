// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_view.hpp"

#include "sheet_view_internal.hpp"

#include "../gfx.hpp"
#include "../song_controller.hpp"
#include "../touch_device.hpp"
#include "sheet_name_fit.hpp"

#include "etude/hands_present.hpp"
#include "etude/note_colours.hpp"
#include "etude/screen_layout.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace etude::app {

namespace {

constexpr SDL_FColor kPlaceholder{0.0f, 0.0f, 0.0f, 0.12f};
// The thick blue playhead.
constexpr SDL_FColor kCursor{0.165f, 0.435f, 0.941f, 0.78f};
// The cursor's width and how far past the outer staff lines it reaches, in staff spaces; never
// thinner than `kCursorMinPx`.
constexpr float kCursorSpaces = 0.5f;
constexpr float kCursorReachSpaces = 1.0f;
constexpr float kCursorMinPx = 3.0f;
// A loop on the score: a tint over its bars' staves and a bracket above them, in the cursor's
// blue; stronger while it is being dragged.
constexpr float kLoopTintAlpha = 0.10f;
constexpr float kLoopDragTintAlpha = 0.18f;
// The bracket has its own band just past the cursor's reach, under the names' lane, so it never
// meets a name: its top edge this far above the reach, this thick.
constexpr float kLoopBracketSpaces = 0.3f;     // the bracket's thickness
constexpr float kLoopBracketRiseSpaces = 0.4f; // its top edge above the cursor's reach
// A resize is engraved again once the size has held this long.
constexpr std::int64_t kResizeSettleNs = 150'000'000;
constexpr float kRingFadeS = 0.25f;
bool same_size(const EngraveOptions& a, const EngraveOptions& b) {
    return std::fabs(a.width_px - b.width_px) < 0.5f &&
           std::fabs(a.staff_space_px - b.staff_space_px) < 0.05f &&
           a.spacing_linear == b.spacing_linear && a.spacing_non_linear == b.spacing_non_linear;
}
bool practised(HandFilter hands, const Note& n) {
    if (n.accompaniment || n.drum) {
        return false;
    }
    return hands == HandFilter::Both || (hands == HandFilter::Left && n.hand == Hand::Left) ||
           (hands == HandFilter::Right && n.hand == Hand::Right);
}

} // namespace

SheetNameRoom SheetView::name_room(float px_per_mm) const {
    if (!names_on_) {
        return {};
    }
    const bool chords = helps_look_.sheet_chords == SheetChords::Symbols;
    return sheet_name_room(chords ? chord_stacks_ : stacks_, px_per_mm, line_em_);
}

SheetView::SheetView(SDL_Renderer* renderer)
    : renderer_(renderer), worker_(std::make_unique<SheetWorker>()), text_(renderer) {
    line_em_ = std::max(1.0f, text_.measure("Hg", 32.0f).y) / 32.0f;
    GestureRecognizer::Config c;
    c.drag_after_long_press = true; // a long press drags a loop across bars
    gestures_.set_config(c);
}

void SheetView::colour_marks(const SongController& songs, const Engraving& e, int system,
                             double tick) {
    const Song& song = songs.song().song;
    const SheetMap& map = e.map;
    const auto& marks = e.marks_of_system[static_cast<std::size_t>(system)];
    for (const std::uint32_t m : marks) {
        const SheetMark& mk = e.layout.marks[m];
        if (mk.kind != SheetMarkKind::Note) {
            continue;
        }
        const int note = sheet_mark_note(map, song, m, tick);
        const SheetMarkState st =
            note >= 0 ? sheet_state_for_grade(songs.grade_of(static_cast<std::uint32_t>(note)))
                      : SheetMarkState::Ahead;
        std::uint8_t colour = kInk;
        if (st == SheetMarkState::Played) {
            colour = kPlayed;
        } else if (st == SheetMarkState::Missed) {
            colour = kMissed;
        } else if (map.mark_first[m] != map.mark_first[m + 1] &&
                   !practised(hands_, song.notes[map.mark_notes[map.mark_first[m]]])) {
            colour = kOtherHand;
        }
        mark_colour_[m] = colour;
        // The ring fades once the note is graded and comes back when the mark is ahead again; a
        // new engraving's marks (below 0) start where they are, so a played note never flashes.
        const float target = st == SheetMarkState::Ahead ? 1.0f : 0.0f;
        float& ring = ring_alpha_[m];
        if (reduced_ || ring < 0.0f) {
            ring = target;
        } else if (ring < target) {
            ring = std::min(target, ring + fade_dt_ / kRingFadeS);
        } else if (ring > target) {
            ring = std::max(target, ring - fade_dt_ / kRingFadeS);
        }
    }
    // A chord's stem follows its notes.
    for (const std::uint32_t m : marks) {
        const SheetMark& mk = e.layout.marks[m];
        if (mk.kind != SheetMarkKind::Stem) {
            continue;
        }
        int notes = 0;
        int ahead = 0;
        int missed = 0;
        int other = 0;
        if (mk.chord >= 0 && static_cast<std::size_t>(mk.chord) + 1 < map.chord_first.size()) {
            for (std::uint32_t i = map.chord_first[static_cast<std::size_t>(mk.chord)];
                 i < map.chord_first[static_cast<std::size_t>(mk.chord) + 1]; ++i) {
                const std::uint8_t c = mark_colour_[map.chord_marks[i]];
                ++notes;
                ahead += c == kInk || c == kOtherHand ? 1 : 0;
                other += c == kOtherHand ? 1 : 0;
                missed += c == kMissed ? 1 : 0;
            }
        }
        const SheetMarkState st = sheet_stem_state(notes, ahead, missed);
        mark_colour_[m] = st == SheetMarkState::Played  ? kPlayed
                          : st == SheetMarkState::Missed ? kMissed
                          : notes > 0 && other == notes  ? kOtherHand
                                                         : kInk;
    }
}

void SheetView::update(const SongController& songs, const SheetPlan& engrave,
                       const SheetPlan& shown, std::int64_t now_ns, bool reduced_effects) {
    fade_dt_ = last_update_ns_ > 0 ? std::clamp(static_cast<float>(now_ns - last_update_ns_) * 1e-9f, 0.0f, 0.1f)
                                   : 0.0f;
    last_update_ns_ = now_ns;
    reduced_ = reduced_effects;
    if (!shown.shown) {
        // A touch in progress when the score hides is dropped: its lift must not set a loop when
        // the score comes back.
        drop_gesture();
    }
    take_results(now_ns);
    if (!has_song_ || song_ == nullptr) {
        return;
    }
    // Engrave at the size wanted: at once for a new song, after the size holds still otherwise (a
    // resize, another view), while the engraving on screen stays up scaled.
    if (engrave.shown && (!answered_ || offered_)) {
        EngraveOptions want;
        want.width_px = std::round(engrave.system_width_px);
        want.staff_space_px = std::round(engrave.staff_space_px * 4.0f) / 4.0f;
        const SheetNameSpacing spacing = sheet_name_spacing(names_on_, helps_look_.sheet_naming);
        want.spacing_linear = spacing.linear;
        want.spacing_non_linear = spacing.non_linear;
        if (!same_size(want, want_options_)) {
            want_options_ = want;
            want_since_ns_ = now_ns;
        }
        const bool due = asked_ == 0 || (!same_size(want, asked_options_) &&
                                         now_ns - want_since_ns_ >= kResizeSettleNs);
        if (due) {
            SheetJob job;
            job.generation = next_generation_++;
            job.song_serial = song_serial_;
            job.song = song_;
            job.options = want;
            worker_->engrave(std::move(job));
            asked_ = next_generation_ - 1;
            asked_options_ = want;
            if (song_job_ns_ == 0) {
                song_job_ns_ = now_ns;
            }
        }
    }
    const Song& song = songs.song().song;
    tick_ = song.tempo.tick_at(songs.display_position_s());
    hands_ = songs.practice_hands();

    // The new engraving takes over once it has the pictures the view shows first; at once while
    // the score is hidden, which draws nothing.
    if (back_.ready) {
        const SheetCursor c = sheet_cursor_at(back_.map, tick_);
        const int top = std::max(0, c.system);
        const int last = std::min(static_cast<int>(back_.layout.systems.size()) - 1,
                                  top + std::max(1, shown.systems) - 1);
        bool complete = true;
        for (int s = top; s <= last && shown.shown; ++s) {
            complete = complete && picture_of(back_, s) != nullptr;
        }
        if (complete) {
            drop_pictures(front_);
            front_ = std::move(back_);
            back_ = Engraving{};
            std::size_t largest = 0;
            for (const auto& list : front_.marks_of_system) {
                largest = std::max(largest, list.size());
            }
            vertices_.assign(largest * 4, SDL_Vertex{});
            indices_.resize(largest * 6);
            for (std::size_t q = 0; q < largest; ++q) {
                const int b = static_cast<int>(q * 4);
                int* ix = &indices_[q * 6];
                ix[0] = b;
                ix[1] = b + 1;
                ix[2] = b + 2;
                ix[3] = b;
                ix[4] = b + 2;
                ix[5] = b + 3;
            }
            mark_colour_.assign(front_.layout.marks.size(), kInk);
            ring_alpha_.assign(front_.layout.marks.size(), -1.0f);
            turn_from_ = turn_to_ = top_ = static_cast<float>(top);
            turn_ns_ = now_ns;
        } else {
            ask_pictures(back_, top);
        }
    }
    if (!front_.ready) {
        cursor_ = SheetCursor{};
        return;
    }
    cursor_ = sheet_cursor_at(front_.map, tick_);
    const float target = static_cast<float>(std::max(0, cursor_.system));
    if (target != turn_to_) {
        turn_from_ = top_;
        turn_to_ = target;
        turn_ns_ = now_ns;
    }
    top_ = sheet_turn_at(turn_from_, turn_to_, static_cast<float>(now_ns - turn_ns_) * 1e-9f,
                         reduced_effects);
    const int count = static_cast<int>(front_.layout.systems.size());
    // Hidden, the layout is kept for a switch, but nothing is drawn or coloured.
    if (shown.shown) {
        if (!back_.ready) {
            ask_pictures(front_, std::max(0, cursor_.system));
        }
        ready_helps();
        const float margin = shown.staff_space_px * kSheetMarginSpaces;
        if (std::fabs(shown.slot_height_px - name_slot_px_) > 0.5f ||
            std::fabs(shown.system_width_px - name_width_px_) > 0.5f || std::fabs(margin - name_margin_px_) > 0.5f) {
            name_slot_px_ = shown.slot_height_px; // the names stay in their slot
            name_width_px_ = shown.system_width_px;
            name_margin_px_ = margin;
            ++names_serial_;
        }
        if (front_.names_serial != names_serial_) {
            place_names(front_); // once per engraving and per change of the names or their room
        }
        run_gestures(songs, now_ns);
        const int first = static_cast<int>(std::floor(top_));
        for (int s = std::max(0, first); s <= first + std::max(1, shown.systems) && s < count; ++s) {
            colour_marks(songs, front_, s, tick_);
        }
    }
    stats_.textures = static_cast<int>(front_.pictures.size());
}

void SheetView::draw_system(const Engraving& e, const Picture* pic, int system, float x, float y,
                            float scale, const SheetPlan& plan) {
    const SheetSystem& sys = e.layout.systems[static_cast<std::size_t>(system)];
    if (pic == nullptr) {
        // Not drawn yet: faint staves where the music will be.
        const float space = e.options.staff_space_px * scale;
        const int staves = two_staves_ ? 2 : 1;
        const float top = y + sys.staff_top * scale;
        const float step = staves == 2 ? (sys.staff_bottom - sys.staff_top) * scale - 4.0f * space : 0.0f;
        gfx::set_colour(renderer_, kPlaceholder.r, kPlaceholder.g, kPlaceholder.b, kPlaceholder.a);
        for (int s = 0; s < staves; ++s) {
            for (int l = 0; l < 5; ++l) {
                const float ly = top + static_cast<float>(s) * (4.0f * space + step) +
                                 static_cast<float>(l) * space;
                gfx::line(renderer_, x, ly, x + plan.system_width_px, ly);
            }
        }
        return;
    }
    const SDL_FRect dst{x, y, static_cast<float>(pic->width) * scale,
                        static_cast<float>(pic->height) * scale};
    gfx::draw_texture(renderer_, pic->base, nullptr, &dst);
    if (colours_on_) {
        draw_rings(e, *pic, system, x, y, scale);
    }
    const auto& marks = e.marks_of_system[static_cast<std::size_t>(system)];
    const float tw = static_cast<float>(pic->width);
    const float th = static_cast<float>(pic->height);
    std::size_t q = 0;
    for (const std::uint32_t m : marks) {
        if (q * 4 + 4 > vertices_.size()) {
            break;
        }
        const RectF& b = e.layout.marks[m].box;
        const float x0 = std::max(0.0f, b.x - 1.0f);
        const float y0 = std::max(0.0f, b.y - 1.0f);
        const float x1 = std::min(tw, b.x + b.w + 1.0f);
        const float y1 = std::min(th, b.y + b.h + 1.0f);
        const SDL_FColor c = kMarkColours[std::min<std::size_t>(mark_colour_[m], 3)];
        SDL_Vertex* v = &vertices_[q * 4];
        v[0] = {{x + x0 * scale, y + y0 * scale}, c, {x0 / tw, y0 / th}};
        v[1] = {{x + x1 * scale, y + y0 * scale}, c, {x1 / tw, y0 / th}};
        v[2] = {{x + x1 * scale, y + y1 * scale}, c, {x1 / tw, y1 / th}};
        v[3] = {{x + x0 * scale, y + y1 * scale}, c, {x0 / tw, y1 / th}};
        ++q;
    }
    if (q > 0) {
        gfx::geometry(renderer_, pic->notes, vertices_.data(), static_cast<int>(q * 4),
                      indices_.data(), static_cast<int>(q * 6));
    }
    if (names_on_) {
        draw_names(e, system, x, y, scale);
    }
    if (system == cursor_.system) {
        const float space = e.options.staff_space_px * scale;
        const float w = std::max(kCursorMinPx, kCursorSpaces * space);
        const float top = y + sys.staff_top * scale - kCursorReachSpaces * space;
        const float bottom = y + sys.staff_bottom * scale + kCursorReachSpaces * space;
        const SDL_FRect r{std::round(x + cursor_.x * scale - w * 0.5f), top, w, bottom - top};
        gfx::set_colour(renderer_, kCursor.r, kCursor.g, kCursor.b, kCursor.a);
        gfx::fill_rect(renderer_, r);
    }
}

void SheetView::draw_loop_band(const Engraving& e, int first_bar, int last_bar, bool dragging) {
    if (first_bar < 0 || last_bar < first_bar ||
        static_cast<std::size_t>(last_bar) >= e.map.bar_written.size()) {
        return;
    }
    for (int i = 0; i < placed_count_; ++i) {
        const Placed& p = placed_[static_cast<std::size_t>(i)];
        const SheetSystem& sys = e.layout.systems[static_cast<std::size_t>(p.system)];
        const float space = e.options.staff_space_px * p.scale;
        const float top = p.y + sys.staff_top * p.scale - kCursorReachSpaces * space;
        const float bottom = p.y + sys.staff_bottom * p.scale + kCursorReachSpaces * space;
        float x0 = 1e9f;
        float x1 = -1e9f;
        for (int k = first_bar; k <= last_bar; ++k) {
            const int w = e.map.bar_written[static_cast<std::size_t>(k)];
            if (w < 0 || static_cast<std::size_t>(w) >= e.box_of_written.size() ||
                e.box_of_written[static_cast<std::size_t>(w)] < 0) {
                continue;
            }
            const SheetBar& b = e.layout.bars[static_cast<std::size_t>(e.box_of_written[static_cast<std::size_t>(w)])];
            if (b.system != p.system) {
                continue;
            }
            const SDL_FRect r{p.x + b.x0 * p.scale, top, (b.x1 - b.x0) * p.scale, bottom - top};
            gfx::set_colour(renderer_, kCursor.r, kCursor.g, kCursor.b,
                            dragging ? kLoopDragTintAlpha : kLoopTintAlpha);
            gfx::fill_rect(renderer_, r);
            x0 = std::min(x0, r.x);
            x1 = std::max(x1, r.x + r.w);
        }
        if (x1 > x0) {
            // The bracket over the loop's bars on this system.
            const float thick = std::max(2.0f, kLoopBracketSpaces * space);
            const float y = top - kLoopBracketRiseSpaces * space;
            const SDL_FRect bar{x0, y, x1 - x0, thick};
            const SDL_FRect left{x0, y, thick, thick * 2.5f};
            const SDL_FRect right{x1 - thick, y, thick, thick * 2.5f};
            gfx::set_colour(renderer_, kCursor.r, kCursor.g, kCursor.b, kCursor.a);
            gfx::fill_rect(renderer_, bar);
            gfx::fill_rect(renderer_, left);
            gfx::fill_rect(renderer_, right);
        }
    }
}

void SheetView::draw(const SheetPlan& plan) {
    placed_count_ = 0;
    paper_ = plan.shown ? plan.paper : RectF{};
    if (!plan.shown) {
        return;
    }
    gfx::set_blend(renderer_, SDL_BLENDMODE_BLEND);
    gfx::set_colour(renderer_, kPaper.r, kPaper.g, kPaper.b, kPaper.a);
    const SDL_FRect paper{plan.paper.x, plan.paper.y, plan.paper.w, plan.paper.h};
    gfx::fill_rect(renderer_, paper);
    const float margin = plan.staff_space_px * kSheetMarginSpaces;
    const RectF inner{plan.paper.x + margin, plan.paper.y + margin, plan.system_width_px,
                      plan.paper.h - 2.0f * margin};
    const SDL_Rect clip{static_cast<int>(std::floor(plan.paper.x)),
                        static_cast<int>(std::floor(plan.paper.y)),
                        static_cast<int>(std::ceil(plan.paper.w)),
                        static_cast<int>(std::ceil(plan.paper.h))};
    gfx::set_clip(renderer_, &clip);
    if (!front_.ready) {
        // Not engraved yet: faint staves in each slot.
        const float space = plan.staff_space_px;
        const int staves = two_staves_ ? 2 : 1;
        const float system_h = (two_staves_ ? kGrandStaffSpaces : kSingleStaffSpaces) * space;
        gfx::set_colour(renderer_, kPlaceholder.r, kPlaceholder.g, kPlaceholder.b, kPlaceholder.a);
        for (int slot = 0; slot < plan.systems; ++slot) {
            const float centre = inner.y + (static_cast<float>(slot) + 0.5f) * plan.slot_height_px;
            const float block = staves == 2 ? system_h * 0.7f : 4.0f * space;
            for (int s = 0; s < staves; ++s) {
                const float staff_top = centre - block * 0.5f + static_cast<float>(s) * (block - 4.0f * space);
                for (int l = 0; l < 5; ++l) {
                    const float ly = staff_top + static_cast<float>(l) * space;
                    gfx::line(renderer_, inner.x, ly, inner.right(), ly);
                }
            }
        }
        gfx::clear_clip(renderer_);
        return;
    }
    const float engraved = std::max(1.0f, front_.options.width_px);
    const int count = static_cast<int>(front_.layout.systems.size());
    const int first = static_cast<int>(std::floor(top_));
    for (int s = std::max(0, first); s <= first + plan.systems && s < count; ++s) {
        const float slot_top = inner.y + (static_cast<float>(s) - top_) * plan.slot_height_px;
        if (slot_top >= inner.bottom() || slot_top + plan.slot_height_px <= inner.y) {
            continue;
        }
        const SheetSystem& sys = front_.layout.systems[static_cast<std::size_t>(s)];
        const float scale = sheet_system_scale(sys, engraved, plan.system_width_px, plan.slot_height_px, margin);
        // The staves sit in the middle of the slot, so every system lines up the same way; a tall
        // system moves to stay inside it.
        float y = slot_top + plan.slot_height_px * 0.5f - (sys.staff_top + sys.staff_bottom) * 0.5f * scale;
        y = std::clamp(y, slot_top - margin * 0.5f,
                       std::max(slot_top - margin * 0.5f,
                                slot_top + plan.slot_height_px + margin * 0.5f - sys.height * scale));
        draw_system(front_, picture_of(front_, s), s, std::round(inner.x), std::round(y), scale, plan);
        if (placed_count_ < static_cast<int>(placed_.size())) {
            placed_[static_cast<std::size_t>(placed_count_++)] = {
                s, std::round(inner.x), std::round(y), scale,
                std::max(plan.paper.y, slot_top), std::min(plan.paper.bottom(), slot_top + plan.slot_height_px)};
        }
    }
    if (dragging_loop_) {
        draw_loop_band(front_, std::min(drag_anchor_, drag_last_), std::max(drag_anchor_, drag_last_), true);
    } else {
        draw_loop_band(front_, loop_first_, loop_last_, false);
    }
    gfx::clear_clip(renderer_);
}

} // namespace etude::app
