// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_names.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace etude {
namespace {

bool overlaps(const RectF& a, const RectF& b, float gap) {
    return a.x < b.right() + gap && b.x < a.right() + gap && a.y < b.bottom() + gap &&
           b.y < a.bottom() + gap;
}

// One thing a column shows, from the staff outwards.
struct Elem {
    SheetNameKind kind = SheetNameKind::Note;
    std::uint32_t mark = 0;
    float unit_width = 0.0f;
    float unit_height = 1.0f;
};

// A way to lay a column out: its size, the step sideways (0 straight, else +1 or -1: each name
// right or left of the one before) and how far out each name is from the one before, in lines.
struct Shape {
    float size = 0.0f;
    int dir = 0;
    float rise = 1.0f;
};

class Placer {
  public:
    Placer(const SheetSystem& sys, const SheetNameStyle& st) : sys_(sys), st_(st) {}

    // The rects a column may meet: the engraving's and the names already placed near `x`.
    void gather(float x, float reach, std::span<const RectF> obstacles, std::span<const RectF> placed) {
        near_.clear();
        near_placed_ = 0;
        for (const RectF& r : placed) {
            if (r.right() >= x - reach && r.x <= x + reach) {
                near_.push_back(r);
            }
        }
        near_placed_ = near_.size();
        for (const RectF& r : obstacles) {
            if (r.right() >= x - reach && r.x <= x + reach) {
                near_.push_back(r);
            }
        }
    }

    // Lays `elems` out by `shape` round `x`, in the lane and out past whatever it meets (with
    // the names placed before only when `with_placed`). False when that leaves the slot.
    bool lay(bool above, float x, std::span<const Elem> elems, const Shape& shape, bool with_placed,
             std::vector<RectF>& boxes) const {
        boxes.clear();
        const float size = shape.size;
        float cx = 0.0f;
        float lo = 0.0f;
        float hi = 0.0f;
        float off = 0.0f; // how far out from the lane's start
        for (std::size_t i = 0; i < elems.size(); ++i) {
            const float w = elems[i].unit_width * size;
            const float h = elems[i].unit_height * size;
            if (i > 0) {
                const float prev_h = elems[i - 1].unit_height * size;
                if (shape.dir != 0) {
                    const float prev = elems[i - 1].unit_width * size;
                    cx += static_cast<float>(shape.dir) *
                          ((prev + w) * 0.5f + std::max(st_.gap, kSheetNameSlopeGap * size));
                    off += shape.rise * std::max(prev_h, h);
                } else {
                    off += prev_h + st_.line_gap;
                }
            }
            const float y = above ? sys_.staff_top - st_.clear - h - off : sys_.staff_bottom + st_.clear + off;
            boxes.push_back({cx - w * 0.5f, y, w, h});
            lo = std::min(lo, cx - w * 0.5f);
            hi = std::max(hi, cx + w * 0.5f);
        }
        // Centred on the column, kept inside the system's width.
        float shift = x - (lo + hi) * 0.5f;
        if (sys_.width > 0.0f && hi - lo <= sys_.width) {
            shift = std::clamp(shift, -lo, sys_.width - hi);
        }
        for (RectF& b : boxes) {
            b.x += shift;
        }
        // Out past each rect a box touches, only ever outwards, so this ends: each move clears a
        // box of a rect for good (with a hair to spare against rounding).
        constexpr float kHair = 0.01f;
        const std::size_t first = with_placed ? 0 : near_placed_;
        for (bool moved = true; moved;) {
            moved = false;
            for (std::size_t i = 0; i < boxes.size() && !moved; ++i) {
                for (std::size_t k = first; k < near_.size(); ++k) {
                    const RectF& o = near_[k];
                    if (!overlaps(boxes[i], o, st_.gap)) {
                        continue;
                    }
                    const float d = above ? boxes[i].bottom() - (o.y - st_.gap) + kHair
                                          : o.bottom() + st_.gap - boxes[i].y + kHair;
                    if (d > 0.0f) {
                        for (RectF& b : boxes) {
                            b.y += above ? -d : d;
                        }
                        moved = true;
                        break;
                    }
                }
            }
            if (!in_room(boxes)) {
                return false;
            }
        }
        return in_room(boxes);
    }

  private:
    bool in_room(std::span<const RectF> boxes) const {
        for (const RectF& b : boxes) {
            if (b.y < st_.top_limit || b.bottom() > st_.bottom_limit) {
                return false;
            }
        }
        return true;
    }

    const SheetSystem& sys_;
    const SheetNameStyle& st_;
    std::vector<RectF> near_;
    std::size_t near_placed_ = 0;
};

} // namespace

