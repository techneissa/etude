// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/block_names.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude {
namespace {

// Black keys' blocks over one white key's block at a name's height: a C's block meets one, a D's
// two, a repeated black note a few more. More than this and the white key's name is left out.
constexpr std::size_t kMaxCovers = 16;

bool finite_rect(const RectF& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h);
}

struct Visible {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float y0 = 0.0f;
    float y1 = 0.0f;
};

// The part of the block inside the clip, false when there is none.
bool visible_part(const RectF& block, const RectF& clip, Visible& v) {
    v.x0 = std::max(block.x, clip.x);
    v.y0 = std::max(block.y, clip.y);
    v.x1 = std::min(block.right(), clip.right());
    v.y1 = std::min(block.bottom(), clip.bottom());
    return v.x1 > v.x0 && v.y1 > v.y0;
}

// Cuts the visible part of a block to what lies above `keep` when the block reaches into it: the
// name goes there, over the keep-clear rect's top edge, not in it.
void clear_of(Visible& v, const RectF& keep) {
    if (keep.empty() || !finite_rect(keep)) {
        return;
    }
    if (v.x1 > keep.x && v.x0 < keep.right() && v.y1 > keep.y && v.y0 < keep.bottom()) {
        v.y1 = std::min(v.y1, keep.y);
    }
}

bool has_letter(const NameWidths& w) { return !w.letter.empty(); }

// The width of a piece at `font` px, rounded up to whole pixels; false for a size the table does
// not hold.
bool piece_px(std::span<const float> table, float font, float& out) {
    const auto i = static_cast<std::size_t>(font);
    if (i >= table.size() || !std::isfinite(table[i]) || !(table[i] > 0.0f)) {
        return false;
    }
    out = std::ceil(table[i]);
    return true;
}

// How far a block's bottom edge is from the keys (the clip's bottom).
float key_distance(const BlockNameIn& in, const RectF& clip) {
    return std::fabs(clip.bottom() - in.block.bottom());
}

// True when `a` ranks before `b`: nearer the keys, then the lower note.
bool ranks_before(float da, int note_a, float db, int note_b) {
    return da < db || (da == db && note_a < note_b);
}

// The widest stretch of [v.x0, v.x1] that no black key's block covers between `y0` and `y1`, for a
// white key's name (black keys' blocks are drawn over it). False when there are too many to tell.
bool uncovered_span(const BlockNameIn& in, const Visible& v, float y0, float y1,
                    std::span<const BlockNameIn> blocks, float& x0, float& x1) {
    x0 = v.x0;
    x1 = v.x1;
    if (in.black) {
        return true;
    }
    std::array<float, kMaxCovers> from{};
    std::array<float, kMaxCovers> to{};
    std::size_t n = 0;
    for (const BlockNameIn& b : blocks) {
        if (!b.black || !finite_rect(b.block) || !(b.block.w > 0.0f) || !(b.block.h > 0.0f) ||
            b.block.right() <= v.x0 || b.block.x >= v.x1 || b.block.bottom() <= y0 ||
            b.block.y >= y1) {
            continue;
        }
        if (n == kMaxCovers) {
            return false;
        }
        // Sorted by left edge as they come in.
        std::size_t j = n++;
        while (j > 0 && from[j - 1] > b.block.x) {
            from[j] = from[j - 1];
            to[j] = to[j - 1];
            --j;
        }
        from[j] = b.block.x;
        to[j] = b.block.right();
    }
    if (n == 0) {
        return true;
    }
    float best = -1.0f;
    float at = v.x0;
    for (std::size_t j = 0; j <= n; ++j) {
        const float end = j < n ? std::min(from[j], v.x1) : v.x1;
        if (end - at > best) {
            best = end - at;
            x0 = at;
            x1 = end;
        }
        if (j < n) {
            at = std::max(at, to[j]);
        }
    }
    return best > 0.0f;
}

