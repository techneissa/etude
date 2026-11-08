// SPDX-License-Identifier: GPL-3.0-or-later
#include "progress_page.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "profile_ui.hpp"
#include "progress_ask.hpp"
#include "medal_draw.hpp"
#include "medal_emblem.hpp"
#include "progress_charts.hpp"
#include "tap.hpp"

#include "etude/progress_text.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

namespace pg = progress_tok;

// A flame and its number, for a streak, centred on `centre`'s line.
void streak_words(char* out, std::size_t size, const etude::Streak& s) {
    if (s.current <= 0) {
        std::snprintf(out, size, "No streak yet");
    } else if (s.longest > s.current) {
        std::snprintf(out, size, "%d-day streak \xC2\xB7 best %d", s.current, s.longest);
    } else {
        std::snprintf(out, size, "%d-day streak", s.current);
    }
}

// The colour a bar ends in, by how full it is: a cool blue-teal when it is low, teal in the middle,
// a warm gold near the top, so the progress reads at a glance.
ImVec4 bar_end_colour(float fraction) {
    constexpr ImVec4 kCool{0.20f, 0.58f, 0.86f, 1.0f};
    constexpr ImVec4 kWarm{1.0f, 0.74f, 0.30f, 1.0f};
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    return f < 0.5f ? mix(kCool, progress_col::kTeal, f * 2.0f)
                    : mix(progress_col::kTeal, kWarm, (f - 0.5f) * 2.0f);
}

// A bar's fill, from its left end to `fraction` of the track, a gradient from the cool colour to the
// colour that fullness gives; the ends are round.
void gradient_bar(ImDrawList* dl, const Box& track, float fraction) {
    const float r = track.h() * 0.5f;
    const float x1 = track.x0 + std::max(track.h(), track.w() * fraction);
    const ImVec4 left = bar_end_colour(0.0f);
    const ImVec4 right = bar_end_colour(fraction);
    dl->AddCircleFilled({track.x0 + r, track.cy()}, r, u32(left));
    dl->AddCircleFilled({x1 - r, track.cy()}, r, u32(right));
    if (x1 - r > track.x0 + r) {
        dl->AddRectFilledMultiColor({track.x0 + r, track.y0}, {x1 - r, track.y1}, u32(left), u32(right),
                                    u32(right), u32(left));
    }
}

// This week's minutes as a bar against the household's most, between `left` and `right` in the
// row: with its words over it, or (a slim row) the words to the right of it.
void house_minutes_bar(Kit& k, ImDrawList* dl, const Box& row, float left, float right, int minutes,
                       int most_minutes, bool slim) {
    const std::string words = etude::minutes_words(minutes);
    const float bar_h = k.s(pg::kHouseBarH);
    const float size = k.s(tok::kBody);
    const float fraction =
        most_minutes > 0 ? static_cast<float>(minutes) / static_cast<float>(most_minutes) : 0.0f;
    Box track{left, row.cy() - bar_h * 0.5f, right, row.cy() + bar_h * 0.5f};
    if (slim) {
        const float label_w = k.s(pg::kHouseMinutesW);
        label_at(k, dl, k.regular, size, {right - label_w, row.y0, right, row.y1}, LabelAlign::Right,
                 col::kText, words.c_str());
        track.x1 = right - label_w - k.s(tok::kS2);
    } else {
        // The words over the bar, the two close round the middle.
        const float line = size * pg::kLineRoom;
        char week[48];
        std::snprintf(week, sizeof week, "%s this week", words.c_str());
        const FitText text = fit(k.regular, size, week, right - left);
        label_at(k, dl, k.regular, size, {left, track.y0 - line, right, track.y0}, LabelAlign::Left,
                 col::kText, text.c_str());
    }
    dl->AddRectFilled({track.x0, track.y0}, {track.x1, track.y1}, u32(with_alpha(col::kText, 0.08f)),
                      bar_h * 0.5f);
    if (fraction > 0.0f && track.w() > 0.0f) {
        gradient_bar(dl, track, fraction);
    }
}

