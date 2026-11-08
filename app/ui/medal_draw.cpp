// SPDX-License-Identifier: GPL-3.0-or-later
#include "medal_draw.hpp"

#include "medal_atlas.hpp"
#include "medal_emblem.hpp"
#include "progress_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace etude::app::ui {

namespace {

struct IconRow {
    const char* name;
    const char* glyph;
};

// The Lucide glyphs the catalogue and the categories name (the icon font is merged into every font).
constexpr IconRow kIcons[] = {
    {"flame", "\xEE\x83\x92"},
    {"calendar-check", "\xEE\x8A\xB7"},
    {"calendar-range", "\xEE\x8A\xBD"},
    {"rotate-ccw", "\xEE\x85\x88"},
    {"target", "\xEE\x86\x80"},
    {"crosshair", "\xEE\x82\xAC"},
    {"hand", "\xEE\x87\x97"},
    {"gauge", "\xEE\x86\xBF"},
    {"star", "\xEE\x85\xB6"},
    {"zap", "\xEE\x86\xB4"},
    {"list-music", "\xEE\x8B\xA0"},
    {"gem", "\xEE\x89\x82"},
    {"compass", "\xEE\x82\x9B"},
    {"heart", "\xEE\x83\xB2"},
    {"mountain", "\xEE\x88\xB1"},
    {"handshake", "\xEE\x97\x80"},
    {"clock", "\xEE\x82\x87"},
    {"hourglass", "\xEE\x8A\x96"},
    {"repeat", "\xEE\x85\x86"},
    {"scissors", "\xEE\x85\x8E"},
    {"calendar-days", "\xEE\x8A\xB9"},
    {"medal", "\xEE\x8D\xAF"},
    {"cake", "\xEE\x8D\x84"},
    {"sparkles", "\xEE\x90\x92"},
    {"trophy", "\xEE\x8D\xB3"},
    {"music", "\xEE\x84\xA2"},
    {"timer", "\xEE\x87\xA0"},
    {"snowflake", "\xEE\x85\xA5"},
    {"layout-grid", "\xEE\x83\xBF"},
    {"rows-3", "\xEE\x96\x8A"},
    {"x", "\xEE\x86\xB2"},
    {"crown", "\xEE\x87\x96"},
};

constexpr float kIconShare = 0.45f;   // the icon's em, over the disc's diameter

ImVec4 hex(std::uint32_t c, float alpha = 1.0f) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f, alpha};
}

} // namespace

const char* lucide_glyph(std::string_view name) {
    for (const IconRow& row : kIcons) {
        if (name == row.name) {
            return row.glyph;
        }
    }
    return "";
}

ImVec4 category_colour(etude::MedalCategory category) {
    switch (category) {
    case etude::MedalCategory::Streaks:
        return hex(0xFF9A3C);
    case etude::MedalCategory::Accuracy:
        return hex(0x7FE3A4);
    case etude::MedalCategory::Songs:
        return hex(0x5AAAFF);
    case etude::MedalCategory::Time:
        return hex(0x1FBFAE);
    case etude::MedalCategory::Milestones:
        return hex(0xF5C84A);
    }
    return hex(0xFFFFFF);
}

const char* category_glyph(etude::MedalCategory category) {
    switch (category) {
    case etude::MedalCategory::Streaks:
        return lucide_glyph("flame");
    case etude::MedalCategory::Accuracy:
        return lucide_glyph("target");
    case etude::MedalCategory::Songs:
        return lucide_glyph("music");
    case etude::MedalCategory::Time:
        return lucide_glyph("timer");
    case etude::MedalCategory::Milestones:
        return lucide_glyph("trophy");
    }
    return "";
}

void draw_medal_of(const Kit& k, ImDrawList* dl, etude::Material material, const char* icon_glyph,
                   ImVec2 centre, float d, bool large, bool ghost, float dim) {
    const float em = d * kIconShare;
    if (ghost) { // not earned: the first medal, dim and grey, its icon a plain dim mark
        if (k.medals != nullptr) {
            k.medals->draw(dl, etude::Material::Iron, large, centre, d, {0.55f, 0.58f, 0.66f, 0.38f * dim});
        }
        glyph(k, dl, icon_glyph, centre, em, ImVec4{0.62f, 0.66f, 0.76f, 0.55f * dim});
        return;
    }
    if (k.medals != nullptr) {
        k.medals->draw(dl, material, large, centre, d, {1.0f, 1.0f, 1.0f, dim});
    }
    // The icon on the face, embossed: a dark edge down and right, a light one up and left, the fill.
    const etude::MedalLook look = etude::medal_look(material);
    const float off = std::max(1.0f, d * 0.5f * 0.69f * 0.07f);
    glyph(k, dl, icon_glyph, {centre.x + off, centre.y + off}, em, hex(look.emboss_dark, dim));
    glyph(k, dl, icon_glyph, {centre.x - off * 0.7f, centre.y - off * 0.7f}, em, hex(look.emboss_light, dim));
    glyph(k, dl, icon_glyph, centre, em, hex(look.icon_fill, dim));
}