// One option: `form` at `font` px with `pad_x` clear on each side of the box. Returns false when the
// box does not fit the block (width ignored when `spill`); `out` holds the geometry when it does.
bool make_option(const BlockNameIn& in, const Visible& v, const BlockNameParams& params,
                 std::span<const BlockNameIn> blocks, NameForm form, float font, bool spill,
                 float pad_x, BlockNamePlacement& out) {
    const bool octave = form == NameForm::Full;
    const float outline = name_outline_px(font);
    BlockNamePlacement p;
    p.form = form;
    p.font_px = font;
    p.outline_px = outline;
    if (!piece_px(in.width.letter, font, p.letter_w) ||
        (octave && !piece_px(in.width.octave, font, p.octave_w))) {
        return false;
    }
    float text_h = std::ceil(params.line_em * font);
    if (in.accidental != AccidentalKind::None) {
        p.accidental = in.accidental;
        p.glyph_h = accidental_glyph_height_px(params.cap_em * font);
        p.glyph_w = accidental_glyph_width_px(in.accidental, p.glyph_h);
        p.glyph_dy = std::round(params.cap_top_em * font);
        text_h = std::max(text_h, p.glyph_dy + p.glyph_h);
    }
    const float box_w = p.letter_w + p.glyph_w + p.octave_w + 2.0f * outline;
    const float box_h = text_h + 2.0f * outline;
    if (v.y1 - v.y0 < box_h + 2.0f * kNamePadYPx) {
        return false;
    }
    const float y = v.y1 - kNamePadYPx - box_h;
    float x0 = v.x0;
    float x1 = v.x1;
    if (!uncovered_span(in, v, y, y + box_h, blocks, x0, x1)) {
        return false;
    }
    if (!spill && x1 - x0 < box_w + 2.0f * pad_x) {
        return false;
    }
    const float cx = 0.5f * (x0 + x1);
    float x = std::round(cx - 0.5f * box_w); // on a whole pixel, so the text is crisp
    if (spill && x1 - x0 < box_w) {
        // Past the block's sides, but inside the key's lane and the clip.
        const bool lane = std::isfinite(in.lane_x) && std::isfinite(in.lane_w) && in.lane_w > 0.0f;
        const float lane0 = std::max(lane ? in.lane_x : in.block.x, params.clip.x);
        const float lane1 =
            std::min(lane ? in.lane_x + in.lane_w : in.block.right(), params.clip.right());
        if (lane1 - lane0 < box_w) {
            return false;
        }
        x = std::clamp(x, std::ceil(lane0), std::floor(lane1 - box_w));
        if (x < lane0 || x + box_w > lane1) {
            return false; // no whole-pixel place inside the lane
        }
    } else if (x1 - x0 >= box_w) {
        // Rounding never pushes it out of the block, nor into the padding where there is room.
        const float pad = x1 - x0 >= box_w + 2.0f * pad_x ? pad_x : 0.0f;
        x = std::clamp(x, x0 + pad, x1 - box_w - pad);
    }
    p.rect = RectF{x, y, box_w, box_h};
    out = p;
    return true;
}

bool clear_of_placed(const RectF& rect, std::span<const BlockNamePlacement> out,
                     std::span<const std::uint32_t> placed) {
    for (const std::uint32_t j : placed) {
        if (rects_overlap(rect, out[j].rect)) {
            return false;
        }
    }
    return true;
}

// The largest size worth trying for `in`: the planner's maximum, cut to its width table.
float top_size(const BlockNameIn& in, float max_font) {
    return std::min(max_font, static_cast<float>(in.width.letter.size()) - 1.0f);
}

} // namespace

float name_outline_px(float font_px) { return std::max(1.0f, std::round(font_px * 0.08f)); }

