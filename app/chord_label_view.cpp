// SPDX-License-Identifier: GPL-3.0-or-later
#include "chord_label_view.hpp"

#include "etude/accidental_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace etude::app {
namespace {

// The quality is this share of the root's size, raised to the root's capitals; the bass shares the
// quality's size and stands on the baseline.
constexpr float kQualityShare = 0.62f;
constexpr float kLineAlpha = 0.9f;

SDL_FColor faded(SDL_FColor c, float a) { return {c.r, c.g, c.b, c.a * std::clamp(a, 0.0f, 1.0f)}; }

} // namespace

ChordLabelView::ChordLabelView() = default;
ChordLabelView::~ChordLabelView() = default;

void ChordLabelView::invalidate() {
    for (Art& a : arts_) {
        a.valid = false;
        a.pieces.clear();
    }
}

// Lays a name out as pieces: the root (its letter, then each sign), the quality, the slash bass.
void ChordLabelView::lay(Text& text, Art& art, const ChordNameParts& name, float font) {
    const Text::VerticalMetrics& m = metrics_;
    art.pieces.clear();
    const float cap_top = m.cap_top_em * font;
    const float baseline = (m.cap_top_em + m.cap_em) * font;
    float x = 0.0f;
    const auto add_text = [&](const std::string& s, float size, bool bold, float cap_top_at) {
        if (s.empty()) {
            return;
        }
        Piece p;
        p.slot = std::make_unique<TextSlot>();
        const SDL_FPoint e = text.prepare_exact(*p.slot, s, size, bold);
        p.x = x;
        p.top = cap_top_at - m.cap_top_em * size;
        p.w = e.x;
        art.pieces.push_back(std::move(p));
        x += e.x;
    };
    const auto add_sign = [&](char sign, float size, float cap_top_at) {
        Piece p;
        p.kind = sign == '#' ? Piece::Sharp : Piece::Flat;
        p.h = accidental_glyph_height_px(m.cap_em * size);
        p.w = accidental_glyph_width_px(sign == '#' ? AccidentalKind::Sharp : AccidentalKind::Flat,
                                        p.h);
        p.x = x + std::round(0.05f * size);
        p.top = cap_top_at;
        x = p.x + p.w + std::round(0.04f * size);
        art.pieces.push_back(std::move(p));
    };
    add_text(name.root.substr(0, 1), font, true, cap_top);
    for (std::size_t i = 1; i < name.root.size(); ++i) {
        add_sign(name.root[i], font, cap_top);
    }
    const float quality_px = std::round(kQualityShare * font);
    x += std::round(0.05f * font);
    // The quality: runs of text split at the signs ('b' and '#' are always signs in a quality).
    std::string run;
    for (const char c : name.quality) {
        if (c == 'b' || c == '#') {
            add_text(run, quality_px, false, cap_top);
            run.clear();
            add_sign(c, quality_px, cap_top);
        } else {
            run += c;
        }
    }
    add_text(run, quality_px, false, cap_top);
    if (!name.bass.empty()) {
        x += std::round(0.03f * font);
        const float bass_top = baseline - m.cap_em * quality_px;
        add_text("/" + name.bass.substr(0, 1), quality_px, false, bass_top);
        for (std::size_t i = 1; i < name.bass.size(); ++i) {
            add_sign(name.bass[i], quality_px, bass_top);
        }
    }
    art.width = x;
}

