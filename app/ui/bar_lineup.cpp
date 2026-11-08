// SPDX-License-Identifier: GPL-3.0-or-later
#include "bar_lineup.hpp"

#include "icons.hpp"
#include "transport.hpp"

#include <array>

namespace etude::app::ui {

namespace {

using bar_metrics::Ctl;
using bar_metrics::Group;

// Each group's width, so a style can draw its card before the controls.
std::array<float, bar_metrics::kCtlCount> group_widths(const Kit& k,
                                                       const bar_metrics::Lineup& line,
                                                       const bar_metrics::StyleCells& cells,
                                                       BarForm form, const bar_metrics::Song& sv,
                                                       float per_group) {
    std::array<float, bar_metrics::kCtlCount> group_w{};
    int g = -1;
    Group last = Group::Song;
    for (const Ctl c : line) {
        if (g < 0 || bar_metrics::group_of(c) != last) {
            ++g;
            last = bar_metrics::group_of(c);
            group_w[static_cast<std::size_t>(g)] = per_group;
        }
        group_w[static_cast<std::size_t>(g)] += k.s(bar_metrics::cell_of(c, cells, form, sv));
    }
    return group_w;
}

// Where a control sits: the middle and hit area of its cell, the corner of a chip, the caption.
struct Cell {
    ImVec2 centre;
    ImVec2 hit;
    ImVec2 chip_pos; // the top left of a chip: its hit area, centred in the row
    const char* caption = nullptr;
};

// One control, in its cell.
void draw_control(Kit& k, UiModel& model, SongPort& song, const Frame& f, const LineupLook& look,
                  Ctl c, const Cell& cell, TempoControls& tempo, AccompanimentMenu& accomp,
                  OpenRequest& request) {
    const ImVec2 centre = cell.centre;
    const ImVec2 hit = cell.hit;
    const char* const caption = cell.caption;
    switch (c) {
    case Ctl::Replay:
    case Ctl::Back:
    case Ctl::Play:
    case Ctl::Forward:
    case Ctl::Sound:
    case Ctl::LeftHand:
    case Ctl::RightHand:
        song_control(k, song, c, centre, hit, look.song_face, f.live, look.prominent_play,
                     caption);
        break;
    case Ctl::Accompaniment:
        accomp.button(k, song, tempo, centre, hit, look.song_face, f.live, caption);
        break;
    case Ctl::AccompanimentMenu:
        accomp.chevron(k, tempo, centre, hit, look.song_face, f.live, caption);
        break;
    case Ctl::Tempo: {
        const ChipLook chip = look.captions                      ? ChipLook::Captioned
                              : bar_chip_has_badge(f.form) ? ChipLook::Full
                                                                 : ChipLook::Narrow;
        tempo.chip(k, song, cell.chip_pos, hit, look.chip_face, chip, f.live);
        if (tempo.open()) {
            accomp.close(); // one card at a time
        }
        break;
    }
    case Ctl::Metronome: {
        bool long_press = false;
        metronome_button(k, model, centre, hit, look.switch_face, f.live, caption,
                         &long_press);
        if (long_press) {
            request.ask(Page::Practice);
        }
        break;
    }
    case Ctl::Mode: {
        const ModeLook mode = look.captions                       ? ModeLook::Captioned
                              : bar_metrics::mode_has_name(f.form) ? ModeLook::Chip
                                                                   : ModeLook::Icon;
        mode_chip(k, song, cell.chip_pos, hit, look.chip_face, mode, f.live);
        break;
    }
    case Ctl::Style:
    case Ctl::Colours: {
        // A shortcut to the page the setting lives on, never a second copy of its control.
        const bool style = c == Ctl::Style;
        if (icon_toggle(k, style ? "##style" : "##colour",
                        style ? icon::kStyle : icon::kPalette,
                        caption != nullptr ? nullptr : (style ? "Note style" : "Colours"),
                        centre, hit, look.switch_face, false, f.live, caption)) {
            request.ask(style ? Page::Notes : Page::Colours);
        }
        break;
    }
    case Ctl::Pin:
        pin_button(k, model, centre, hit, look.switch_face, f.live, caption);
        break;
    }
}

} // namespace

float draw_lineup(Kit& k, UiModel& model, SongPort& song, const Frame& f, const LineupLook& look,
                  float x, float row_top, float h, TempoControls& tempo,
                  AccompanimentMenu& accomp, OpenRequest& request,
                  LineupFrame& frame) {
    const SongView& v = song.view();
    const bar_metrics::Song sv{v.has_song, v.beat_dotted, v.has_accompaniment};
    const bar_metrics::Lineup line = bar_metrics::lineup(f.form, v.has_song, f.pin, v.has_accompaniment);
    const bar_metrics::StyleCells& cells = look.cells;
    const float cy = row_top + h * 0.5f;
    const float per_group = k.s(cells.per_group);
    const float between = k.s(cells.between);
    const std::array<float, bar_metrics::kCtlCount> group_w =
        group_widths(k, line, cells, f.form, sv, per_group);
    x += k.s(cells.ends) * 0.5f;
    int g = -1;
    Group last = Group::Song;
    for (const Ctl c : line) {
        const Group group = bar_metrics::group_of(c);
        if (g < 0 || group != last) {
            if (g >= 0) {
                x += per_group * 0.5f;
                frame.divider(x + between * 0.5f);
                x += between;
            }
            ++g;
            last = group;
            frame.group(group, x, x + group_w[static_cast<std::size_t>(g)]);
            x += per_group * 0.5f;
        }
        const float w = k.s(bar_metrics::cell_of(c, cells, f.form, sv));
        // The cell is the hit area and the dead gap to the next one; the hit area is a hit tall at
        // most, centred in the row, so the band under the bar keeps its gap too.
        const float gap = k.s(tok::kHitGap);
        const ImVec2 hit{w - gap, std::min(h, k.s(tok::kHit))};
        const ImVec2 centre{x + w * 0.5f, cy};
        const Cell cell{centre,
                        hit,
                        {x + gap * 0.5f, cy - hit.y * 0.5f},
                        look.captions ? bar_metrics::caption_of(c) : nullptr};
        draw_control(k, model, song, f, look, c, cell, tempo, accomp, request);
        x += w;
    }
    if (g >= 0) {
        x += per_group * 0.5f;
    }
    return x;
}

} // namespace etude::app::ui
