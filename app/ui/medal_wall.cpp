// SPDX-License-Identifier: GPL-3.0-or-later
#include "medal_wall.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "medal_draw.hpp"
#include "progress_charts.hpp"
#include "tap.hpp"

#include <algorithm>
#include <cstdio>

namespace etude::app::ui {

namespace {

namespace w = wall_tok;

constexpr const char* kDot = " \xC2\xB7 ";

// The words under a medal: the material held and what the rule counted, or what the first tier asks.
std::string caption_of(const etude::AchievementInfo& info, int tier, std::int64_t value) {
    if (tier <= 0) {
        return etude::threshold_words(info, info.tiers[0]) + " to start";
    }
    return std::string(etude::tier_material_name(tier)) + kDot + etude::threshold_words(info, value);
}

int category_index(etude::MedalCategory c) { return static_cast<int>(c); }

// The name and the caption under a medal, measured at the whole size the kit draws them at, in `room`.
struct Under {
    WallText name;
    WallText caption;
};

Under under_medal(const Kit& k, const etude::AchievementInfo& info, const std::string& caption, float room,
                  WallLines most = {2, 2}) {
    const auto width = [&](ImFont* font) {
        return [font](const std::string& text, float pt) { return label_width(font, pt, text.c_str()); };
    };
    const float least = wall_tok::kCaptionMin;
    return {wall_name(std::string(info.name), k.s(tok::kBody), k.s(tok::kCaption), room, least, width(k.bold),
                      most.name),
            wall_caption(caption, k.s(tok::kCaption), room, least, width(k.regular), most.caption)};
}

// `text`'s lines, each in its own `line_h` of `slot`, from the top; a line no wrap could fit is cut.
void draw_lines(const Kit& k, ImDrawList* dl, const Box& slot, float line_h, ImFont* font, const WallText& text,
                ImVec4 colour) {
    const float room = slot.w() - k.s(tok::kHitGap);
    for (int i = 0; i < text.lines; ++i) {
        const FitText line = fit(font, text.pt, text.line[static_cast<std::size_t>(i)], room);
        const float y = slot.y0 + line_h * static_cast<float>(i);
        label_at(k, dl, font, text.pt, {slot.x0, y, slot.x1, y + line_h}, LabelAlign::Centre, colour, line.c_str());
    }
}

} // namespace

void MedalWall::refresh(const etude::MedalSet& set) {
    if (have_cache_ && cached_ == set) {
        return;
    }
    cached_ = set;
    have_cache_ = true;
    for (auto& m : members_) {
        m.clear();
    }
    for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
        const etude::AchievementInfo& info = etude::achievement_info(static_cast<etude::Achievement>(i));
        words_[i].caption = caption_of(info, set.tier[i], set.value[i]);
        members_[static_cast<std::size_t>(category_index(info.category))].push_back(static_cast<int>(i));
    }
    near_ = etude::closest_to_next(set, 3);
    for (std::size_t i = 0; i < near_.size() && i < near_have_.size(); ++i) {
        const etude::AchievementInfo& info = etude::achievement_info(near_[i].achievement);
        near_have_[i] = etude::threshold_words(info, near_[i].value) + " of " +
                        etude::threshold_words(info, near_[i].next);
    }
    ladder_words_ = {};
}

void MedalWall::refresh_ladder(const etude::AchievementInfo& info, int held) {
    if (ladder_words_.achievement == ladder_ && ladder_words_.held == held) {
        return;
    }
    ladder_words_.achievement = ladder_;
    ladder_words_.held = held;
    ladder_words_.rule = etude::rule_words(info, std::min(held + 1, etude::kTierCount));
    for (int t = 1; t <= etude::kTierCount; ++t) {
        ladder_words_.asks[static_cast<std::size_t>(t - 1)] =
            etude::threshold_words(info, info.tiers[static_cast<std::size_t>(t - 1)]);
    }
}

