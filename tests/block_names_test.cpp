// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/block_column.hpp"
#include "etude/block_names.hpp"
#include "etude/keyboard.hpp"
#include "etude/look.hpp"
#include "etude/pitch.hpp"
#include "etude/screen_layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

const RectF kClip{0.0f, 100.0f, 800.0f, 300.0f}; // bottom at 400

// The widths of the pieces in em, as the app's font measures them (Atkinson Hyperlegible Bold,
// the advance widths of its glyphs). Stated here so core needs no font.
float piece_em(const std::string& text) {
    struct Entry {
        const char* text;
        float em;
    };
    static const Entry kTable[] = {
        {"C", 0.673f},  {"D", 0.686f},  {"E", 0.580f},  {"F", 0.544f},
        {"G", 0.719f},  {"A", 0.690f},  {"B", 0.639f},  {"Do", 1.259f},
        {"Re", 1.220f}, {"Mi", 1.157f}, {"Fa", 1.097f}, {"Sol", 1.506f},
        {"La", 1.090f}, {"Si", 0.934f}, {"#", 0.783f},  {"b", 0.596f},
    };
    for (const Entry& e : kTable) {
        if (text == e.text) {
            return e.em;
        }
    }
    // An octave number: digits and a minus sign, each its own advance width.
    static const float kDigits[] = {0.652f, 0.448f, 0.586f, 0.605f, 0.626f,
                                    0.612f, 0.632f, 0.543f, 0.625f, 0.561f};
    float em = 0.0f;
    for (char c : text) {
        em += c == '-' ? 0.374f : kDigits[c - '0'];
    }
    return em;
}

// Sizes the width tables reach, as the app measures them.
constexpr std::size_t kTablePx = 64;

// The width of `text` at every size from 0 to kTablePx - 1, as the app's tables hold it: here
// its em width times the size (a real font hints a little at small sizes, the planner does not
// care). Kept for the whole run, so a span into it stays valid.
std::span<const float> table_for(const std::string& text) {
    static std::map<std::string, std::vector<float>> tables;
    auto it = tables.find(text);
    if (it == tables.end()) {
        std::vector<float> t(kTablePx);
        for (std::size_t px = 0; px < kTablePx; ++px) {
            t[px] = piece_em(text) * static_cast<float>(px);
        }
        it = tables.emplace(text, std::move(t)).first;
    }
    return it->second;
}

NameWidths widths_for(int note, const LookSettings& look) {
    const NameParts p = note_name_parts(note, look);
    return {table_for(p.letter), p.octave.empty() ? std::span<const float>{} : table_for(p.octave)};
}

BlockNameIn block_in(const RectF& block, int note, const LookSettings& look = LookSettings{}) {
    return BlockNameIn{block, note, is_black_key(note),
                       accidental_kind(note_name_parts(note, look).accidental),
                       widths_for(note, look)};
}

BlockNameParams params_for(float max_font = 17.0f, const RectF& clip = kClip) {
    BlockNameParams p;
    p.clip = clip;
    p.max_font_px = max_font;
    p.min_font_px = kMinNameFontPx;
    p.min_small_font_px = kMinSmallNameFontPx;
    return p;
}

std::vector<BlockNamePlacement> plan(const BlockNameParams& params,
                                     const std::vector<BlockNameIn>& blocks) {
    std::vector<BlockNamePlacement> out(blocks.size());
    plan_block_names(params, blocks, out);
    return out;
}

BlockNamePlacement plan_one(const BlockNameParams& params, const BlockNameIn& block) {
    return plan(params, {block})[0];
}

// A tall block `w` wide whose lower edge is on the clip's, for `note`.
// `in` with the key's lane set.
BlockNameIn in_lane(BlockNameIn in, float lane_x, float lane_w) {
    in.lane_x = lane_x;
    in.lane_w = lane_w;
    return in;
}

RectF tall(float w, float cx = 300.0f) { return RectF{cx - 0.5f * w, 100.0f, w, 300.0f}; }

bool inside(const RectF& inner, const RectF& outer) {
    return inner.x >= outer.x - 0.001f && inner.y >= outer.y - 0.001f &&
           inner.right() <= outer.right() + 0.001f && inner.bottom() <= outer.bottom() + 0.001f;
}

// What the box is made of must add up, whatever the form.
void check_box_adds_up(const BlockNamePlacement& p) {
    REQUIRE(p.form != NameForm::Hidden);
    CHECK(p.rect.w == doctest::Approx(p.letter_w + p.glyph_w + p.octave_w + 2.0f * p.outline_px));
    CHECK(p.outline_px == name_outline_px(p.font_px));
    CHECK(p.font_px == std::floor(p.font_px));
    if (p.accidental == AccidentalKind::None) {
        CHECK(p.glyph_w == 0.0f);
        CHECK(p.glyph_h == 0.0f);
    } else {
        CHECK(p.glyph_h >= kMinAccidentalGlyphPx);
        CHECK(p.glyph_w == accidental_glyph_width_px(p.accidental, p.glyph_h));
        // The glyph hangs from the letter's capitals and stays inside the box.
        CHECK(p.glyph_dy + p.glyph_h <= p.rect.h - 2.0f * p.outline_px);
    }
}

// The width of the glyph of a sharp beside a letter at `font` px.
float sharp_w(float font) {
    return accidental_glyph_width_px(AccidentalKind::Sharp,
                                     accidental_glyph_height_px(BlockNameParams{}.cap_em * font));
}

} // namespace

TEST_CASE("the outline is at least a pixel and grows with the text") {
    CHECK(name_outline_px(9.0f) == 1.0f);
    CHECK(name_outline_px(1.0f) == 1.0f);
    CHECK(name_outline_px(30.0f) == 2.0f);
}

TEST_CASE("a name that fits is centred on its block, above its bottom edge, inside it") {
    const LookSettings look;
    const RectF block = tall(40.0f);
    const BlockNamePlacement p = plan_one(params_for(), block_in(block, 60, look)); // C4
    check_box_adds_up(p);
    CHECK(p.form == NameForm::Short); // octave numbers are off: the name has none to lose
    CHECK(p.font_px == 17.0f);
    CHECK(inside(p.rect, block));
    CHECK(p.rect.x + 0.5f * p.rect.w == doctest::Approx(300.0f).epsilon(0.02));
    CHECK(p.rect.bottom() == doctest::Approx(400.0f - kNamePadYPx));
    CHECK(p.accidental == AccidentalKind::None);
}