// The names, over their lines.
void ChordLabelView::draw_names(SDL_Renderer* renderer, Text& text, Mesh& mesh,
                                const ChordLabelFrame& in, const ChordLabelGeometry& g,
                                std::span<Shown> shown, std::span<const ChordNamePlace> places) {
    const std::size_t count = shown.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (!places[i].placed) {
            continue;
        }
        const SDL_FColor c =
            faded(in.colours[static_cast<std::size_t>(shown[i].hand)], shown[i].alpha);
        const float left = std::round(places[i].left);
        mesh.clear();
        for (Piece& p : shown[i].art->pieces) {
            const float px = std::round(left + p.x);
            const float py = std::round(g.name_top + p.top);
            if (p.kind == Piece::Text) {
                text.draw(*p.slot, px, py, Align::Left, c);
                continue;
            }
            const AccidentalShape shape = accidental_shape(
                p.kind == Piece::Sharp ? AccidentalKind::Sharp : AccidentalKind::Flat, p.h);
            for (std::size_t q = 0; q < shape.count; ++q) {
                const GlyphQuad& quad = shape.quads[q];
                SDL_FPoint pt[4];
                for (std::size_t k = 0; k < 4; ++k) {
                    pt[k] = {px + quad.x[k], py + quad.y[k]};
                }
                mesh.triangle(pt[0], pt[1], pt[2], c, c, c);
                mesh.triangle(pt[0], pt[2], pt[3], c, c, c);
            }
        }
        mesh.draw(renderer, SDL_BLENDMODE_BLEND);
        if (shown[i].alpha > 0.0f) {
            const float x0 = std::min(left, shown[i].x0);
            const float x1 = std::max(left + shown[i].art->width, shown[i].x1);
            RectF& rect = rects_[static_cast<std::size_t>(shown[i].hand)];
            const float top = rect.empty() ? g.name_top : std::min(rect.y, g.name_top);
            const float left_edge = rect.empty() ? x0 : std::min(rect.x, x0);
            const float right_edge = rect.empty() ? x1 : std::max(rect.right(), x1);
            rect = {left_edge, top, right_edge - left_edge, g.line_y + g.line_thickness - top};
        }
    }
}