bool MedalWall::press_slot(Kit& k, const WallSlot& slot, int achievement) {
    char id[40];
    std::snprintf(id, sizeof id, "##wall-medal-%d", achievement);
    // The hit area leaves half the dead gap at each side, so neighbours are never closer than `kHitGap`.
    const float half = k.s(tok::kHitGap) * 0.5f;
    const Box hit{slot.cell.x0 + half, slot.cell.y0, slot.cell.x1 - half, slot.cell.y1};
    const bool pressed = press_area(k, id, hit);
    const State st = item_state(k, ladder_ == achievement);
    draw_state(k, ImGui::GetWindowDrawList(), id, hit, hit, st, Idle::None);
    audit::note(id, {hit.x0, hit.y0}, {hit.x1, hit.y1}, InkKind::Target);
    return tapped(pressed);
}

void MedalWall::draw_tabs(Kit& k, ImDrawList* dl, const WallLayout& lay) {
    // The five categories share one track with one raised pill on the chosen one (the kit's tabs).
    const int bold = tab_track(k, dl, "##wall-tabs", lay.tab, category_);
    for (int i = 0; i < 5; ++i) {
        const auto category = static_cast<etude::MedalCategory>(i);
        char id[32];
        std::snprintf(id, sizeof id, "##wall-tab-%d", i);
        const bool on = category_ == i;
        const TabHit hit = tab_cell(k, dl, id, lay.tab[static_cast<std::size_t>(i)], on);
        const Box& face = hit.pill;
        const State& st = hit.state;
        const char* name = etude::medal_category_name(category).data();
        const bool named = lay.tab_names || on;
        const float icon = k.s(tok::kIconSm);
        const float pad = k.s(tok::kS2);
        if (named) {
            // One face of the words, bold once the pill's centre has passed the middle to this cell.
            ImFont* const font = i == bold ? k.bold : k.regular;
            const float text_w = text_size(font, k.s(tok::kLabel), name).x;
            const float group = icon + k.s(tok::kS1) + text_w;
            const float x = face.cx() - group * 0.5f;
            glyph(k, dl, category_glyph(category), {x + icon * 0.5f, face.cy()}, icon, category_colour(category));
            const FitText cut = fit(font, k.s(tok::kLabel), name,
                                    face.x1 - pad - (x + icon + k.s(tok::kS1)));
            fade_label(k, dl, k.s(tok::kLabel), {x + icon + k.s(tok::kS1), face.y0, face.x1 - pad, face.y1},
                       LabelAlign::Left, cut.c_str(), i == bold, hit.weight, state_ink(st, col::kText2),
                       state_ink(st, col::kText));
        } else {
            glyph(k, dl, category_glyph(category), {face.cx(), face.cy()}, icon, category_colour(category));
        }
        if (hit.tapped) {
            category_ = i;
        }
    }
}

void MedalWall::draw_category(Kit& k, ImDrawList* dl, const WallLayout& lay, const etude::MedalSet& set) {
    const std::vector<int>& list = members_[static_cast<std::size_t>(category_)];
    for (int i = 0; i < lay.slots && i < static_cast<int>(list.size()); ++i) {
        const WallSlot& slot = lay.slot[static_cast<std::size_t>(i)];
        const int a = list[static_cast<std::size_t>(i)];
        const auto id = static_cast<etude::Achievement>(a);
        const etude::AchievementInfo& info = etude::achievement_info(id);
        const int tier = set.tier[static_cast<std::size_t>(a)];
        draw_medal(k, dl, id, tier, {slot.cx, slot.cy}, slot.d, true);
        const Under under = under_medal(k, info, words_[static_cast<std::size_t>(a)].caption,
                                        wall_text_room(slot.cell.w(), k.u), lay.under);
        draw_lines(k, dl, slot.name, w::kNameH * k.u, k.bold, under.name, tier > 0 ? col::kText : col::kText2);
        draw_lines(k, dl, slot.caption, w::kCaptionH * k.u, k.regular, under.caption, col::kText2);
        if (ladder_ < 0 && press_slot(k, slot, a)) {
            ladder_ = a;
        }
    }
}