TEST_CASE("a black key's name is the letter and a drawn accidental hung from its capitals") {
    const LookSettings look;
    const BlockNamePlacement p = plan_one(params_for(), block_in(tall(40.0f), 61, look)); // C#4
    check_box_adds_up(p);
    CHECK(p.accidental == AccidentalKind::Sharp);
    // At 17 px the capitals are 11.4 px tall: a glyph 9 px tall, 7 wide, its top at the capitals'.
    CHECK(p.glyph_h == 9.0f);
    CHECK(p.glyph_w == 7.0f);
    CHECK(p.glyph_dy == std::round(0.282f * 17.0f));
    CHECK(p.letter_w == std::ceil(0.673f * 17.0f));
    // A flat gets its own, narrower glyph.
    const BlockNamePlacement f = plan_one(params_for(), block_in(tall(40.0f), 63, [] {
                                              LookSettings l;
                                              l.names_spelling = NameSpelling::Flats;
                                              return l;
                                          }()));
    CHECK(f.accidental == AccidentalKind::Flat);
    CHECK(f.glyph_w == accidental_glyph_width_px(AccidentalKind::Flat, f.glyph_h));
    CHECK(f.glyph_w < p.glyph_w);
}

TEST_CASE("with octave numbers the whole name goes first, the name without them next") {
    LookSettings look;
    look.names_show_octave = true;
    // "C#4" needs more room than "C#" at every size: a block between the two widths.
    const NameWidths w = widths_for(61, look);
    const auto need = [&](float font, bool octave) {
        const auto px = static_cast<std::size_t>(font);
        return std::ceil(w.letter[px]) + sharp_w(font) + (octave ? std::ceil(w.octave[px]) : 0.0f) +
               2.0f * name_outline_px(font) + 2.0f * kNamePadXPx;
    };
    BlockNameParams params = params_for(9.0f); // one size only, to isolate the form
    const BlockNameIn wide = block_in(tall(need(9.0f, true) + 0.5f), 61, look);
    CHECK(plan_one(params, wide).form == NameForm::Full);
    const BlockNameIn mid = block_in(tall(need(9.0f, true) - 0.5f), 61, look);
    const BlockNamePlacement m = plan_one(params, mid);
    CHECK(m.form == NameForm::Short);
    CHECK(m.octave_w == 0.0f);
    // The octave number goes before the name shrinks: Full at 9 px beats Short at 12 px.
    params = params_for(12.0f);
    const BlockNamePlacement f = plan_one(params, wide);
    CHECK(f.form == NameForm::Full);
    CHECK(f.font_px == 9.0f);
}

TEST_CASE("the fit is exact: just wide enough names, one pixel less hides a white key's name") {
    const LookSettings look;
    const float font = 9.0f;
    const float need = std::ceil(0.673f * font) + 2.0f * name_outline_px(font);
    BlockNameParams params = params_for(font);
    CHECK(plan_one(params, block_in(tall(need), 60, look)).form != NameForm::Hidden); // exactly
    CHECK(plan_one(params, block_in(tall(need - 0.01f), 60, look)).form == NameForm::Hidden);
    // And vertically: the box, its outline and the padding above and below.
    const float high = std::ceil(1.24f * font) + 2.0f * name_outline_px(font) + 2.0f * kNamePadYPx;
    const auto at_height = [&](float h) {
        return plan_one(params, block_in(RectF{280.0f, 400.0f - h, 40.0f, h}, 60, look));
    };
    CHECK(at_height(high).form != NameForm::Hidden);
    CHECK(at_height(high - 0.01f).form == NameForm::Hidden);
}

TEST_CASE("a name keeps a pixel clear of the block's sides when any size allows, else fits exactly") {
    const LookSettings look;
    const float outline = name_outline_px(9.0f);
    const float box = std::ceil(0.673f * 9.0f) + 2.0f * outline;
    BlockNameParams params = params_for(9.0f); // one size only
    // Room for the padding: the box sits with kNamePadXPx clear on each side.
    const BlockNameIn roomy = block_in(tall(box + 2.0f * kNamePadXPx), 60, look);
    const BlockNamePlacement p = plan_one(params, roomy);
    REQUIRE(p.form != NameForm::Hidden);
    CHECK(p.rect.x >= roomy.block.x + kNamePadXPx);
    CHECK(p.rect.right() <= roomy.block.right() - kNamePadXPx);
    // A hair less and the padding goes, not the name: the box still lies inside the block.
    const BlockNameIn tight = block_in(tall(box + 2.0f * kNamePadXPx - 0.5f), 60, look);
    const BlockNamePlacement t = plan_one(params, tight);
    REQUIRE(t.form != NameForm::Hidden);
    CHECK(t.rect.x >= tight.block.x);
    CHECK(t.rect.right() <= tight.block.right());
    // The padding is bought with size before it is given up: where a smaller size gives it, that
    // size is drawn rather than the larger one without it.
    params = params_for(12.0f);
    const float box12 = std::ceil(0.673f * 12.0f) + 2.0f * name_outline_px(12.0f);
    const BlockNamePlacement s = plan_one(params, block_in(tall(box12 + 0.5f), 60, look));
    REQUIRE(s.form != NameForm::Hidden);
    CHECK(s.font_px < 12.0f);
    CHECK(s.rect.x >= 280.0f + kNamePadXPx);
}

