// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_bar.hpp"

#include "bar.hpp" // begin_layer
#include "card_kit.hpp"
#include "icons.hpp"

#include "etude/grade_palette.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cstdio>

namespace etude::app::ui {

namespace {

ImVec2 centre_of(const RectF& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }
ImVec2 size_of(const RectF& r) { return {r.w, r.h}; }

// "2:05" from a count of milliseconds.
void clock_text(std::int32_t ms, char* out, std::size_t size) {
    const int total = std::max(0, ms) / 1000;
    std::snprintf(out, size, "%d:%02d", total / 60, total % 60);
}

// The run bar by bar: a segment for every bar that was played, its colour how that bar went (the
// accuracy's grade colour; dim when nothing in it was graded).
void run_track(Kit& k, ImDrawList* dl, const ReplayInfo& info, const RectF& track) {
    const float face_h = std::min(track.h, k.s(tok::kS3));
    const float y0 = track.y + (track.h - face_h) * 0.5f;
    const float y1 = y0 + face_h;
    const float radius = face_h * 0.5f;
    dl->AddRectFilled({track.x, y0}, {track.right(), y1}, u32(col::kFill), radius);
    if (info.duration_ms <= 0 || info.summary == nullptr) {
        return;
    }
    const double span = static_cast<double>(info.duration_ms) * 0.001;
    const auto x_at = [&](double seconds) {
        return track.x + track.w * static_cast<float>(std::clamp(seconds / span, 0.0, 1.0));
    };
    const std::vector<RunBar>& played = info.summary->played_bars;
    std::size_t next = 0;
    for (std::size_t m = 0; m < info.measures.size() && next < played.size(); ++m) {
        const Measure& measure = info.measures[m];
        if (measure.number != played[next].measure) {
            continue;
        }
        const RunBar& bar = played[next++];
        const double end_s = m + 1 < info.measures.size() ? info.measures[m + 1].start_s
                                                          : info.song_duration_s;
        const float a = x_at(measure.start_s);
        const float b = x_at(end_s);
        if (b - a < 0.5f) {
            continue;
        }
        const int pct = bar_accuracy_pct(bar.graded, bar.missed, bar.wrong);
        Grade tier = Grade::Perfect;
        ImVec4 c = col::kText3;
        if (pct >= 0 && accuracy_grade(pct, &tier)) {
            c = grade_ink(k, tier).top;
        }
        const float gap = b - a > 3.0f ? k.s(tok::kHairline) : 0.0f;
        dl->AddRectFilled({a + gap, y0}, {b - gap, y1}, u32(with_alpha(c, 0.75f)));
    }
}

// The top row: the strip's own look, translucent with a hairline under it, then five icon controls
// and the speed.
void top_row(Kit& k, const ReplayInfo& info, const ReplayBarLayout& l, const RectF& row,
             float display_w, ReplayChoice* choice) {
    if (!begin_layer("##replay-top", {0.0f, row.y}, {display_w, row.h + k.s(tok::kS1)})) {
        ImGui::End();
        return;
    }
    const float drawn = k.s(tok::kFace);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor({0.0f, row.y}, {display_w, row.bottom()},
                                u32(with_alpha(col::kSurface, 0.96f)),
                                u32(with_alpha(col::kSurface, 0.96f)),
                                u32(with_alpha(col::kSurface, 0.86f)),
                                u32(with_alpha(col::kSurface, 0.86f)));
    dl->AddLine({0.0f, row.bottom() - 0.5f}, {display_w, row.bottom() - 0.5f}, u32(col::kHairline));
    const auto cell = [&](std::size_t i, const char* id, Icon icon, const char* label, bool on,
                          ReplayAction action) {
        if (icon_toggle(k, id, icon, label, centre_of(l.cells[i]), size_of(l.cells[i]), drawn, on,
                        true)) {
            choice->action = action;
        }
    };
    cell(0, "##replay-restart", icon::kPrevious, "Restart", false, ReplayAction::Restart);
    cell(1, "##replay-back", icon::kSkipBack, "Back 5 seconds", false, ReplayAction::SeekBack);
    cell(2, "##replay-play", info.playing ? icon::kPause : icon::kPlay,
         info.playing ? "Pause" : "Play", false, ReplayAction::PlayPause);
    cell(3, "##replay-forward", icon::kSkipForward, "Forward 5 seconds", false,
         ReplayAction::SeekForward);
    cell(4, "##replay-sound", info.sound_on ? icon::kSoundBoth : icon::kMute,
         info.sound_on ? "My sound on" : "My sound off", info.sound_on, ReplayAction::Sound);
    ChipOpts opts;
    opts.width = l.speed.w;
    if (chip(k, "##replay-speed", format_unit(info.speed_percent, Unit::Percent).c_str(),
             {l.speed.x, l.speed.y}, opts)) {
        choice->action = ReplayAction::Speed;
    }
    ImGui::End();
}

// The run bar: tap or drag on it seeks (a surface of its own, drawn over the bar's colours), the
// playhead on top. Returns true while it is held.
bool run_bar(Kit& k, ImDrawList* dl, const ReplayInfo& info, const ReplayBarLayout& l,
             ReplayChoice* choice) {
    run_track(k, dl, info, l.track);
    press_area(k, "##scrub", {l.track.x, l.track.y, l.track.right(), l.track.bottom()});
    bool held = false;
    if (ImGui::IsItemActive() && l.track.w > 0.0f) {
        const float x = ImGui::GetIO().MousePos.x;
        choice->action = ReplayAction::Seek;
        choice->fraction = std::clamp(static_cast<double>((x - l.track.x) / l.track.w), 0.0, 1.0);
        held = true;
    }
    if (info.duration_ms > 0) {
        const double at = std::clamp(
            static_cast<double>(info.position_ms) / static_cast<double>(info.duration_ms), 0.0, 1.0);
        const float px = l.track.x + l.track.w * static_cast<float>(at);
        const float half = std::min(l.track.h, k.s(tok::kFace)) * 0.5f;
        const float cy = l.track.y + l.track.h * 0.5f;
        dl->AddLine({px, cy - half}, {px, cy + half}, u32(col::kText), k.s(tok::kS1));
    }
    return held;
}

} // namespace

ReplayChoice ReplayBarView::draw(Kit& k, const ReplayInfo& info, const RectF& row,
                                 const RectF& bar, const RectF& band, float display_w) {
    ReplayChoice choice;
    const ReplayBarLayout l = replay_bar_layout(bar, band, k.u);
    top_row(k, info, l, row, display_w, &choice);

    // The band row: the run bar, the time, the four tools.
    if (begin_layer("##replay-band", {0.0f, band.y}, {display_w, band.h + k.s(tok::kS1)})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({0.0f, band.y}, {display_w, band.bottom()},
                          u32(with_alpha(col::kSurface, 0.96f)));
        dl->AddLine({0.0f, band.bottom() - 0.5f}, {display_w, band.bottom() - 0.5f},
                    u32(col::kHairline));
        if (l.fits) {
            run_bar(k, dl, info, l, &choice);
        }
        char now[16];
        char total[16];
        clock_text(info.position_ms, now, sizeof(now));
        clock_text(info.duration_ms, total, sizeof(total));
        char both[40];
        std::snprintf(both, sizeof(both), "%s / %s", now, total);
        label_at(k, dl, k.regular, k.s(tok::kCaption),
                 {l.time.x, l.time.y, l.time.right(), l.time.bottom()}, LabelAlign::Centre,
                 col::kText2, both);
        const float drawn = k.s(tok::kFace);
        const auto tool = [&](std::size_t i, const char* id, Icon icon, const char* label,
                              bool enabled, ReplayAction action) {
            ImGui::BeginDisabled(!enabled);
            if (icon_toggle(k, id, icon, label, centre_of(l.tools[i]), size_of(l.tools[i]), drawn,
                            false, true) &&
                enabled) {
                choice.action = action;
            }
            ImGui::EndDisabled();
        };
        tool(0, "##replay-prev-mistake", icon::kBack, "Previous mistake", info.prev_mistake,
             ReplayAction::PrevMistake);
        tool(1, "##replay-next-mistake", icon::kChevron, "Next mistake", info.next_mistake,
             ReplayAction::NextMistake);
        tool(2, "##replay-summary", icon::kInfo, "How it went", info.summary_offered,
             ReplayAction::Summary);
        if (pin_chip(k, "##replay-pin", info.pin, centre_of(l.tools[3]), size_of(l.tools[3]))) {
            choice.action = ReplayAction::Pin;
        }
        tool(4, "##replay-practise", icon::kPractice, "Practise the worst bars",
             info.practise_offered, ReplayAction::Practise);
    }
    ImGui::End();
    return choice;
}

} // namespace etude::app::ui