// The points in a circle of the person's own colour, the number as large as the circle holds.
void points_badge(Kit& k, ImDrawList* dl, ImVec2 centre, float d, std::uint32_t colour, int points) {
    const ImVec4 own = profile_rgb(colour);
    dl->AddCircleFilled(centre, d * 0.5f, u32(with_alpha(own, 0.22f)));
    dl->AddCircle(centre, d * 0.5f - k.s(tok::kFocusRing) * 0.5f, u32(own), 0, k.s(tok::kFocusRing));
    const std::string words = etude::thousands(points);
    float size = std::min(k.s(tok::kTitle), d * pg::kBadgeTextShare);
    const float limit = d * pg::kBadgeInkShare;
    const float wide = text_size(k.bold, size, words.c_str()).x;
    if (wide > limit) {
        size *= limit / wide;
    }
    label_at(k, dl, k.bold, size, {centre.x - d * 0.5f, centre.y - d * 0.5f, centre.x + d * 0.5f, centre.y + d * 0.5f},
             LabelAlign::Centre, col::kText, words.c_str());
}

} // namespace

void ProgressPage::open(bool household) {
    open_ = true;
    tab_ = household ? ProgressTab::Household : ProgressTab::Me;
    wall_.close_ladder();
    asked_.reopen();
}

void ProgressPage::open_medals(int ladder_of) {
    open(false);
    tab_ = ProgressTab::Medals;
    if (ladder_of >= 0) {
        wall_.open_ladder(ladder_of);
    }
}

ProgressPage::Ask ProgressPage::take_ask() {
    const Ask out = ask_;
    ask_ = {};
    return out;
}

void ProgressPage::draw_header(Kit& k, ImDrawList* dl, const ProgressLayout& lay, const char* title,
                               etude::AppSettings& app, bool* back) {
    ImGui::SetCursorScreenPos({lay.back.x0, lay.back.y0});
    if (icon_action(k, "##progress-back", icon::kBack, "Back", lay.back.w(), true)) {
        *back = true;
    }
    const FitText name = fit(k.bold, k.s(tok::kTitle), title, lay.title.w());
    label_at(k, dl, k.bold, k.s(tok::kTitle), lay.title, LabelAlign::Left, col::kText, name.c_str());
    // The wall's view switch shows the view it switches to.
    if (lay.toggle.h() > 0.0f) {
        ImGui::SetCursorScreenPos({lay.toggle.x0, lay.toggle.y0});
        const bool to_rows = app.badge_wall_view == BadgeWallView::Category;
        if (icon_action(k, "##wall-toggle", lucide_glyph(to_rows ? "rows-3" : "layout-grid"),
                        to_rows ? "All categories on one screen" : "One category at a time", lay.toggle.w(),
                        !wall_.ladder_open())) {
            app.badge_wall_view = to_rows ? BadgeWallView::Rows : BadgeWallView::Category;
        }
    }
    // The three views sit in one track with one raised pill on the chosen view (the kit's tabs).
    const std::array<Box, 3> cells{lay.mode_me, lay.mode_household, lay.mode_medals};
    constexpr std::array<const char*, 3> kIds{"##progress-mode-me", "##progress-mode-household",
                                              "##progress-mode-medals"};
    constexpr std::array<const char*, 3> kWords{"Progress", "Household", "Medals"};
    constexpr std::array<ProgressTab, 3> kViews{ProgressTab::Me, ProgressTab::Household, ProgressTab::Medals};
    int chosen = 0;
    for (int i = 0; i < 3; ++i) {
        chosen = tab_ == kViews[static_cast<std::size_t>(i)] ? i : chosen;
    }
    const int bold = tab_track(k, dl, "##progress-modes", cells, chosen);
    for (int i = 0; i < 3; ++i) {
        const std::size_t n = static_cast<std::size_t>(i);
        const bool on = i == chosen;
        const TabHit hit = tab_cell(k, dl, kIds[n], cells[n], on);
        fade_label(k, dl, k.s(tok::kLabel), hit.pill, LabelAlign::Centre, kWords[n], i == bold,
                   hit.weight, state_ink(hit.state, col::kText2), state_ink(hit.state, col::kText));
        if (hit.tapped) {
            tab_ = kViews[n];
            if (tab_ == ProgressTab::Medals) {
                wall_.close_ladder();
            }
        }
    }
}