TEST_CASE("sizes are whole pixels: the minimum rounds up, the maximum rounds down") {
    const LookSettings look;
    const BlockNameIn big = block_in(tall(60.0f), 60, look);
    CHECK(plan_one(params_for(16.9f), big).font_px == 16.0f);
    // A maximum under 9 leaves nothing legible for a white key.
    CHECK(plan_one(params_for(8.9f), big).form == NameForm::Hidden);
    // A minimum of 8.5 means 9: a block that holds the letter at 8 px but not at 9 stays empty.
    const float need8 = std::ceil(0.673f * 8.0f) + 2.0f * name_outline_px(8.0f);
    const float need9 = std::ceil(0.673f * 9.0f) + 2.0f * name_outline_px(9.0f);
    REQUIRE(need8 < need9);
    BlockNameParams params = params_for(9.0f);
    params.min_font_px = 8.5f;
    CHECK(plan_one(params, block_in(tall(need9 - 0.01f), 60, look)).form == NameForm::Hidden);
    CHECK(plan_one(params, block_in(tall(need9), 60, look)).font_px == 9.0f);
    // Whatever is planned is drawn at exactly that size: no placement has a fractional size.
    for (float max_font : {9.0f, 11.3f, 14.5f, 17.0f, 17.99f}) {
        const BlockNamePlacement p = plan_one(params_for(max_font), big);
        CHECK(p.font_px == std::floor(p.font_px));
        CHECK(p.font_px == std::floor(max_font));
    }
}

TEST_CASE("only a name with an accidental may go below the minimum legible size") {
    const LookSettings look;
    // "G#": at 9 px it needs a bit more than at 8 or 7 px (the glyph is the same size, the
    // letter a pixel wider).
    const float need8 = std::ceil(0.719f * 8.0f) + sharp_w(8.0f) + 2.0f * name_outline_px(8.0f);
    const float need9 = std::ceil(0.719f * 9.0f) + sharp_w(9.0f) + 2.0f * name_outline_px(9.0f);
    REQUIRE(need8 < need9);
    const BlockNamePlacement black = plan_one(params_for(), block_in(tall(need8), 68, look)); // G#
    REQUIRE(black.form != NameForm::Hidden);
    CHECK(black.font_px < 9.0f);
    CHECK(black.font_px >= kMinSmallNameFontPx);
    CHECK(black.accidental == AccidentalKind::Sharp);
    // A white key's G needs 9 + 2 px at the legible minimum: 8.5 px would hold it at 7 px, but
    // that is too small for a letter alone.
    const float white9 = std::ceil(0.719f * 9.0f) + 2.0f * name_outline_px(9.0f);
    REQUIRE(white9 > 8.5f);
    CHECK(plan_one(params_for(), block_in(tall(8.5f), 67, look)).form == NameForm::Hidden);
    CHECK(plan_one(params_for(), block_in(tall(white9), 67, look)).form != NameForm::Hidden);
    // Never below the small minimum, even for a black key with room to spare in height only.
    const float need6 = std::ceil(0.719f * 6.0f) + sharp_w(6.0f) + 2.0f * name_outline_px(6.0f);
    CHECK(plan_one(params_for(), block_in(tall(need6), 68, look)).form == NameForm::Hidden);
}

TEST_CASE("a block with no visible part, no size or a bad number is never named") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const LookSettings look;
    const BlockNameParams params = params_for();
    CHECK(plan_one(params, block_in(RectF{280.0f, 100.0f, 0.0f, 300.0f}, 60, look)).form ==
          NameForm::Hidden); // zero width
    CHECK(plan_one(params, block_in(RectF{280.0f, 100.0f, 40.0f, 0.0f}, 60, look)).form ==
          NameForm::Hidden); // zero height
    CHECK(plan_one(params, block_in(RectF{280.0f, 100.0f, -40.0f, 300.0f}, 60, look)).form ==
          NameForm::Hidden);
    CHECK(plan_one(params, block_in(RectF{280.0f, 100.0f, 40.0f, -300.0f}, 60, look)).form ==
          NameForm::Hidden);
    CHECK(plan_one(params, block_in(RectF{nan, 100.0f, 40.0f, 300.0f}, 60, look)).form ==
          NameForm::Hidden);
    CHECK(plan_one(params, block_in(RectF{280.0f, 500.0f, 40.0f, 300.0f}, 60, look)).form ==
          NameForm::Hidden); // below the clip
    CHECK(plan_one(params, block_in(RectF{280.0f, 0.0f, 40.0f, 90.0f}, 60, look)).form ==
          NameForm::Hidden); // above it
    BlockNameIn no_letter = block_in(tall(40.0f), 60, look);
    no_letter.width.letter = {};
    CHECK(plan_one(params, no_letter).form == NameForm::Hidden);
    const std::vector<float> nans(kTablePx, nan);
    no_letter.width.letter = nans;
    CHECK(plan_one(params, no_letter).form == NameForm::Hidden);
    const std::vector<float> zeros(kTablePx, 0.0f);
    no_letter.width.letter = zeros;
    CHECK(plan_one(params, no_letter).form == NameForm::Hidden);
    // A table that stops short of the maximum: the sizes it holds are used, none past it.
    const std::vector<float> short_table(table_for("C").begin(), table_for("C").begin() + 12);
    BlockNameIn short_c = block_in(tall(40.0f), 60, look);
    short_c.width.letter = short_table;
    CHECK(plan_one(params, short_c).font_px == 11.0f);
    // Bad parameters name nothing and do not crash.
    BlockNameParams bad = params;
    bad.max_font_px = nan;
    CHECK(plan_one(bad, block_in(tall(40.0f), 60, look)).form == NameForm::Hidden);
    bad = params;
    bad.clip.w = nan;
    CHECK(plan_one(bad, block_in(tall(40.0f), 60, look)).form == NameForm::Hidden);
    bad = params;
    bad.clip = RectF{};
    CHECK(plan_one(bad, block_in(tall(40.0f), 60, look)).form == NameForm::Hidden);
    bad = params;
    bad.line_em = 0.0f;
    CHECK(plan_one(bad, block_in(tall(40.0f), 60, look)).form == NameForm::Hidden);
    bad = params;
    bad.max_names = 0;
    CHECK(plan_one(bad, block_in(tall(40.0f), 60, look)).form == NameForm::Hidden);
    CHECK(plan_block_names(params, {}, {}) == 0);
}