void MedalWall::draw_rows(Kit& k, ImDrawList* dl, const WallLayout& lay, const etude::MedalSet& set) {
    for (int r = 0; r < lay.rows; ++r) {
        const WallLayout::Row& row = lay.row[static_cast<std::size_t>(r)];
        const auto category = static_cast<etude::MedalCategory>(r);
        dl->AddRectFilled({row.row.x0, row.row.y0}, {row.row.x1, row.row.y1}, u32(col::kFill),
                          k.s(tok::kRadiusControl));
        const float icon = k.s(tok::kIcon);
        const float pad = k.s(tok::kS2);
        const etude::MedalCategory c = category;
        const float line = k.s(tok::kLabel) * 1.4f;
        const float small = k.s(tok::kCaption) * 1.4f;
        if (lay.row_head_icon) { // a narrow body: the icon alone
            glyph(k, dl, category_glyph(category), {row.head.cx(), row.head.cy()}, icon, category_colour(category));
        } else if (lay.row_stacked) { // the icon, the name, the count: three lines round the row's middle
            const float total = icon + k.s(tok::kS1) + line + small;
            const float top = row.head.cy() - total * 0.5f;
            glyph(k, dl, category_glyph(category), {row.head.x0 + pad + icon * 0.5f, top + icon * 0.5f}, icon,
                  category_colour(category));
            const FitText name = fit(k.bold, k.s(tok::kLabel), etude::medal_category_name(c), row.head.w() - pad);
            const float y = top + icon + k.s(tok::kS1);
            label_at(k, dl, k.bold, k.s(tok::kLabel), {row.head.x0 + pad, y, row.head.x1, y + line}, LabelAlign::Left,
                     col::kText, name.c_str());
            char points[32];
            std::snprintf(points, sizeof points, "%d of %d medals", etude::category_points(set, c),
                          etude::category_points_possible(c));
            const FitText count = fit(k.regular, k.s(tok::kCaption), points, row.head.w() - pad);
            label_at(k, dl, k.regular, k.s(tok::kCaption), {row.head.x0 + pad, y + line, row.head.x1, y + line + small},
                     LabelAlign::Left, col::kText2, count.c_str());
        } else { // a short row: the icon and the name on one line
            glyph(k, dl, category_glyph(category), {row.head.x0 + pad + icon * 0.5f, row.head.cy()}, icon,
                  category_colour(category));
            const float x = row.head.x0 + pad + icon + k.s(tok::kS1);
            const FitText name = fit(k.bold, k.s(tok::kLabel), etude::medal_category_name(c), row.head.x1 - x);
            label_at(k, dl, k.bold, k.s(tok::kLabel), {x, row.head.y0, row.head.x1, row.head.y1}, LabelAlign::Left,
                     col::kText, name.c_str());
        }
        const std::vector<int>& list = members_[static_cast<std::size_t>(r)];
        for (int i = 0; i < row.slots && i < static_cast<int>(list.size()); ++i) {
            const WallSlot& slot = row.slot[static_cast<std::size_t>(i)];
            const int a = list[static_cast<std::size_t>(i)];
            const auto id = static_cast<etude::Achievement>(a);
            const etude::AchievementInfo& info = etude::achievement_info(id);
            const int tier = set.tier[static_cast<std::size_t>(a)];
            draw_medal(k, dl, id, tier, {slot.cx, slot.cy}, slot.d, false);
            if (lay.row_names) {
                const FitText nm = fit(k.regular, k.s(tok::kCaption), info.name, slot.cell.w() - k.s(tok::kS1));
                label_at(k, dl, k.regular, k.s(tok::kCaption), slot.name, LabelAlign::Centre,
                         tier > 0 ? col::kText : col::kText3, nm.c_str());
            }
            if (ladder_ < 0 && press_slot(k, slot, a)) {
                ladder_ = a;
            }
        }
    }
}