bool ProgressPage::draw(Kit& k, LibraryPort& port, const Box& area, etude::AppSettings& app) {
    const int goal_minutes = app.daily_goal_minutes;
    const std::int64_t active = port.active_profile();
    // The numbers are asked for again when the page opens, the profile in use or the goal changes,
    // the household gains or loses a profile, and when the local day turns over (the calendar, the
    // streak and the week move on).
    const std::size_t people = port.profiles().size();
    if (asked_.due(active, goal_minutes, port.today(), people)) {
        port.request_progress(goal_minutes);
        asked_.note(active, goal_minutes, port.today(), people);
    }
    const std::shared_ptr<const etude::ProgressView> held = port.progress();
    const bool loaded = held != nullptr && held->profile_id == active;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ProgressContent content;
    content.view = tab_;
    if (loaded) {
        content.best_rows = static_cast<int>(held->bests.size());
        content.spots = static_cast<int>(held->practise.size());
        content.can_continue = held->last_song_hash != 0;
        content.profiles = static_cast<int>(held->household.size());
    }
    const ProgressLayout lay = plan_progress(area, k.u, content);
    const etude::Profile* me = port.profiles().find(active);
    std::string title = tab_ == ProgressTab::Household ? "Household"
                        : tab_ == ProgressTab::Medals  ? "Medals"
                        : me != nullptr                ? me->name + "'s progress"
                                                       : "Progress";
    bool back = false;
    draw_header(k, dl, lay, title.c_str(), app, &back);
    if (progress_status(loaded, port.progress_failed()) != ProgressStatus::Ready) {
        const bool failed = port.progress_failed();
        label_at(k, dl, k.regular, k.s(tok::kBody),
                 {area.x0, area.y0 + k.s(tok::kHit), area.x1, area.y1}, LabelAlign::Centre,
                 failed ? col::kWarning : col::kText3,
                 failed ? "Your progress could not be read (the history is not available)"
                        : "Working out your progress...");
        return back;
    }
    if (tab_ == ProgressTab::Household) {
        draw_household(k, dl, port, lay, *held);
    } else if (tab_ == ProgressTab::Medals) {
        wall_.draw(k, dl, lay.wall, app, held->me.medals);
    } else {
        draw_me(k, dl, port, lay, *held);
    }
    return back;
}

// The profile's card: avatar, name, streak, and the pencil that opens its edit sheet.
void ProgressPage::draw_profile_card(Kit& k, ImDrawList* dl, const ProgressLayout& lay,
                                     const Profile& who, const etude::ProgressView& view) {
    progress_card(k, dl, lay.profile, nullptr);
    const float pad = k.s(pg::kPad);
    const float d = std::min(k.s(pg::kAvatar), lay.profile.h() - 2.0f * pad * 0.5f);
    const ImVec2 c{lay.profile.x0 + pad + d * 0.5f, lay.profile.cy()};
    profile_avatar(k, dl, c, d, who);
    const Box text{c.x + d * 0.5f + k.s(tok::kS3), lay.profile.y0, lay.edit.x0, lay.profile.y1};
    const float mid = lay.profile.cy();
    const FitText name = fit(k.bold, k.s(tok::kTitle), who.name, text.w());
    label_at(k, dl, k.bold, k.s(tok::kTitle), {text.x0, lay.profile.y0, text.x1, mid}, LabelAlign::Left,
             col::kText, name.c_str());
    // The streak under the name, a flame before it.
    const float flame = k.s(tok::kIconSm);
    const Box line{text.x0 + flame + k.s(tok::kS1), mid, text.x1, lay.profile.y1};
    char streak[64];
    streak_words(streak, sizeof streak, view.me.streak);
    // The banked freezes after the words, when there is a streak and the room.
    const bool freezes = view.me.streak.current > 0;
    const float freeze_w = freezes ? draw_freezes(k, dl, {0.0f, 0.0f}, k.s(tok::kCaption), 0, true) + k.s(tok::kS2) : 0.0f;
    const FitText tail = fit(k.regular, k.s(tok::kCaption), streak, line.w() - freeze_w);
    glyph(k, dl, icon::kFlame, {text.x0 + flame * 0.5f, line.cy()}, flame,
          view.me.streak.current > 0 ? progress_col::kFlame : col::kText3);
    label_at(k, dl, k.regular, k.s(tok::kCaption), line, LabelAlign::Left, col::kText2, tail.c_str());
    if (freezes) {
        const float x = line.x0 + text_size(k.regular, k.s(tok::kCaption), tail.c_str()).x + k.s(tok::kS2);
        draw_freezes(k, dl, {x, line.cy()}, k.s(tok::kCaption), view.me.streak.freezes);
    }
    ImGui::SetCursorScreenPos({lay.edit.x0, lay.edit.y0});
    if (icon_action(k, "##progress-edit", icon::kRename, "Edit profile", lay.edit.w(), true)) {
        ask_ = {Ask::Kind::Edit, who.id};
    }
}