TEST_CASE("only the part of a block inside the clip counts") {
    const LookSettings look;
    // 200 px of block but only 12 px of it above the clip's bottom edge: too short for any name.
    CHECK(plan_one(params_for(), block_in(RectF{280.0f, 388.0f, 40.0f, 200.0f}, 60, look)).form ==
          NameForm::Hidden);
    const BlockNameIn long_block = block_in(RectF{280.0f, 200.0f, 40.0f, 400.0f}, 60, look);
    const BlockNamePlacement p = plan_one(params_for(), long_block);
    REQUIRE(p.form != NameForm::Hidden);
    CHECK(p.rect.bottom() == doctest::Approx(400.0f - kNamePadYPx)); // the clip's bottom
}

TEST_CASE("a white key's name and the black key's name over its block both keep their room") {
    const LookSettings look;
    // C4 over a 40 px block, and a C#4 block 13 px wide over its right part, as with KeyWidth: the
    // white key's name keeps clear of the black key's block, so the black key's name, inside its
    // own block, never meets it, and neither has to shrink.
    for (float black_cx : {312.0f, 306.0f}) {
        const BlockNameIn white = block_in(tall(40.0f, 300.0f), 60, look);
        const BlockNameIn black = block_in(tall(13.0f, black_cx), 61, look);
        for (bool black_first : {false, true}) {
            const std::vector<BlockNamePlacement> out = black_first
                                                            ? plan(params_for(), {black, white})
                                                            : plan(params_for(), {white, black});
            const BlockNamePlacement& w = out[black_first ? 1 : 0];
            const BlockNamePlacement& b = out[black_first ? 0 : 1];
            REQUIRE(w.form != NameForm::Hidden);
            REQUIRE(b.form != NameForm::Hidden);
            CHECK(w.font_px == 17.0f);
            CHECK(b.accidental == AccidentalKind::Sharp);
            CHECK_FALSE(rects_overlap(w.rect, b.rect));
            CHECK_FALSE(rects_overlap(w.rect, black.block));
            check_box_adds_up(w);
            check_box_adds_up(b);
        }
    }
    // Two black keys' names never overlap.
    const BlockNameIn b1 = block_in(tall(13.0f, 312.0f), 61, look);
    const BlockNameIn b2 = block_in(tall(13.0f, 322.0f), 63, look);
    const std::vector<BlockNamePlacement> blacks = plan(params_for(), {b1, b2});
    CHECK(blacks[0].form != NameForm::Hidden);
    CHECK_FALSE(rects_overlap(blacks[0].rect, blacks[1].rect));
}

TEST_CASE(
    "names that would overlap: the white key's settles first, the other takes its next option") {
    const LookSettings look;
    // A white block and a black block over its right side, names too wide to sit apart.
    const RectF white{100.0f, 100.0f, 40.0f, 300.0f};
    const RectF black{125.0f, 100.0f, 30.0f, 300.0f};
    const BlockNameIn w = block_in(white, 62, look);
    const BlockNameIn b = block_in(black, 61, look);
    const std::vector<BlockNamePlacement> out = plan(params_for(), {b, w}); // listed black first
    REQUIRE(out[1].form != NameForm::Hidden);
    CHECK(out[1].font_px == 17.0f); // the white key keeps its full size
    if (out[0].form != NameForm::Hidden) {
        CHECK_FALSE(rects_overlap(out[0].rect, out[1].rect));
    }
}

TEST_CASE(
    "Auto leaves a black key's name out rather than overlap, Always draws it at the minimum") {
    const LookSettings look;
    // A black block 3 px wide, tall: nothing fits inside it.
    const BlockNameIn thin = block_in(tall(3.0f), 61, look);
    BlockNameParams params = params_for();
    CHECK(plan_one(params, thin).form == NameForm::Hidden);
    params.black_names = BlackKeyNames::Off;
    CHECK(plan_one(params, block_in(tall(40.0f), 61, look)).form == NameForm::Hidden);
    params.black_names = BlackKeyNames::Always;
    // Its lane is wider than the block: the name spills into it.
    const BlockNameIn laned = in_lane(thin, 280.0f, 40.0f);
    const BlockNamePlacement p = plan_one(params, laned);
    REQUIRE(p.form == NameForm::Short);
    CHECK(p.font_px == kMinSmallNameFontPx);
    CHECK(p.rect.w > 3.0f); // spills past the block's sides
    CHECK(p.rect.x + 0.5f * p.rect.w == doctest::Approx(300.0f).epsilon(0.01));
    CHECK(p.rect.bottom() == doctest::Approx(400.0f - kNamePadYPx));
    // Still has to fit the block's height.
    const BlockNameIn flat = in_lane(block_in(RectF{298.5f, 390.0f, 3.0f, 10.0f}, 61, look),
                                     280.0f, 40.0f);
    CHECK(plan_one(params, flat).form == NameForm::Hidden);
    // A white key is not a black key: Always and Off do not touch it.
    params.black_names = BlackKeyNames::Off;
    CHECK(plan_one(params, block_in(tall(40.0f), 60, look)).form != NameForm::Hidden);
    params.black_names = BlackKeyNames::Always;
    CHECK(plan_one(params, block_in(tall(3.0f), 60, look)).form == NameForm::Hidden);
    // Always only spills when nothing fits: a roomy block gets the same name as in Auto.
    const BlockNamePlacement roomy = plan_one(params, block_in(tall(40.0f), 61, look));
    CHECK(roomy.font_px == 17.0f);
}

TEST_CASE("Always never spills over another name") {
    const LookSettings look;
    const RectF white{100.0f, 100.0f, 40.0f, 300.0f};
    const BlockNameIn w = block_in(white, 62, look);
    // A thin black block right over the white key's name, in a wide lane.
    const BlockNameIn b = in_lane(block_in(RectF{118.0f, 100.0f, 4.0f, 300.0f}, 61, look), 100.0f,
                                  40.0f);
    BlockNameParams params = params_for();
    params.black_names = BlackKeyNames::Always;
    const std::vector<BlockNamePlacement> always = plan(params, {w, b});
    REQUIRE(always[0].form != NameForm::Hidden);
    if (always[1].form != NameForm::Hidden) {
        CHECK_FALSE(rects_overlap(always[0].rect, always[1].rect));
    }
    // Alone, it spills into its lane.
    CHECK(plan_one(params, b).form == NameForm::Short);
}