void draw_medal(const Kit& k, ImDrawList* dl, etude::Achievement achievement, int tier, ImVec2 centre,
                float d, bool large) {
    const etude::AchievementInfo& info = etude::achievement_info(achievement);
    draw_medal_of(k, dl, etude::tier_material(std::max(tier, 1)), lucide_glyph(info.icon), centre, d, large,
                  tier <= 0);
}

void name_and_title(const Kit& k, ImDrawList* dl, const Box& slot, float size, const char* name, const char* title,
                    ImVec4 ink) {
    const float name_w = text_size(k.bold, size, name).x;
    char tail[192];
    std::snprintf(tail, sizeof tail, "\xC2\xB7 %s", title);
    const float gap = size * progress_tok::kTitleGap;
    const float tail_w = text_size(k.regular, size, tail).x;
    if (title[0] == '\0' || name_w + gap + tail_w > slot.w()) {
        const FitText cut = fit(k.bold, size, name, slot.w());
        label_at(k, dl, k.bold, size, slot, LabelAlign::Left, ink, cut.c_str());
        return;
    }
    label_at(k, dl, k.bold, size, slot, LabelAlign::Left, ink, name);
    label_at(k, dl, k.regular, size, {slot.x0 + name_w + gap, slot.y0, slot.x1, slot.y1}, LabelAlign::Left,
             col::kText2, tail);
}

void name_title_medal(const Kit& k, ImDrawList* dl, const Box& slot, float size, const char* name, int title,
                      ImVec4 ink) {
    const float name_w = text_size(k.bold, size, name).x;
    const char* words = title > 0 ? etude::tier_title(title).data() : ""; // a view of a literal: terminated
    const float gap = size * 0.4f;
    const float d = size * 1.35f;
    const float words_w = text_size(k.regular, size, words).x;
    if (title <= 0 || name_w + gap + d + gap * 0.6f + words_w > slot.w()) {
        const FitText cut = fit(k.bold, size, name, slot.w());
        label_at(k, dl, k.bold, size, slot, LabelAlign::Left, ink, cut.c_str());
        return;
    }
    label_at(k, dl, k.bold, size, slot, LabelAlign::Left, ink, name);
    const float x = slot.x0 + name_w + gap;
    draw_title_medal_small(k, dl, title, {x + d * 0.5f, slot.cy()}, d);
    label_at(k, dl, k.regular, size, {x + d + gap * 0.6f, slot.y0, slot.x1, slot.y1}, LabelAlign::Left, col::kText2,
             words);
}

float draw_freezes(const Kit& k, ImDrawList* dl, ImVec2 left, float size, int freezes, bool width_only) {
    const float icon = size * 1.05f;
    char count[16];
    std::snprintf(count, sizeof count, "%d", freezes);
    const float gap = size * 0.25f;
    const float count_w = text_size(k.bold, size, count).x;
    const float width = icon + gap + count_w;
    if (!width_only) {
        const ImVec4 ice{0.49f, 0.83f, 0.99f, 1.0f};
        const ImVec4 colour = freezes > 0 ? ice : col::kText3;
        glyph(k, dl, lucide_glyph("snowflake"), {left.x + icon * 0.5f, left.y}, icon, colour);
        label_at(k, dl, k.bold, size, {left.x + icon + gap, left.y - size, left.x + width, left.y + size}, LabelAlign::Left,
                 freezes > 0 ? col::kText : col::kText3, count);
    }
    return width;
}

void draw_medal_ring(Kit& k, ImDrawList* dl, ImVec2 centre, float d, const etude::MedalSet& set,
                     const char* label) {
    constexpr float kPi = 3.14159265f;
    const float stroke = std::max(2.0f, d * 0.12f);
    const float radius = d * 0.5f - stroke * 0.5f;
    const int n = static_cast<int>(etude::kMedalCategoryCount);
    const float slice = 2.0f * kPi / static_cast<float>(n);
    const float gap = slice * 0.10f;
    for (int i = 0; i < n; ++i) {
        const auto category = static_cast<etude::MedalCategory>(i);
        const float a0 = -kPi * 0.5f + static_cast<float>(i) * slice + gap * 0.5f;
        const float a1 = a0 + slice - gap;
        dl->PathArcTo(centre, radius, a0, a1);
        dl->PathStroke(u32(with_alpha(col::kText, 0.16f)), 0, stroke);
        const float share = static_cast<float>(etude::category_points(set, category)) /
                            static_cast<float>(std::max(1, etude::category_points_possible(category)));
        if (share > 0.0f) {
            dl->PathArcTo(centre, radius, a0, a0 + (a1 - a0) * std::min(1.0f, share));
            dl->PathStroke(u32(category_colour(category)), 0, stroke);
        }
    }
    const float inner = d - 2.0f * stroke;
    const Box slot{centre.x - inner * 0.5f, centre.y - inner * 0.5f, centre.x + inner * 0.5f,
                   centre.y + inner * 0.5f};
    label_at(k, dl, k.bold, std::min(k.s(tok::kLabel) * 1.15f, inner * 0.42f), slot, LabelAlign::Centre,
             col::kText, label);
}

} // namespace etude::app::ui