void ProgressPage::draw_me(Kit& k, ImDrawList* dl, LibraryPort& port, const ProgressLayout& lay,
                           const etude::ProgressView& view) {
    if (lay.calendar.h() > 0.0f) {
        draw_calendar(k, dl, lay.calendar, view);
    }
    if (lay.graph.h() > 0.0f) {
        draw_graph(k, dl, lay.graph, view);
    }
    if (lay.best.h() > 0.0f) {
        draw_best(k, dl, lay.best,
                  std::span<const Box>(lay.best_row.data(), static_cast<std::size_t>(lay.best_rows)), view);
    }
    if (const etude::Profile* who = port.profiles().find(view.profile_id); who != nullptr) {
        if (lay.profile.h() > 0.0f) {
            draw_profile_card(k, dl, lay, *who, view);
        }
    }
    if (lay.ring.h() > 0.0f) {
        draw_ring(k, dl, lay.ring, view);
    }
    if (lay.tiles.h() > 0.0f) {
        const etude::ProfileProgress& me = view.me;
        char bold[64];
        char caption[64];
        if (lay.wide) {
            std::snprintf(bold, sizeof bold, "%d song%s", me.songs, me.songs == 1 ? "" : "s");
            std::snprintf(caption, sizeof caption, "%d run%s", me.runs, me.runs == 1 ? "" : "s");
            draw_tile(k, dl, lay.tile_songs, icon::kNote, bold, caption);
        } else {
            const std::string week = etude::minutes_words(me.minutes_week);
            draw_tile(k, dl, lay.tile_songs, icon::kClock, week.c_str(), "this week");
            std::snprintf(bold, sizeof bold, "%d song%s", me.songs, me.songs == 1 ? "" : "s");
            std::snprintf(caption, sizeof caption, "%d run%s", me.runs, me.runs == 1 ? "" : "s");
            draw_tile(k, dl, lay.tile_rank, icon::kNote, bold, caption);
        }
        if (lay.wide) {
            if (view.household_size > 1 && me.rank > 0) {
                const std::string rank = etude::ordinal_words(me.rank);
                draw_tile(k, dl, lay.tile_rank, icon::kFlash, rank.c_str(), "in the household");
            } else {
                const std::string points = etude::thousands(me.points);
                draw_tile(k, dl, lay.tile_rank, icon::kFlash, points.c_str(), "points");
            }
        }
    }
    if (lay.medals.h() > 0.0f) {
        draw_medals_card(k, dl, lay.medals, view.me);
    }
    if (lay.practise.h() > 0.0f) {
        draw_practise(k, dl, port, lay, view);
    }
}