TEST_CASE("Always keeps a spilled name inside the key's lane") {
    const LookSettings look;
    BlockNameParams params = params_for();
    params.black_names = BlackKeyNames::Always;
    const BlockNameIn thin = block_in(tall(3.0f), 61, look);
    // No lane given: the lane is the block, and nothing fits in it.
    CHECK(plan_one(params, thin).form == NameForm::Hidden);
    // A lane just wide enough: the name fills it, never past its sides.
    const BlockNamePlacement wide = plan_one(params, in_lane(thin, 280.0f, 40.0f));
    REQUIRE(wide.form == NameForm::Short);
    const float need = wide.rect.w;
    for (const float lane_w : {need + 0.5f, need + 1.0f, need + 3.0f}) {
        CAPTURE(lane_w);
        // A lane off to one side of the block: the name moves into it, never out of it.
        const float lane_x = 300.0f - 0.5f * 3.0f - 1.0f;
        const BlockNamePlacement p = plan_one(params, in_lane(thin, lane_x, lane_w));
        REQUIRE(p.form == NameForm::Short);
        CHECK(p.rect.x >= lane_x);
        CHECK(p.rect.right() <= lane_x + lane_w);
        CHECK(p.rect.x == std::floor(p.rect.x)); // still on a whole pixel
    }
    // A lane one pixel narrower than the name: hidden.
    CHECK(plan_one(params, in_lane(thin, 280.0f, need - 1.0f)).form == NameForm::Hidden);
    // A bad lane counts as none.
    CHECK(plan_one(params, in_lane(thin, std::nanf(""), 40.0f)).form == NameForm::Hidden);
}

TEST_CASE("the cap keeps the names nearest the keys, then the lower note, whatever the order") {
    const LookSettings look;
    // 60 white-key blocks side by side, each lower edge a little nearer the keys than the last.
    constexpr int kWhites[7] = {60, 62, 64, 65, 67, 69, 71};
    std::vector<BlockNameIn> blocks;
    for (int i = 0; i < 60; ++i) {
        const float h = 18.0f;
        const float bottom = 400.0f - 4.0f * static_cast<float>(59 - i); // the last is nearest
        blocks.push_back(
            block_in(RectF{10.0f + 13.0f * static_cast<float>(i), bottom - h, 12.0f, h},
                     kWhites[i % 7], look));
    }
    BlockNameParams params = params_for();
    params.clip = RectF{0.0f, 0.0f, 800.0f, 400.0f};
    params.max_names = 48;
    const std::vector<BlockNamePlacement> out = plan(params, blocks);
    int named = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].form != NameForm::Hidden) {
            ++named;
            CHECK(i >= 12); // the 12 farthest (the first listed) lost their names
        }
    }
    CHECK(named == 48);
    // The same blocks in the opposite order name the same ones.
    const std::vector<BlockNameIn> reversed(blocks.rbegin(), blocks.rend());
    const std::vector<BlockNamePlacement> back = plan(params, reversed);
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK((out[i].form != NameForm::Hidden) ==
              (back[out.size() - 1 - i].form != NameForm::Hidden));
    }
    // A cap of the whole list names them all.
    params.max_names = 60;
    int all = 0;
    for (const BlockNamePlacement& p : plan(params, blocks)) {
        all += p.form != NameForm::Hidden ? 1 : 0;
    }
    CHECK(all == 60);
}

TEST_CASE("blocks at the same distance: the lower note keeps its name") {
    const LookSettings look;
    BlockNameParams params = params_for();
    params.max_names = 1;
    const BlockNameIn high = block_in(tall(40.0f, 200.0f), 72, look);
    const BlockNameIn low = block_in(tall(40.0f, 400.0f), 60, look);
    for (const std::vector<BlockNameIn>& order :
         {std::vector<BlockNameIn>{high, low}, std::vector<BlockNameIn>{low, high}}) {
        const std::vector<BlockNamePlacement> out = plan(params, order);
        for (std::size_t i = 0; i < 2; ++i) {
            CHECK((out[i].form != NameForm::Hidden) == (order[i].note == 60));
        }
    }
}

TEST_CASE("more candidates than the limit: the nearest are ranked, wherever they are listed") {
    const LookSettings look;
    // The last 40 listed are the nearest the keys: the old cap by list position lost them.
    std::vector<BlockNameIn> blocks;
    const std::size_t total = kMaxNameCandidates + 40;
    for (std::size_t i = 0; i < total; ++i) {
        const float above = 3.0f * static_cast<float>(total - 1 - i); // the last is nearest
        blocks.push_back(block_in(
            RectF{10.0f + 3.0f * static_cast<float>(i % 250), 400.0f - above - 60.0f, 20.0f, 60.0f},
            60, look));
    }
    BlockNameParams params = params_for();
    params.clip = RectF{0.0f, -1000.0f, 800.0f, 1400.0f};
    params.max_names = 1;
    std::vector<BlockNamePlacement> out = plan(params, blocks);
    for (std::size_t i = 0; i < total; ++i) {
        CHECK((out[i].form != NameForm::Hidden) == (i == total - 1));
    }
    params.max_names = 48;
    out = plan(params, blocks);
    int named = 0;
    for (std::size_t i = 0; i < total; ++i) {
        if (out[i].form != NameForm::Hidden) {
            ++named;
            CHECK(i >= total - 48);
        }
    }
    CHECK(named > 0);
}

TEST_CASE("a caller's buffer keeps the blocks nearest the keys") {
    const LookSettings look;
    const RectF clip = kClip;
    std::vector<BlockNameIn> held;
    const std::size_t capacity = 4;
    // Ten blocks, farthest first; then one as far as the farthest held.
    for (int i = 0; i < 10; ++i) {
        const float above = 10.0f * static_cast<float>(10 - i);
        const BlockNameIn b = block_in(RectF{100.0f, 300.0f - above, 20.0f, 100.0f}, 60 + i, look);
        const std::size_t slot = name_candidate_slot(held, capacity, b, clip);
        if (slot == held.size()) {
            held.push_back(b);
        } else if (slot < capacity) {
            held[slot] = b;
        }
    }
    REQUIRE(held.size() == capacity);
    for (const BlockNameIn& b : held) {
        CHECK(b.note >= 66); // the four nearest
    }
    // Farther than all held: dropped. Same distance as the farthest held but a higher note:
    // dropped.
    const BlockNameIn far = block_in(RectF{100.0f, 0.0f, 20.0f, 100.0f}, 60, look);
    CHECK(name_candidate_slot(held, capacity, far, clip) == capacity);
    const BlockNameIn tie = block_in(RectF{100.0f, 260.0f, 20.0f, 100.0f}, 100, look);
    CHECK(name_candidate_slot(held, capacity, tie, clip) == capacity);
}