Chord sheet_column_chord(std::span<const int> pitches) {
    int classes = 0;
    unsigned seen = 0;
    for (const int p : pitches) {
        const unsigned bit = 1u << static_cast<unsigned>(((p % 12) + 12) % 12);
        if ((seen & bit) == 0) {
            seen |= bit;
            ++classes;
        }
    }
    if (classes < 3) {
        return {};
    }
    Chord c = detect_chord(pitches);
    if (c.quality == ChordQuality::Power) {
        c.found = false;
    }
    return c;
}

void sheet_name_columns(const SheetLayout& layout, int system, std::span<const float> unit_width,
                        bool chord_symbols, std::vector<SheetNameColumn>& out) {
    out.clear();
    if (system < 0 || static_cast<std::size_t>(system) >= layout.systems.size()) {
        return;
    }
    const SheetSystem& sys = layout.systems[static_cast<std::size_t>(system)];
    // The notes starting together on one side, by written bar and offset.
    std::vector<std::tuple<bool, int, long, std::uint32_t>> keyed;
    for (std::uint32_t m = 0; m < layout.marks.size(); ++m) {
        const SheetMark& mk = layout.marks[m];
        if (mk.system != system || mk.kind != SheetMarkKind::Note || m >= unit_width.size() ||
            !(unit_width[m] > 0.0f)) {
            continue;
        }
        const bool above = mk.staff <= 0 || sys.staves <= 1;
        keyed.emplace_back(!above, mk.bar, std::lround(mk.offset_q * 960.0), m);
    }
    std::sort(keyed.begin(), keyed.end());
    std::vector<int> pitches;
    for (std::size_t i = 0; i < keyed.size();) {
        std::size_t j = i;
        SheetNameColumn c;
        c.above = !std::get<0>(keyed[i]);
        float sum = 0.0f;
        pitches.clear();
        while (j < keyed.size() && std::get<0>(keyed[j]) == std::get<0>(keyed[i]) &&
               std::get<1>(keyed[j]) == std::get<1>(keyed[i]) &&
               std::get<2>(keyed[j]) == std::get<2>(keyed[i])) {
            const std::uint32_t m = std::get<3>(keyed[j]);
            const SheetMark& mk = layout.marks[m];
            sum += mk.head.empty() ? mk.head_x : mk.head.x + mk.head.w * 0.5f;
            c.marks.push_back(m);
            pitches.push_back(mk.pitch);
            ++j;
        }
        c.x = sum / static_cast<float>(c.marks.size());
        std::sort(c.marks.begin(), c.marks.end(), [&](std::uint32_t a, std::uint32_t b) {
            return layout.marks[a].pitch > layout.marks[b].pitch;
        });
        if (chord_symbols) {
            c.chord = sheet_column_chord(pitches);
        }
        out.push_back(std::move(c));
        i = j;
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SheetNameColumn& a, const SheetNameColumn& b) { return a.x < b.x; });
}