// The medals card: the ring with the medals held, the overall title and what it takes to reach the
// next one. The whole card opens the wall.
void ProgressPage::draw_medals_card(Kit& k, ImDrawList* dl, const Box& box, const etude::ProfileProgress& me) {
    progress_card(k, dl, box, nullptr);
    const bool pressed = press_area(k, "##progress-medals", box);
    const State st = item_state(k, false);
    draw_state(k, dl, "##progress-medals", box, box, st, Idle::None);
    audit::note("##progress-medals", {box.x0, box.y0}, {box.x1, box.y1}, InkKind::Target);
    const float pad = k.s(pg::kPad);
    const float d = std::min(k.s(pg::kMedalsRing), box.h() - 2.0f * pad * 0.5f);
    const ImVec2 c{box.x0 + pad + d * 0.5f, box.cy()};
    char held[16];
    std::snprintf(held, sizeof held, "%d", me.medals.points());
    draw_medal_ring(k, dl, c, d, me.medals, held);
    const Box text{c.x + d * 0.5f + k.s(tok::kS3), box.y0, box.x1 - pad - k.s(tok::kIconSm), box.y1};
    const int title = me.medals.title();
    const float mid = box.cy();
    // The title's medal, then the title.
    const float medal = std::min(k.s(pg::kTitleMedal), (mid - box.y0) * 1.25f);
    draw_title_medal_small(k, dl, title, {text.x0 + medal * 0.5f, (box.y0 + mid) * 0.5f}, medal);
    const Box words{text.x0 + medal + k.s(tok::kS2), box.y0, text.x1, mid};
    const FitText name = fit(k.bold, k.s(tok::kTitle), etude::tier_title(title), words.w());
    label_at(k, dl, k.bold, k.s(tok::kTitle), words, LabelAlign::Left, state_ink(st, col::kText), name.c_str());
    char line[80];
    if (title < etude::kTierCount) {
        std::snprintf(line, sizeof line, "%d of %d medals \xC2\xB7 %s at %d medals", me.medals.points(),
                      etude::kMaxMedalPoints, etude::tier_title(title + 1).data(),
                      etude::title_points(title + 1));
    } else {
        std::snprintf(line, sizeof line, "%d of %d medals", me.medals.points(), etude::kMaxMedalPoints);
    }
    const FitText cut = fit(k.regular, k.s(tok::kCaption), line, text.w());
    label_at(k, dl, k.regular, k.s(tok::kCaption), {text.x0, mid, text.x1, box.y1}, LabelAlign::Left,
             col::kText2, cut.c_str());
    glyph(k, dl, icon::kChevron, {box.x1 - pad - k.s(tok::kIconSm) * 0.5f, box.cy()}, k.s(tok::kIconSm),
          state_ink(st, col::kText3));
    if (tapped(pressed)) {
        tab_ = ProgressTab::Medals;
        wall_.close_ladder();
    }
}

// "Practise today": the bars the latest runs stumbled on, a play button each, and Continue.
void ProgressPage::draw_practise(Kit& k, ImDrawList* dl, LibraryPort& port, const ProgressLayout& lay,
                                 const etude::ProgressView& view) {
    progress_card(k, dl, lay.practise, "Practise today");
    const float pad = k.s(pg::kPad);
    if (view.practise.empty()) {
        label_at(k, dl, k.regular, k.s(tok::kBody),
                 {lay.practise.x0 + pad, lay.practise.y0 + pad + k.s(pg::kTitleH),
                  lay.practise.x1 - pad, lay.practise.y0 + pad + k.s(pg::kTitleH) + k.s(pg::kSpotH)},
                 LabelAlign::Left, col::kText3, "Nothing to practise yet");
    }
    for (int i = 0; i < lay.spots; ++i) {
        const etude::PractiseSpot& spot = view.practise[static_cast<std::size_t>(i)];
        const Box row = lay.spot[static_cast<std::size_t>(i)];
        char id[40];
        std::snprintf(id, sizeof id, "##progress-spot-%d", i);
        const bool pressed = press_area(k, id, row);
        const State st = item_state(k, false);
        draw_state(k, dl, id, row, row, st, Idle::Fill);
        audit::note(id, {row.x0, row.y0}, {row.x1, row.y1}, InkKind::Target);
        const float d = std::min(k.s(pg::kIconBox), row.h() - k.s(tok::kS2));
        const ImVec2 c{row.x0 + k.s(tok::kS2) + d * 0.5f, row.cy()};
        dl->AddRectFilled({c.x - d * 0.5f, c.y - d * 0.5f}, {c.x + d * 0.5f, c.y + d * 0.5f},
                          u32(with_alpha(col::kAccent, 0.16f)), k.s(tok::kRadiusControl));
        glyph(k, dl, icon::kRepeat, c, k.s(tok::kIcon), state_ink(st, col::kAccent));
        const float play = k.s(tok::kIcon);
        glyph(k, dl, icon::kPlay, {row.x1 - k.s(tok::kS3) - play * 0.5f, row.cy()}, play,
              state_ink(st, col::kAccent));
        const Box text{c.x + d * 0.5f + k.s(tok::kS2), row.y0, row.x1 - k.s(tok::kS3) - play - k.s(tok::kS2),
                       row.y1};
        const FitText name = fit(k.bold, k.s(tok::kBody), spot.title, text.w());
        const std::string bars = etude::bars_words(spot.first_measure, spot.last_measure);
        const StackLine top{k.bold, k.s(tok::kBody), state_ink(st, col::kText), name.c_str()};
        const StackLine bottom{k.regular, k.s(tok::kCaption), col::kText2, bars.c_str()};
        label_stack(k, dl, text, top, &bottom);
        if (tapped(pressed)) {
            port.practise_bars(spot.song_hash, spot.first_measure, spot.last_measure);
        }
    }
    if (lay.continue_row.h() > 0.0f) {
        const Box row = lay.continue_row;
        const bool pressed = press_area(k, "##progress-continue", row);
        const State st = item_state(k, false);
        draw_state(k, dl, "##progress-continue", row, row, st, Idle::Fill);
        audit::note("##progress-continue", {row.x0, row.y0}, {row.x1, row.y1}, InkKind::Target);
        const float icon_size = k.s(tok::kIcon);
        FitText words;
        words.format("Continue %s", view.last_song_title.c_str());
        const Box text{row.x0 + k.s(tok::kS3) + icon_size + k.s(tok::kS2), row.y0, row.x1 - k.s(tok::kS3),
                       row.y1};
        const FitText cut = fit(k.regular, k.s(tok::kBody), words.view(), text.w());
        glyph(k, dl, icon::kReplay, {row.x0 + k.s(tok::kS3) + icon_size * 0.5f, row.cy()}, icon_size,
              state_ink(st, col::kText2));
        label_at(k, dl, k.regular, k.s(tok::kBody), text, LabelAlign::Left, state_ink(st, col::kText),
                 cut.c_str());
        if (tapped(pressed)) {
            port.open_song_hash(view.last_song_hash);
        }
    }
}