TEST_CASE("a white key's name keeps clear of the black keys' blocks drawn over its block") {
    const LookSettings look;
    // Key width: C's block 40 px wide, C#'s over its right part (from 24 px on), D# far away.
    const BlockNameIn c = block_in(RectF{100.0f, 100.0f, 40.0f, 300.0f}, 60, look);
    const BlockNameIn cs = block_in(RectF{124.0f, 100.0f, 24.0f, 300.0f}, 61, look);
    std::vector<BlockNamePlacement> out = plan(params_for(), {c, cs});
    REQUIRE(out[0].form != NameForm::Hidden);
    CHECK(out[0].rect.right() <= 124.0f);
    CHECK(out[0].rect.x >= 100.0f);
    // Centred on what is left of it, not on the whole block.
    CHECK(out[0].rect.x + 0.5f * out[0].rect.w == doctest::Approx(112.0f).epsilon(0.03));
    // Black names off: the C# block is still drawn, so the C name still keeps clear of it.
    BlockNameParams off = params_for();
    off.black_names = BlackKeyNames::Off;
    out = plan(off, {c, cs});
    CHECK(out[1].form == NameForm::Hidden);
    REQUIRE(out[0].form != NameForm::Hidden);
    CHECK(out[0].rect.right() <= 124.0f);
    // A black block only over the top of the white block, above the name, does not matter.
    const BlockNameIn high = block_in(RectF{124.0f, 100.0f, 24.0f, 100.0f}, 61, look);
    out = plan(params_for(), {c, high});
    CHECK(out[0].rect.x + 0.5f * out[0].rect.w == doctest::Approx(120.0f).epsilon(0.03));
    // D between C# and D#: the gap between them.
    const BlockNameIn d = block_in(RectF{140.0f, 100.0f, 40.0f, 300.0f}, 62, look);
    const BlockNameIn ds = block_in(RectF{164.0f, 100.0f, 24.0f, 300.0f}, 63, look);
    out = plan(params_for(), {d, cs, ds});
    REQUIRE(out[0].form != NameForm::Hidden);
    CHECK(out[0].rect.x >= 148.0f);
    CHECK(out[0].rect.right() <= 164.0f);
    // Covered all over: no name.
    const BlockNameIn wide_black = block_in(RectF{95.0f, 100.0f, 50.0f, 300.0f}, 61, look);
    CHECK(plan(params_for(), {c, wide_black})[0].form == NameForm::Hidden);
}

TEST_CASE("the biggest size tried is no taller than the clip") {
    const LookSettings look;
    BlockNameParams params = params_for(1e30f, RectF{0.0f, 380.0f, 800.0f, 20.0f});
    const BlockNamePlacement p = plan_one(params, block_in(tall(60.0f), 60, look));
    REQUIRE(p.form != NameForm::Hidden);
    CHECK(p.font_px <= 20.0f);
    CHECK(inside(p.rect, params.clip));
}

// ---- whole screens ---------------------------------------------------------------------------

namespace {

struct Screen {
    float w;
    float h;
};

const Screen kScreens[] = {{800.0f, 480.0f}, {1366.0f, 768.0f}, {1920.0f, 1080.0f}};

struct Frame {
    ScreenLayout layout;
    BlockNameParams params;
    std::vector<BlockNameIn> inputs;
    std::vector<BlockNamePlacement> out;
};

// A block for `note` on a keyboard of `range` across `screen`, as the renderer builds it: its
// block column in the look's layout. `above` is how far its bottom edge is above the hit line.
BlockNameIn key_block(Screen screen, const ScreenLayout& layout, const KeyboardLayout& keys,
                      int note, float height, float above, const LookSettings& look) {
    KeyViewFrame view;
    view.layout_range = keys.range();
    view.span_whites = static_cast<float>(keys.white_key_count());
    const BlockColumn c = block_column(keys, view, note, screen.w);
    const RectF rect{c.x, layout.note_area.bottom() - above - height, c.w, height};
    return in_lane(block_in(rect, note, look), c.lane_x, c.lane_w);
}

Frame plan_frame(Screen screen, const LookSettings& look, KeyRange range,
                 const std::vector<std::pair<int, std::pair<float, float>>>& notes,
                 BlackKeyNames mode = BlackKeyNames::Auto) {
    Frame f;
    f.layout = compute_layout(screen.w, screen.h, kDefaultPxPerMm, look);
    f.params.clip = f.layout.note_area;
    f.params.max_font_px = f.layout.note_label_font_px;
    f.params.min_font_px = min_name_font_px(kDefaultPxPerMm);
    f.params.min_small_font_px = min_small_name_font_px(kDefaultPxPerMm);
    f.params.black_names = mode;
    const KeyboardLayout keys(range, look.block_layout);
    for (const auto& n : notes) {
        f.inputs.push_back(
            key_block(screen, f.layout, keys, n.first, n.second.first, n.second.second, look));
    }
    f.out.resize(f.inputs.size());
    plan_block_names(f.params, f.inputs, f.out);
    return f;
}

void check_legal(const Frame& f) {
    for (std::size_t i = 0; i < f.out.size(); ++i) {
        const BlockNamePlacement& a = f.out[i];
        if (a.form == NameForm::Hidden) {
            continue;
        }
        INFO("note " << f.inputs[i].note);
        check_box_adds_up(a);
        CHECK(a.font_px <= f.params.max_font_px);
        CHECK(a.font_px >= std::ceil(f.params.min_small_font_px));
        if (f.inputs[i].accidental == AccidentalKind::None) {
            CHECK(a.font_px >= f.params.min_font_px);
        }
        CHECK(inside(a.rect, f.inputs[i].block));
        CHECK(inside(a.rect, f.layout.note_area));
        // Black keys' blocks are drawn over white keys' blocks: none covers a white key's name.
        if (!f.inputs[i].black) {
            for (const BlockNameIn& b : f.inputs) {
                if (b.black) {
                    CHECK_FALSE(rects_overlap(a.rect, b.block));
                }
            }
        }
        for (std::size_t j = i + 1; j < f.out.size(); ++j) {
            if (f.out[j].form != NameForm::Hidden) {
                CHECK_FALSE(rects_overlap(a.rect, f.out[j].rect));
            }
        }
    }
}

// The default look in each block layout.
std::vector<LookSettings> both_layouts(LookSettings look = {}) {
    std::vector<LookSettings> out;
    for (BlockLayout mode : {BlockLayout::KeyWidth, BlockLayout::EvenLanes}) {
        look.block_layout = mode;
        out.push_back(look);
    }
    return out;
}

} // namespace