bool rects_overlap(const RectF& a, const RectF& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

float name_clearance_px(const BlockNameParams& params, float font_px) {
    if (!(font_px > 0.0f)) {
        return 0.0f;
    }
    const float glyph_h = accidental_glyph_height_px(params.cap_em * font_px);
    const float text_h = std::max(std::ceil(params.line_em * font_px),
                                  std::round(params.cap_top_em * font_px) + glyph_h);
    return kNamePadYPx + text_h + 2.0f * name_outline_px(font_px);
}

std::size_t plan_block_names(const BlockNameParams& params_in, std::span<const BlockNameIn> blocks,
                             std::span<BlockNamePlacement> out) {
    BlockNameParams params = params_in; // with the font's metrics made usable
    if (!std::isfinite(params.cap_em) || !(params.cap_em > 0.0f)) {
        params.cap_em = BlockNameParams{}.cap_em;
    }
    if (!std::isfinite(params.cap_top_em) || params.cap_top_em < 0.0f) {
        params.cap_top_em = BlockNameParams{}.cap_top_em;
    }
    const std::size_t n = std::min(blocks.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = {};
    }
    blocks = blocks.first(n);
    if (n == 0 || params.max_names == 0 || !finite_rect(params.clip) || params.clip.empty() ||
        !std::isfinite(params.max_font_px) || !std::isfinite(params.line_em) ||
        !(params.line_em > 0.0f)) {
        return 0;
    }
    // Whole sizes: the minimum rounds up and the maximum down, so a drawn size is a planned one.
    // No name is taller than the clip, so no size above its height is worth a try.
    const float min_font = std::isfinite(params.min_font_px)
                               ? std::max(std::ceil(params.min_font_px), 1.0f)
                               : std::ceil(kMinNameFontPx);
    const float min_small =
        std::isfinite(params.min_small_font_px)
            ? std::min(std::max(std::ceil(params.min_small_font_px), 1.0f), min_font)
            : std::min(std::ceil(kMinSmallNameFontPx), min_font);
    const float max_font = std::floor(std::min(params.max_font_px, params.clip.h));
    if (!(max_font >= min_small)) {
        return 0;
    }

    // The blocks that may get a name, nearest the keys first, whatever order they came in: an
    // insertion into a list that keeps only the nearest, with no allocation.
    const std::size_t keep = std::min(params.max_names, kMaxNameCandidates);
    std::array<std::uint32_t, kMaxNameCandidates> order{};
    std::array<float, kMaxNameCandidates> dist{};
    std::size_t eligible = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const BlockNameIn& in = blocks[i];
        Visible v;
        if (!finite_rect(in.block) || !has_letter(in.width) ||
            (in.black && params.black_names == BlackKeyNames::Off) ||
            !visible_part(in.block, params.clip, v)) {
            continue;
        }
        const float d = key_distance(in, params.clip);
        if (eligible == keep &&
            !ranks_before(d, in.note, dist[eligible - 1], blocks[order[eligible - 1]].note)) {
            continue; // farther than every one kept
        }
        std::size_t j = eligible < keep ? eligible++ : eligible - 1;
        while (j > 0 && ranks_before(d, in.note, dist[j - 1], blocks[order[j - 1]].note)) {
            order[j] = order[j - 1];
            dist[j] = dist[j - 1];
            --j;
        }
        order[j] = static_cast<std::uint32_t>(i);
        dist[j] = d;
    }

    std::array<std::uint32_t, kMaxNameCandidates> placed{};
    std::size_t placed_count = 0;
    for (int pass = 0; pass < 2; ++pass) { // white keys' blocks first, then black keys'
        for (std::size_t k = 0; k < eligible; ++k) {
            const std::size_t i = order[k];
            const BlockNameIn& in = blocks[i];
            if (in.black != (pass == 1)) {
                continue;
            }
            Visible v;
            visible_part(in.block, params.clip, v);
            clear_of(v, params.keep_clear);
            const std::span<const std::uint32_t> taken(placed.data(), placed_count);
            const bool accidental = in.accidental != AccidentalKind::None;
            const bool octave = !in.width.octave.empty();
            BlockNamePlacement option;
            bool done = false;
            // Whole name at every size from the biggest down, then without the octave; then, with
            // an accidental, smaller than the legible minimum for a letter alone. All of that
            // first with `kNamePadXPx` clear to each side, so the outline never touches the
            // block's border; only when no size gives the name that room, with none (the box
            // still inside the block).
            for (const float pad_x : {kNamePadXPx, 0.0f}) {
                for (int form_i = octave ? 0 : 1; form_i < 2 && !done; ++form_i) {
                    const NameForm form = form_i == 0 ? NameForm::Full : NameForm::Short;
                    const float floor_px =
                        form == NameForm::Short && accidental ? min_small : min_font;
                    for (float font = top_size(in, max_font); font >= floor_px && !done;
                         font -= 1.0f) {
                        if (form == NameForm::Full && font < min_font) {
                            break;
                        }
                        done = make_option(in, v, params, blocks, form, font, false, pad_x,
                                           option) &&
                               clear_of_placed(option.rect, out, taken);
                    }
                }
                if (done || kNamePadXPx == 0.0f) {
                    break;
                }
            }
            if (!done && in.black && params.black_names == BlackKeyNames::Always) {
                done =
                    make_option(in, v, params, blocks, NameForm::Short, min_small, true, 0.0f, option) &&
                    clear_of_placed(option.rect, out, taken);
            }
            if (done) {
                out[i] = option;
                placed[placed_count++] = static_cast<std::uint32_t>(i);
            }
        }
    }
    return placed_count;
}

std::size_t name_candidate_slot(std::span<const BlockNameIn> held, std::size_t capacity,
                                const BlockNameIn& block, const RectF& clip) {
    if (held.size() < capacity) {
        return held.size();
    }
    std::size_t worst = capacity;
    float worst_d = 0.0f;
    for (std::size_t i = 0; i < held.size() && i < capacity; ++i) {
        const float d = key_distance(held[i], clip);
        // The last of equals is the one to give up, so the earlier one stays.
        if (worst == capacity || !ranks_before(d, held[i].note, worst_d, held[worst].note)) {
            worst = i;
            worst_d = d;
        }
    }
    if (worst == capacity ||
        !ranks_before(key_distance(block, clip), block.note, worst_d, held[worst].note)) {
        return capacity;
    }
    return worst;
}

float stand_in_height(float from_px, float from_w, float from_h, float px, float width,
                      float max_height, float max_skew) {
    const auto good = [](float v) { return std::isfinite(v) && v > 0.0f; };
    if (!good(from_px) || !good(from_w) || !good(from_h) || !good(px) || !good(width) ||
        !good(max_height)) {
        return 0.0f;
    }
    const float sy = px / from_px;
    const float sx = width / from_w;
    const float h = from_h * sy;
    if (h > max_height || std::abs(sx - sy) > max_skew * sy) {
        return 0.0f;
    }
    return h;
}

} // namespace etude