// One ranked row of the household: the rank, who they are and their streak, this week's minutes as a
// bar against the household's most, the medal ring and the points.
void ProgressPage::draw_house_row(Kit& k, ImDrawList* dl, const Box& row, const Profile& who,
                                  const etude::ProfileProgress& p, bool mine, int most_minutes,
                                  const ProgressLayout& lay) {
    char id[64];
    std::snprintf(id, sizeof id, "##progress-house-%lld", static_cast<long long>(p.profile_id));
    const bool pressed = press_area(k, id, row);
    const State st = item_state(k, mine);
    draw_state(k, dl, id, row, row, st, Idle::Fill);
    audit::note(id, {row.x0, row.y0}, {row.x1, row.y1}, InkKind::Target);
    const float pad = k.s(pg::kPad);
    const float gap = k.s(tok::kS2);
    // Rank.
    const Box rank{row.x0 + pad, row.y0, row.x0 + pad + k.s(pg::kHouseRankW), row.y1};
    char rank_text[16];
    if (p.rank > 0) {
        std::snprintf(rank_text, sizeof rank_text, "%d", p.rank);
    } else {
        std::snprintf(rank_text, sizeof rank_text, "-");
    }
    label_at(k, dl, k.bold, k.s(tok::kTitle), rank, LabelAlign::Left,
             p.rank == 1 ? progress_col::kGold : state_ink(st, col::kText2), rank_text);
    // Avatar, name and streak.
    const float d = std::min(k.s(pg::kHouseAvatar), row.h() - 2.0f * k.s(tok::kS1));
    const ImVec2 c{rank.x1 + d * 0.5f, row.cy()};
    profile_avatar(k, dl, c, d, who);
    const float text_w = std::min(k.s(pg::kHouseTextW), (row.w() - 2.0f * pad) * 0.38f);
    const Box text{c.x + d * 0.5f + gap, row.y0, c.x + d * 0.5f + gap + text_w, row.y1};
    char who_name[128];
    std::snprintf(who_name, sizeof who_name, "%s%s", who.name.c_str(), mine ? " (you)" : "");
    const int title = p.medals.title();
    char streak[64];
    streak_words(streak, sizeof streak, p.streak);
    if (lay.house_slim) {
        name_title_medal(k, dl, text, k.s(pg::kHouseName), who_name, title, state_ink(st, col::kText));
    } else {
        const bool freezes = p.streak.current > 0;
        const float freeze_w = freezes ? draw_freezes(k, dl, {0.0f, 0.0f}, k.s(pg::kHouseStreak), 0, true) + k.s(tok::kS2) : 0.0f;
        const FitText tail = fit(k.regular, k.s(pg::kHouseStreak), streak, text.w() - k.s(tok::kIconSm) - k.s(tok::kS1) - freeze_w);
        // The two lines sit close round the row's middle.
        const float mid = row.cy();
        const float name_line = k.s(pg::kHouseName) * pg::kHouseNameLine;
        const float streak_line = k.s(pg::kHouseStreak) * pg::kHouseStreakLine;
        name_title_medal(k, dl, {text.x0, mid - name_line, text.x1, mid}, k.s(pg::kHouseName), who_name, title,
                         state_ink(st, col::kText));
        const float flame = k.s(tok::kIconSm);
        glyph(k, dl, icon::kFlame, {text.x0 + flame * 0.5f, mid + streak_line * 0.5f}, flame,
              p.streak.current > 0 ? progress_col::kFlame : col::kText3);
        label_at(k, dl, k.regular, k.s(pg::kHouseStreak),
                 {text.x0 + flame + k.s(tok::kS1), mid, text.x1, mid + streak_line}, LabelAlign::Left,
                 col::kText2, tail.c_str());
        if (freezes) {
            const float fx = text.x0 + flame + k.s(tok::kS1) + text_size(k.regular, k.s(pg::kHouseStreak), tail.c_str()).x + k.s(tok::kS2);
            draw_freezes(k, dl, {fx, mid + streak_line * 0.5f}, k.s(pg::kHouseStreak), p.streak.freezes);
        }
    }
    // The points in a circle before the bar, then the bar and, at the right end, the medal ring.
    float at = text.x1 + gap;
    const float pd = std::min(k.s(pg::kHousePointsD), row.h() - 2.0f * k.s(tok::kS1));
    float end = row.x1 - pad;
    if (at + pd <= end) {
        points_badge(k, dl, {at + pd * 0.5f, row.cy()}, pd, who.colour, p.points);
        at += pd + gap;
    }
    const float least = k.s(pg::kBarMinW);
    const float ring_d = std::min(k.s(pg::kHouseRing), row.h() - 2.0f * k.s(tok::kS1));
    if (row.w() >= k.s(pg::kHouseRingAt) && end - at - ring_d - gap >= least) {
        char held[16];
        std::snprintf(held, sizeof held, "%d", p.medals.points());
        draw_medal_ring(k, dl, {end - ring_d * 0.5f, row.cy()}, ring_d, p.medals, held);
        end -= ring_d + gap;
    }
    if (end - at >= least) {
        house_minutes_bar(k, dl, row, at, end, p.minutes_week, most_minutes, lay.house_slim);
    }
    if (tapped(pressed)) {
        ask_ = {Ask::Kind::Switch, p.profile_id};
        tab_ = ProgressTab::Me;
    }
}

// The household, ranked: the best on the household's board first, those with no scored run after,
// the person in use highlighted wherever they land.
void ProgressPage::draw_household(Kit& k, ImDrawList* dl, LibraryPort& port, const ProgressLayout& lay,
                                  const etude::ProgressView& view) {
    const std::int64_t active = port.active_profile();
    // The view carries the order; one made by hand (a demo) without it is ordered here.
    const std::vector<std::size_t> own =
        view.household_ranked.size() == view.household.size() ? std::vector<std::size_t>{}
                                                              : etude::household_order(view.household);
    const std::vector<std::size_t>& order = own.empty() ? view.household_ranked : own;
    int most = 0;
    for (const etude::ProfileProgress& p : view.household) {
        most = std::max(most, p.minutes_week);
    }
    for (int i = 0; i < lay.houses && i < static_cast<int>(order.size()); ++i) {
        const etude::ProfileProgress& p = view.household[order[static_cast<std::size_t>(i)]];
        if (const etude::Profile* who = port.profiles().find(p.profile_id)) {
            draw_house_row(k, dl, lay.house[static_cast<std::size_t>(i)], *who, p, p.profile_id == active, most,
                           lay);
        }
    }
}

} // namespace etude::app::ui