TEST_CASE("fit target: a chromatic run from A0 to C8 names every black key with its accidental") {
    // Default settings at the sizes of a laptop's full screen, in both block layouts, with and
    // without octave numbers. The run is played in windows of consecutive notes, one block high
    // each, so no two names compete for room.
    LookSettings octaves;
    octaves.names_show_octave = true;
    std::vector<LookSettings> looks = both_layouts();
    for (const LookSettings& l : both_layouts(octaves)) {
        looks.push_back(l);
    }
    for (const LookSettings& look : looks)
        for (Screen screen : {kScreens[1], kScreens[2]}) {
            INFO("layout " << static_cast<int>(look.block_layout) << " octave "
                           << look.names_show_octave);
            const float step = 24.0f;
            int black_seen = 0;
            for (int first = 21; first <= 108; first += 16) {
                std::vector<std::pair<int, std::pair<float, float>>> notes;
                for (int note = first; note < std::min(first + 16, 109); ++note) {
                    notes.push_back({note, {step, step * static_cast<float>(note - first)}});
                }
                const Frame f = plan_frame(screen, look, kRange88, notes);
                check_legal(f);
                for (std::size_t i = 0; i < f.out.size(); ++i) {
                    INFO("screen " << screen.w << " note " << f.inputs[i].note);
                    CHECK(f.out[i].form != NameForm::Hidden);
                    if (is_black_key(f.inputs[i].note)) {
                        ++black_seen;
                        CHECK(f.out[i].accidental != AccidentalKind::None);
                        CHECK(f.out[i].glyph_w > 0.0f);
                        CHECK(f.out[i].glyph_h >= kMinAccidentalGlyphPx);
                    }
                }
            }
            CHECK(black_seen == 36); // A0 to C8 has 36 black keys
        }
}

TEST_CASE("a cluster C to E struck together names all five keys in both layouts") {
    for (const LookSettings& look : both_layouts()) {
        for (Screen screen : {kScreens[1], kScreens[2]}) {
            INFO("layout " << static_cast<int>(look.block_layout) << " screen " << screen.w);
            std::vector<std::pair<int, std::pair<float, float>>> notes;
            for (int note = 60; note <= 64; ++note) {
                notes.push_back({note, {80.0f, 0.0f}});
            }
            const Frame f = plan_frame(screen, look, kRange88, notes);
            check_legal(f);
            for (std::size_t i = 0; i < f.out.size(); ++i) {
                INFO("note " << f.inputs[i].note);
                CHECK(f.out[i].form != NameForm::Hidden);
            }
        }
    }
}

TEST_CASE(
    "at 800x480 names may hide but are legal, and black keys' names are never a bare letter") {
    for (const LookSettings& look : both_layouts()) {
        const Frame f = [&] {
            std::vector<std::pair<int, std::pair<float, float>>> notes;
            for (int note = 36; note <= 96; ++note) {
                notes.push_back({note, {20.0f, 20.0f * static_cast<float>(note % 12)}});
            }
            return plan_frame(kScreens[0], look, kRange61, notes);
        }();
        check_legal(f);
        for (std::size_t i = 0; i < f.out.size(); ++i) {
            if (f.out[i].form != NameForm::Hidden && is_black_key(f.inputs[i].note)) {
                CHECK(f.out[i].accidental != AccidentalKind::None);
            }
        }
    }
}

TEST_CASE("a dense chord is legal at every size and hides a name only when it must") {
    LookSettings solfege;
    solfege.name_mode = NameMode::Solfege;
    solfege.names_show_octave = true;
    LookSettings octaves;
    octaves.names_show_octave = true;
    std::vector<LookSettings> looks;
    for (const LookSettings& base : {LookSettings{}, solfege, octaves}) {
        for (const LookSettings& l : both_layouts(base)) {
            looks.push_back(l);
        }
    }
    for (const LookSettings& look : looks) {
        for (Screen screen : kScreens) {
            for (float height : {40.0f, 160.0f}) {
                std::vector<std::pair<int, std::pair<float, float>>> notes;
                for (int note : {60, 61, 62, 63, 64, 65, 66, 67}) {
                    notes.push_back({note, {height, 0.0f}});
                }
                const Frame f = plan_frame(screen, look, kRange61, notes);
                check_legal(f);
                // A white key's name is settled first: it is hidden only by its own block.
                for (std::size_t i = 0; i < f.out.size(); ++i) {
                    if (!is_black_key(f.inputs[i].note) && screen.w >= 1366.0f &&
                        look.name_mode == NameMode::Letters) {
                        CHECK(f.out[i].form != NameForm::Hidden);
                    }
                }
            }
        }
    }
}