void MedalWall::draw_strip(Kit& k, ImDrawList* dl, const WallLayout& lay) {
    if (lay.nears <= 0) {
        return;
    }
    label_at(k, dl, k.bold, k.s(tok::kCaption), lay.strip_title, LabelAlign::Left, col::kText2,
             "Closest to the next tier");
    for (int i = 0; i < lay.nears && i < static_cast<int>(near_.size()); ++i) {
        const etude::MedalProgress& m = near_[static_cast<std::size_t>(i)];
        const Box card = lay.near[static_cast<std::size_t>(i)];
        const etude::AchievementInfo& info = etude::achievement_info(m.achievement);
        dl->AddRectFilled({card.x0, card.y0}, {card.x1, card.y1}, u32(col::kFill), k.s(tok::kRadiusControl));
        const float d = std::min(k.s(w::kNearMedal), card.h() - k.s(tok::kS2));
        const ImVec2 c{card.x0 + k.s(tok::kS2) + d * 0.5f, card.cy()};
        draw_medal_of(k, dl, etude::tier_material(m.tier + 1), lucide_glyph(info.icon), c, d, false, false, 0.85f);
        const Box text{c.x + d * 0.5f + k.s(tok::kS2), card.y0, card.x1 - k.s(tok::kS2), card.y1};
        const FitText name = fit(k.bold, k.s(tok::kLabel), info.name, text.w());
        const FitText sub = fit(k.regular, k.s(tok::kCaption), near_have_[static_cast<std::size_t>(i)], text.w());
        const StackLine top{k.bold, k.s(tok::kLabel), col::kText, name.c_str()};
        const StackLine bottom{k.regular, k.s(tok::kCaption), col::kText2, sub.c_str()};
        label_stack(k, dl, {text.x0, text.y0, text.x1, text.y1 - k.s(tok::kS2)}, top, &bottom);
        // How far it is on its way, a thin bar along the card's foot.
        const float bar_h = std::max(2.0f, k.s(tok::kS1) * 0.75f);
        const Box track{card.x0 + k.s(tok::kS2), card.y1 - k.s(tok::kS2) - bar_h * 0.5f, card.x1 - k.s(tok::kS2),
                        card.y1 - k.s(tok::kS2) + bar_h * 0.5f};
        dl->AddRectFilled({track.x0, track.y0}, {track.x1, track.y1}, u32(with_alpha(col::kText, 0.10f)), bar_h * 0.5f);
        dl->AddRectFilled({track.x0, track.y0},
                          {track.x0 + track.w() * static_cast<float>(std::clamp(m.share, 0.0, 1.0)), track.y1},
                          u32(category_colour(info.category)), bar_h * 0.5f);
    }
}