void ChordLabelView::draw(SDL_Renderer* renderer, Text& text, Mesh& mesh,
                          const ChordLabelFrame& in) {
    rects_ = {};
    if (in.chords == nullptr || in.columns == nullptr || in.strip.empty() ||
        !(in.font_px >= 1.0f)) {
        for (ChordLabelFade& f : fades_) {
            f.reset();
        }
        return;
    }
    if (!have_serial_ || in.serial != serial_) {
        for (ChordLabelFade& f : fades_) {
            f.reset();
        }
        invalidate();
        serial_ = in.serial;
        have_serial_ = true;
    }
    if (!metrics_known_) {
        metrics_ = text.vertical_metrics();
        metrics_known_ = true;
    }
    const float font = std::round(in.font_px);
    const int font_key = static_cast<int>(font);
    const std::array<BlockColumn, 128>& columns = *in.columns;
    int first = -1;
    int last = -1;
    for (int k = 0; k < 128; ++k) {
        if (columns[static_cast<std::size_t>(k)].visible) {
            first = first < 0 ? k : first;
            last = k;
        }
    }

    // What each hand shows: the label fading out and the one fading in.
    std::array<Shown, kChordMaxNames> shown{};
    std::size_t count = 0;
    for (int hand = 0; hand < 2; ++hand) {
        const HandChordTimeline& timeline = hand == 0 ? in.chords->left : in.chords->right;
        ChordLabelFade& fade = fades_[static_cast<std::size_t>(hand)];
        fade.update(chord_label_at(timeline, in.time_s), in.visible, in.now_ns, in.reduced_effects);
        const std::size_t begin = count;
        for (const ChordLabelAlpha& item : fade.items()) {
            if (item.index < 0 || static_cast<std::size_t>(item.index) >= timeline.size() ||
                first < 0) {
                continue;
            }
            const HandChordLabel& label = timeline[static_cast<std::size_t>(item.index)];
            // The keys the line spans are those the blocks of its notes use, those the keyboard
            // shows: a chord wholly off it has no keys to stand over.
            const std::optional<ChordKeys> keys =
                chord_label_keys(label, in.transposition, first, last);
            if (!keys) {
                continue;
            }
            const BlockColumn& a = columns[static_cast<std::size_t>(keys->low)];
            const BlockColumn& b = columns[static_cast<std::size_t>(keys->high)];
            Shown& s = shown[count++];
            s.hand = hand;
            s.index = item.index;
            s.alpha = item.alpha;
            s.label = &label;
            s.x0 = a.x;
            s.x1 = b.x + b.w;
        }
        // A name is laid out once: an art that already holds this hand's label is reused.
        constexpr std::size_t kPerHand = kChordMaxLeaving + 1;
        std::array<Art*, kPerHand> pair{};
        for (std::size_t k = 0; k < kPerHand; ++k) {
            pair[k] = &arts_[static_cast<std::size_t>(hand) * kPerHand + k];
        }
        for (std::size_t i = begin; i < count; ++i) {
            for (Art* a : pair) {
                if (a->valid && a->index == shown[i].index && a->font == font_key) {
                    shown[i].art = a;
                }
            }
        }
        for (std::size_t i = begin; i < count; ++i) {
            if (shown[i].art != nullptr) {
                continue;
            }
            // The art not wanted by the label shown beside this one.
            Art* victim = pair[0];
            for (Art* a : pair) {
                bool wanted = false;
                for (std::size_t j = begin; j < count; ++j) {
                    wanted = wanted || shown[j].art == a;
                }
                if (!wanted) {
                    victim = a;
                    break;
                }
            }
            victim->valid = true;
            victim->hand = hand;
            victim->index = shown[i].index;
            victim->font = font_key;
            lay(text, *victim, shown[i].label->name, font);
            shown[i].art = victim;
        }
    }
    if (count == 0) {
        return;
    }

    const ChordLabelGeometry g =
        chord_label_geometry(in.strip, font, in.ui_unit, metrics_.cap_em, metrics_.cap_top_em);
    std::array<ChordSpan, kChordMaxBlockers> blocked{};
    const std::size_t blocked_n = chord_label_blockers(in.strip, in.avoid, g.pad, blocked);
    const std::span<const ChordSpan> blockers(blocked.data(), blocked_n);

    std::array<ChordNameWant, kChordMaxNames> wants{};
    std::array<ChordNamePlace, kChordMaxNames> places{};
    for (std::size_t i = 0; i < count; ++i) {
        wants[i] = {shown[i].art->width, 0.5f * (shown[i].x0 + shown[i].x1), shown[i].hand};
    }
    place_chord_names(std::span<const ChordNameWant>(wants.data(), count), in.strip.x + g.margin,
                      in.strip.right() - g.margin, g.pad, blockers,
                      std::span<ChordNamePlace>(places.data(), count));

    // The lines, with a tick standing up at each end, cut round whatever shares the height.
    mesh.clear();
    const float th = g.line_thickness;
    const auto add_runs = [&](float xa, float xb, float y, float h, SDL_FColor c) {
        std::array<ChordSpan, kChordMaxBlockers + 1> runs{};
        const std::size_t n = chord_line_runs(xa, xb, blockers, runs);
        for (std::size_t r = 0; r < n; ++r) {
            const float x0 = std::round(runs[r].x0);
            const float x1 = std::round(runs[r].x1);
            if (x1 > x0 && c.a > 0.0f) {
                mesh.rect({x0, y, x1 - x0, h}, c, c);
            }
        }
    };
    for (std::size_t i = 0; i < count; ++i) {
        const SDL_FColor c =
            faded(in.colours[static_cast<std::size_t>(shown[i].hand)], kLineAlpha * shown[i].alpha);
        add_runs(shown[i].x0, shown[i].x1, g.line_y, th, c);
        add_runs(shown[i].x0, shown[i].x0 + th, g.line_y - g.tick_height, g.tick_height + th, c);
        add_runs(shown[i].x1 - th, shown[i].x1, g.line_y - g.tick_height, g.tick_height + th, c);
    }
    mesh.draw(renderer, SDL_BLENDMODE_BLEND);

    draw_names(renderer, text, mesh, in, g, std::span<Shown>(shown.data(), count),
               std::span<const ChordNamePlace>(places.data(), count));
}

} // namespace etude::app