TEST_CASE("names on black keys: Off names none, Always names each one its lane holds") {
    for (const LookSettings& look : both_layouts()) {
        for (Screen screen : kScreens) {
            CAPTURE(screen.w);
            // Black keys alone: Always names every one, inside its lane (800x480 may hide: its
            // lanes are narrower than the smallest name).
            std::vector<std::pair<int, std::pair<float, float>>> blacks;
            for (int note : {61, 63, 66, 68, 70}) {
                blacks.push_back({note, {60.0f, 0.0f}});
            }
            const Frame alone = plan_frame(screen, look, kRange61, blacks, BlackKeyNames::Always);
            for (std::size_t i = 0; i < blacks.size(); ++i) {
                const BlockNameIn& in = alone.inputs[i];
                if (screen.w < 1366.0f && alone.out[i].form == NameForm::Hidden) {
                    continue;
                }
                REQUIRE(alone.out[i].form != NameForm::Hidden);
                CHECK(alone.out[i].rect.x >= in.lane_x - 0.001f);
                CHECK(alone.out[i].rect.right() <= in.lane_x + in.lane_w + 0.001f);
            }
            // Among white keys' names: Off names none; Always names at least what Auto does, and
            // never over another name.
            std::vector<std::pair<int, std::pair<float, float>>> notes;
            for (int note : {60, 61, 62, 63, 64, 65, 66}) {
                notes.push_back({note, {60.0f, 0.0f}});
            }
            const Frame off = plan_frame(screen, look, kRange61, notes, BlackKeyNames::Off);
            const Frame autos = plan_frame(screen, look, kRange61, notes, BlackKeyNames::Auto);
            const Frame always = plan_frame(screen, look, kRange61, notes, BlackKeyNames::Always);
            for (std::size_t i = 0; i < notes.size(); ++i) {
                if (is_black_key(notes[i].first)) {
                    CHECK(off.out[i].form == NameForm::Hidden);
                    if (autos.out[i].form != NameForm::Hidden) {
                        CHECK(always.out[i].form != NameForm::Hidden);
                    }
                }
                for (std::size_t j = i + 1; j < notes.size(); ++j) {
                    if (always.out[i].form != NameForm::Hidden &&
                        always.out[j].form != NameForm::Hidden) {
                        CHECK_FALSE(rects_overlap(always.out[i].rect, always.out[j].rect));
                    }
                }
            }
        }
    }
}

TEST_CASE("planning twice gives the same answer") {
    const LookSettings look;
    std::vector<std::pair<int, std::pair<float, float>>> notes;
    for (int note : {60, 61, 62, 63, 64}) {
        notes.push_back({note, {50.0f, 0.0f}});
    }
    const Frame a = plan_frame(kScreens[1], look, kRange61, notes);
    const Frame b = plan_frame(kScreens[1], look, kRange61, notes);
    for (std::size_t i = 0; i < a.out.size(); ++i) {
        CHECK(a.out[i].form == b.out[i].form);
        CHECK(a.out[i].rect.x == b.out[i].rect.x);
        CHECK(a.out[i].font_px == b.out[i].font_px);
    }
}

TEST_CASE("stand-in: a texture of another size fits when its height and stretch stay in the box") {
    // 8 px text, 6 wide and 12 tall, standing in for 12 px text planned 9 wide: both factors 1.5.
    CHECK(stand_in_height(8.0f, 6.0f, 12.0f, 12.0f, 9.0f, 20.0f, 0.2f) == doctest::Approx(18.0f));
    // The box is exactly as tall as the stretched texture: it fits; a hair less: it does not.
    CHECK(stand_in_height(8.0f, 6.0f, 12.0f, 12.0f, 9.0f, 18.0f, 0.2f) == 18.0f);
    CHECK(stand_in_height(8.0f, 6.0f, 12.0f, 12.0f, 9.0f, 17.99f, 0.2f) == 0.0f);
    // Hinting makes the planned width differ from the linear one: allowed up to the skew, not past
    // it (sy is 1.5, so sx may be 1.5 +- 0.3, that is a width of 7.2 to 10.8).
    CHECK(stand_in_height(8.0f, 6.0f, 12.0f, 12.0f, 10.7f, 20.0f, 0.2f) > 0.0f);
    CHECK(stand_in_height(8.0f, 6.0f, 12.0f, 12.0f, 10.9f, 20.0f, 0.2f) == 0.0f);
    CHECK(stand_in_height(8.0f, 6.0f, 12.0f, 12.0f, 7.3f, 20.0f, 0.2f) > 0.0f);
    CHECK(stand_in_height(8.0f, 6.0f, 12.0f, 12.0f, 7.1f, 20.0f, 0.2f) == 0.0f);
}

TEST_CASE("stand-in: nothing for sizes that are not positive finite numbers") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(stand_in_height(0.0f, 6.0f, 12.0f, 12.0f, 7.2f, 20.0f, 0.2f) == 0.0f);
    CHECK(stand_in_height(10.0f, 0.0f, 12.0f, 12.0f, 7.2f, 20.0f, 0.2f) == 0.0f);
    CHECK(stand_in_height(10.0f, 6.0f, 12.0f, 12.0f, nan, 20.0f, 0.2f) == 0.0f);
    CHECK(stand_in_height(10.0f, 6.0f, 12.0f, 12.0f, 7.2f, -1.0f, 0.2f) == 0.0f);
}

TEST_CASE(
    "a name never reaches above name_clearance_px of its planned size from the block's foot") {
    // Wide and narrow blocks, white and black keys, the largest size the frame allows and smaller:
    // whatever the planner places sits within the clearance of its own font, and so within the
    // clearance of the largest font.
    for (float max_font : {12.0f, 17.0f, 26.0f, 40.0f}) {
        const BlockNameParams params = params_for(max_font);
        for (int note : {60, 61, 63, 66}) {
            for (float w : {12.0f, 20.0f, 36.0f, 80.0f}) {
                CAPTURE(max_font);
                CAPTURE(note);
                CAPTURE(w);
                const BlockNameIn in = block_in(tall(w), note);
                const BlockNamePlacement p = plan_one(params, in);
                if (p.form == NameForm::Hidden) {
                    continue;
                }
                const float foot = in.block.bottom();
                CHECK(foot - p.rect.y <= name_clearance_px(params, p.font_px) + 0.001f);
                CHECK(name_clearance_px(params, p.font_px) <=
                      name_clearance_px(params, params.max_font_px) + 0.001f);
            }
        }
    }
    CHECK(name_clearance_px(params_for(), 0.0f) == 0.0f);
}
