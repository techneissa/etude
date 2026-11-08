// SPDX-License-Identifier: GPL-3.0-or-later
// Baking the glyphs the pages use into the UI's font atlas a few at a time (Ui::draw).
#pragma once

#include "../frame_log.hpp"
#include "card_kit.hpp"
#include "exact_shot.hpp"
#include "kit.hpp"
#include "results_card.hpp"
#include "results_card_layout.hpp"
#include "tokens.hpp"

#include <imgui.h>
#include <imgui_internal.h> // ImTextCharFromUtf8

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

namespace etude::app::ui {

// The text the pages draw is baked into the font atlas a glyph at a time the first time it is
// used, which costs a few milliseconds when a page of new words opens. A few of the glyphs the
// pages are made of (the printable ASCII of both fonts at the four text sizes) are baked in each
// of the frames after start-up and again after a change of scale, so a page finds them done.
struct GlyphWarmUp {
    float scale = 0.0f;      // the scale the glyphs were baked for
    float card_scale = 0.0f; // and the scale the results card draws at when it is not the same
    std::size_t next = 0;
};

constexpr std::int64_t kGlyphWarmSliceNs = 500'000;
constexpr int kGlyphWarmAfterFrames = 2;
constexpr float kWarmSizes[] = {tok::kBody, tok::kLabel, tok::kCaption, tok::kTitle,
                                card_tok::kFigureSize};
constexpr char kWarmFirst = ' ';
constexpr char kWarmLast = '~';
// Beyond ASCII: the middle dot that separates the status and results lines, the en dash for "no
// accuracy", the ellipsis a cut line ends in, the minus of a negative value or a skip step
// (U+2212, "\xE2\x88\x925s") and the times sign of a count (U+00D7).
constexpr ImWchar kWarmExtra[] = {0x00B7, 0x00D7, 0x2013, 0x2026, 0x2212};
constexpr std::size_t kWarmTextGlyphs = static_cast<std::size_t>(kWarmLast - kWarmFirst) + 1;
constexpr std::size_t kWarmGlyphs = kWarmTextGlyphs + std::size(kWarmExtra);
constexpr std::size_t kWarmText = 2 * std::size(kWarmSizes) * kWarmGlyphs;
// The card draws at a scale of its own on a big screen (core `card_size`): the text it uses, at
// that scale, is baked after the rest. Its sizes and the font each is drawn in.
struct CardWarm {
    float size;
    bool bold;
};
constexpr CardWarm kCardWarm[] = {{tok::kBody, true},
                                  {tok::kBody, false},
                                  {tok::kLabel, true},
                                  {tok::kLabel, false},
                                  {tok::kCaption, true},
                                  {tok::kCaption, false},
                                  {card_tok::kFigureSize, true},
                                  {tok::kMark, true}};
constexpr std::size_t kCardWarmText = std::size(kCardWarm) * kWarmGlyphs;

inline void warm_glyphs(Kit& k, GlyphWarmUp& w, float card_scale) {
    const float scale = k.s(tok::kBody);
    // Only a card bigger than the UI's own scale needs its own sizes.
    const float own_card = card_scale > k.u * 1.001f ? card_scale : 0.0f;
    if (scale != w.scale || own_card != w.card_scale) {
        w.scale = scale;
        w.card_scale = own_card;
        w.next = 0;
    }
    // A screenshot bakes every glyph in one frame, so the atlas is laid out the same on every run.
    const bool exact = exact_shot();
    const std::int64_t deadline = exact ? 0 : frame_clock_ns() + kGlyphWarmSliceNs;
    const std::span<const CardIcon> icons = card_warm_icons();
    const std::size_t total = kWarmText + icons.size() + (own_card > 0.0f ? kCardWarmText : 0);
    while (w.next < total && (exact || frame_clock_ns() < deadline)) {
        const std::size_t i = w.next++;
        if (i >= kWarmText + icons.size()) {
            const std::size_t j = i - kWarmText - icons.size();
            const CardWarm& cw = kCardWarm[j / kWarmGlyphs];
            const std::size_t glyph = j % kWarmGlyphs;
            ImGui::PushFont(cw.bold ? k.bold : k.regular, own_card * cw.size);
            ImGui::GetFontBaked()->FindGlyph(glyph < kWarmTextGlyphs
                                                 ? static_cast<ImWchar>(kWarmFirst + glyph)
                                                 : kWarmExtra[glyph - kWarmTextGlyphs]);
            ImGui::PopFont();
            continue;
        }
        if (i >= kWarmText) {
            // The icons of the results card, at the sizes it draws them.
            const CardIcon& card_icon = icons[i - kWarmText];
            unsigned int cp = 0;
            if (ImTextCharFromUtf8(&cp, card_icon.glyph, nullptr) > 0) {
                ImGui::PushFont(k.regular, k.s(card_icon.size));
                ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(cp));
                ImGui::PopFont();
            }
            continue;
        }
        const std::size_t glyph = i % kWarmGlyphs;
        const std::size_t size = (i / kWarmGlyphs) % std::size(kWarmSizes);
        ImFont* font = i / (kWarmGlyphs * std::size(kWarmSizes)) == 0 ? k.regular : k.bold;
        ImGui::PushFont(font, k.s(kWarmSizes[size]));
        ImGui::GetFontBaked()->FindGlyph(glyph < kWarmTextGlyphs
                                             ? static_cast<ImWchar>(kWarmFirst + glyph)
                                             : kWarmExtra[glyph - kWarmTextGlyphs]);
        ImGui::PopFont();
    }
}

} // namespace etude::app::ui