void place_sheet_names(const SheetLayout& layout, int system, std::span<const SheetNameColumn> columns,
                       std::span<const float> unit_width, const SheetNameStyle& style, SheetNames& out) {
    std::vector<SheetName> keep_names = std::move(out.names);
    out = SheetNames{};
    out.names = std::move(keep_names); // its capacity, refilled
    out.names.clear();
    if (system < 0 || static_cast<std::size_t>(system) >= layout.systems.size() ||
        !(style.size > 0.0f) || columns.empty()) {
        return;
    }
    const SheetSystem& sys = layout.systems[static_cast<std::size_t>(system)];
    std::vector<RectF> obstacles;
    for (const SheetObstacle& o : layout.obstacles) {
        if (o.system == system && !o.box.empty()) {
            obstacles.push_back(o.box);
        }
    }
    // The sizes tried, the full one first: a small step down, never under min_size.
    const float full = style.size;
    const float floor_size = style.min_size > 0.0f ? std::min(full, style.min_size) : full;
    float sizes[3] = {full, 0.0f, 0.0f};
    int size_count = 1;
    if (floor_size < full - 0.5f) {
        if (full - floor_size > 0.15f * full) {
            sizes[size_count++] = (full + floor_size) * 0.5f;
        }
        sizes[size_count++] = floor_size;
    }

    Placer placer(sys, style);
    std::vector<RectF> placed;
    std::vector<RectF> boxes;
    std::vector<RectF> alone;
    std::vector<Elem> all;
    std::vector<Elem> elems;
    for (const SheetNameColumn& c : columns) {
        // From the staff outwards: above, the lowest name first; below, the highest.
        all.clear();
        const bool symbol = c.chord.found && c.symbol >= 0 && c.symbol_unit_width > 0.0f;
        if (symbol) {
            all.push_back({SheetNameKind::Symbol, c.marks.empty() ? 0u : c.marks.front(), c.symbol_unit_width, 1.0f});
        } else {
            for (const std::uint32_t m : c.marks) {
                if (m < unit_width.size() && unit_width[m] > 0.0f) {
                    all.push_back({SheetNameKind::Note, m, unit_width[m], 1.0f});
                }
            }
            if (c.above) {
                std::reverse(all.begin(), all.end());
            }
        }
        if (all.empty()) {
            continue;
        }
        const int n = static_cast<int>(all.size());
        float reach = 0.0f;
        for (const Elem& e : all) {
            reach += e.unit_width * full + style.gap;
        }
        reach = 2.0f * (reach + std::max(style.more_unit_width * full, 0.0f) + style.gap);
        placer.gather(c.x, reach, obstacles, placed);

        // Straight, then on a slope (gentler first, right then left), at each size in turn.
        bool done = false;
        Shape shape;
        float at_x = c.x;
        const auto attempt = [&](std::span<const Elem> es, const Shape& s) {
            if (placer.lay(c.above, at_x, es, s, true, boxes)) {
                shape = s;
                elems.assign(es.begin(), es.end());
                done = true;
            }
            return done;
        };
        const auto shapes_at = [&](std::span<const Elem> es, float size) {
            if (attempt(es, {size, 0, 1.0f})) {
                return true;
            }
            if (es.size() < 2) {
                return false;
            }
            for (const float rise : kSheetNameSlopeRises) {
                for (const int dir : {1, -1}) {
                    if (attempt(es, {size, dir, rise})) {
                        return true;
                    }
                }
            }
            return false;
        };
        for (int s = 0; s < size_count && !done; ++s) {
            shapes_at(all, sizes[s]);
        }
        // Then the names nearest the staff give way to "…", the outermost kept longest.
        int keep = n;
        if (!done) {
            std::vector<Elem> part;
            const Elem more{SheetNameKind::More, c.marks.empty() ? 0u : c.marks.front(), style.more_unit_width,
                            style.more_unit_height};
            for (keep = symbol ? 0 : n - 1; keep >= 0 && !done; --keep) {
                part.clear();
                part.push_back(more);
                part.insert(part.end(), all.end() - keep, all.end());
                if (shapes_at(part, floor_size)) {
                    break;
                }
            }
            // The mark alone, a little to either side of a column with no room over it.
            const float step = style.more_unit_width * floor_size + style.gap;
            for (int k = 1; k <= kSheetNameMoreSideSteps && !done; ++k) {
                for (const float side : {1.0f, -1.0f}) {
                    at_x = c.x + side * static_cast<float>(k) * step;
                    placer.gather(at_x, reach, obstacles, placed);
                    if (attempt(std::span<const Elem>(&more, 1), {floor_size, 0, 1.0f})) {
                        keep = 0;
                        break;
                    }
                }
            }
        }
        if (!done) {
            out.dropped += n;
            continue;
        }
        // Staggered: further out than the engraving alone would put it.
        placer.lay(c.above, at_x, elems, shape, false, alone);
        const bool staggered = !alone.empty() && std::fabs(alone.front().y - boxes.front().y) > 0.5f;
        for (std::size_t i = 0; i < elems.size(); ++i) {
            const Elem& e = elems[i];
            SheetName name;
            name.kind = e.kind;
            name.mark = e.mark;
            name.symbol = e.kind == SheetNameKind::Symbol ? c.symbol : -1;
            name.box = boxes[i];
            name.size = shape.size;
            out.names.push_back(name);
            placed.push_back(boxes[i]);
            if (e.kind == SheetNameKind::More) {
                ++out.marks;
                continue;
            }
            out.symbols += e.kind == SheetNameKind::Symbol ? 1 : 0;
            out.staggered += staggered ? 1 : 0;
            out.sloped += shape.dir != 0 ? 1 : 0;
            out.smaller += shape.size < full - 0.01f ? 1 : 0;
        }
        out.left_out += n - static_cast<int>(std::count_if(elems.begin(), elems.end(), [](const Elem& e) {
                            return e.kind != SheetNameKind::More;
                        }));
    }
    out.size = out.names.empty() ? 0.0f : full;
}

SheetNameStacks sheet_name_stacks(const Song& song, bool two_staves, bool chord_symbols) {
    SheetNameStacks s;
    std::vector<int> above_pitches;
    std::vector<int> below_pitches;
    const auto count = [&](const std::vector<int>& pitches) {
        if (chord_symbols && sheet_column_chord(pitches).found) {
            return 1;
        }
        return static_cast<int>(pitches.size());
    };
    // Song::notes is sorted by start tick: each run of equal starts per side.
    std::size_t i = 0;
    while (i < song.notes.size()) {
        const std::int64_t at = song.notes[i].start_tick;
        above_pitches.clear();
        below_pitches.clear();
        for (; i < song.notes.size() && song.notes[i].start_tick == at; ++i) {
            const Note& n = song.notes[i];
            if (n.drum || n.accompaniment) {
                continue;
            }
            (two_staves && n.hand == Hand::Left ? below_pitches : above_pitches).push_back(n.pitch);
        }
        s.above = std::max(s.above, count(above_pitches));
        s.below = std::max(s.below, count(below_pitches));
    }
    return s;
}

} // namespace etude