void MedalWall::draw_ladder(Kit& k, ImDrawList* dl, const Box& body, const etude::MedalSet& set) {
    const auto id = static_cast<etude::Achievement>(ladder_);
    const etude::AchievementInfo& info = etude::achievement_info(id);
    const int held = set.tier[static_cast<std::size_t>(ladder_)];
    const LadderLayout lay = plan_ladder(body, k.u);
    // A tap outside the card closes the ladder, on release (read from the pointer: no hit area behind the card).
    const ImVec2 at = ImGui::GetIO().MousePos;
    const bool inside = at.x >= lay.card.x0 && at.x <= lay.card.x1 && at.y >= lay.card.y0 && at.y <= lay.card.y1;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !inside) {
        ladder_ = -1;
        return;
    }
    dl->AddRectFilled({lay.card.x0, lay.card.y0}, {lay.card.x1, lay.card.y1}, u32(col::kSurface),
                      k.s(tok::kRadiusControl) + k.s(tok::kS1));
    dl->AddRect({lay.card.x0, lay.card.y0}, {lay.card.x1, lay.card.y1}, u32(with_alpha(col::kText, 0.14f)),
                k.s(tok::kRadiusControl) + k.s(tok::kS1), 0, k.s(tok::kHairline));
    refresh_ladder(info, held);
    const std::string& rule = ladder_words_.rule;
    if (lay.one_line) { // the name, then the rule, on one line
        name_and_title(k, dl, lay.title, k.s(tok::kBody), info.name.data(), rule.c_str(), col::kText);
    } else {
        const FitText name = fit(k.bold, k.s(tok::kTitle), info.name, lay.title.w());
        label_at(k, dl, k.bold, k.s(tok::kTitle), lay.title, LabelAlign::Left, col::kText, name.c_str());
        const FitText rule_cut = fit(k.regular, k.s(tok::kBody), rule, lay.rule.w());
        label_at(k, dl, k.regular, k.s(tok::kBody), lay.rule, LabelAlign::Left, col::kText2, rule_cut.c_str());
    }
    ImGui::SetCursorScreenPos({lay.close.x0, lay.close.y0});
    if (icon_action(k, "##wall-ladder-close", icon::kClose, "Close", lay.close.w(), true)) {
        ladder_ = -1;
        return;
    }
    for (int t = 1; t <= etude::kTierCount; ++t) {
        const WallSlot& slot = lay.rung[static_cast<std::size_t>(t - 1)];
        const bool reached = t <= held;
        if (t == held) {
            const Box cell = slot.cell;
            dl->AddRect({cell.x0 + k.s(tok::kS1) * 0.5f, cell.y0}, {cell.x1 - k.s(tok::kS1) * 0.5f, cell.y1},
                        u32(category_colour(info.category)), k.s(tok::kRadiusControl), 0, k.s(tok::kFocusRing));
        }
        draw_medal_of(k, dl, etude::tier_material(t), lucide_glyph(info.icon), {slot.cx, slot.cy}, slot.d, false,
                      false, reached ? 1.0f : 0.35f);
        const FitText tm = fit(k.bold, k.s(tok::kCaption), etude::tier_material_name(t), slot.cell.w() - k.s(tok::kS1));
        label_at(k, dl, k.bold, k.s(tok::kCaption), slot.name, LabelAlign::Centre,
                 reached ? col::kText : col::kText3, tm.c_str());
        const FitText ask = fit(k.regular, k.s(tok::kCaption), ladder_words_.asks[static_cast<std::size_t>(t - 1)],
                                slot.cell.w() - k.s(tok::kS1));
        label_at(k, dl, k.regular, k.s(tok::kCaption), slot.caption, LabelAlign::Centre, col::kText2, ask.c_str());
    }
}

// The lines the chosen category's names and captions need, so its medals stand level.
WallLines MedalWall::lines_under(const Kit& k, const Box& body, const std::array<int, 5>& counts) const {
    WallLines out;
    const float room = wall_text_room(wall_cell_w(body, counts[static_cast<std::size_t>(category_)]), k.u);
    for (const int a : members_[static_cast<std::size_t>(category_)]) {
        const etude::AchievementInfo& info = etude::achievement_info(static_cast<etude::Achievement>(a));
        const Under under = under_medal(k, info, words_[static_cast<std::size_t>(a)].caption, room);
        out.name = std::max(out.name, under.name.lines);
        out.caption = std::max(out.caption, under.caption.lines);
    }
    return out;
}

void MedalWall::draw(Kit& k, ImDrawList* dl, const Box& body, etude::AppSettings& app,
                     const etude::MedalSet& set) {
    refresh(set);
    std::array<int, 5> counts{};
    for (std::size_t i = 0; i < counts.size(); ++i) {
        counts[i] = static_cast<int>(members_[i].size());
    }
    // The rows view needs five rows a hit tall: a short body shows the category view whatever was chosen.
    const BadgeWallView view = wall_rows_fit(body, k.u) ? app.badge_wall_view : BadgeWallView::Category;
    const WallLayout lay =
        plan_wall(body, k.u, view, counts, category_, static_cast<int>(near_.size()), lines_under(k, body, counts));
    if (ladder_ >= 0) { // the ladder has the body to itself: no words of the wall under its card
        draw_ladder(k, dl, body, set);
        return;
    }
    if (view == BadgeWallView::Category) {
        draw_tabs(k, dl, lay);
        draw_category(k, dl, lay, set);
        draw_strip(k, dl, lay);
    } else {
        draw_rows(k, dl, lay, set);
    }
}

} // namespace etude::app::ui
