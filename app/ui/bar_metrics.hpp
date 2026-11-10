// SPDX-License-Identifier: GPL-3.0-or-later
// What each bar style is made of, in design points, with no ImGui in it: the controls of each form
// with their hit widths, and the gaps between them. The bars draw from these numbers and size
// themselves with them.
//
// Rules:
// - The three styles hold one set of controls, in one order (`lineup`), and give them up in one
//   order as the room shrinks (core fit_bar_form, BarForm): they differ only in look.
// - A control's cell is its hit width and the dead gap to the next cell (`tok::kHitGap`): its hit
//   width, the cell less that gap, is never below `tok::kHit` in any form. A bar that is short of
//   room chooses a narrower form, which has fewer controls, never smaller ones.
// - The look shortcuts open the settings page of their setting; they never flip it.
#pragma once

#include "tokens.hpp"

#include "etude/screen_layout.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui::bar_metrics {

// The status text keeps at least this much of the top row when the bar comes in, if it needs it
// (design points); a centred bar (the dock, the groups) keeps it a little more. Below `kMinStatus`
// of status text the bar leaves its full form, and it gives up controls (BarForm) rather than cut
// the status text below it.
inline constexpr float kKeepStatus = 180.0f;
inline constexpr float kKeepStatusCentred = 220.0f;
inline constexpr float kMinStatus = 100.0f;
// The status text's own floor (app/ui/status_fit.hpp): the keyboard's name or the song's title is
// cut with an ellipsis only down to this much, and dropped whole below it, so the row never shows
// a fragment such as "No ke…".
inline constexpr float kStatusMinCut = 80.0f;

// The tempo chip: the note, the number's slot (three digits wide, so the chip does not shift as the
// tempo changes), and in its full look a slot for the speed badge. A dotted note is a little wider.
inline constexpr float kChipPad = 10.0f;
inline constexpr float kChipNote = 15.0f;
inline constexpr float kChipDot = 5.0f;
inline constexpr float kChipGap = 4.0f;
inline constexpr float kChipNumber = 28.0f;
inline constexpr float kChipBadge = 34.0f;

// The number sits in a slot of `kChipDigits` digits. A tempo with more (1000 and up) is drawn
// smaller to fit it, never wider: the chip, and so the bar, keeps its width. The scale of the
// number's font for a number of `digits` digits.
inline constexpr int kChipDigits = 3;
inline constexpr float chip_number_scale(int digits) {
    return digits <= kChipDigits ? 1.0f
                                 : static_cast<float>(kChipDigits) / static_cast<float>(digits);
}

// The tempo chip's cell: its face, then the dead gap to the next control.
inline constexpr float chip_width(bool badge, bool dotted) {
    return kChipPad + kChipNote + (dotted ? kChipDot : 0.0f) + kChipGap + kChipNumber + kChipPad +
           (badge ? kChipGap + kChipBadge : 0.0f) + tok::kHitGap;
}

// The Flow/Wait chip: its icon and the mode's name ("Flow", "Wait") on a pill (the kit's `chip`:
// padding, a small icon, a gap, the word at `tok::kLabel`, padding), in the forms with look
// buttons; in the tight and minimal forms the icon alone, in a bar cell `cell` wide.
inline constexpr float kModeChip = 80.0f; // the chip's face; its cell adds the dead gap
inline constexpr bool mode_has_name(BarForm form) {
    return form == BarForm::Full || form == BarForm::Compact;
}
inline constexpr float mode_width(BarForm form, float cell) {
    return mode_has_name(form) ? kModeChip + tok::kHitGap : cell;
}

// The song's transport: the buttons of `cell` the form keeps (replay, back, play, forward, the
// song's sound) and the two hand toggles, each at least `hit` (a cell: a hit area and the gap).
inline constexpr float hand_cell(float cell, float hit) { return std::max(cell * 0.9f, hit); }

// Every control a bar can hold, in the order every style draws them.
enum class Ctl : std::uint8_t {
    Replay,
    Back,
    Play,
    Forward,
    Sound,
    Accompaniment,     // mutes or unmutes the accompaniment; only for a song that has some
    AccompanimentMenu, // its chevron: the menu of the parts and the volume
    LeftHand,
    RightHand,
    Tempo,
    Metronome,
    Mode,
    Style,   // opens Settings > Notes
    Colours, // opens Settings > Effects
    Pin,
};
inline constexpr int kCtlCount = 15;

// The groups the controls fall in: the strip and the dock draw a divider between two, the groups
// style a card round each.
enum class Group : std::uint8_t { Song, Tempo, Look, Pin };
inline constexpr Group group_of(Ctl c) {
    switch (c) {
    case Ctl::Tempo:
    case Ctl::Metronome:
    case Ctl::Mode:
        return Group::Tempo;
    case Ctl::Style:
    case Ctl::Colours:
        return Group::Look;
    case Ctl::Pin:
        return Group::Pin;
    default:
        return Group::Song;
    }
}

inline constexpr const char* name_of(Ctl c) {
    constexpr const char* kNames[kCtlCount] = {
        "replay", "back",   "play", "forward", "sound", "accompaniment", "accompaniment menu",
        "left hand", "right hand", "tempo", "metronome", "mode", "style", "colour", "pin"};
    return kNames[static_cast<int>(c)];
}

// The caption the groups style draws under a control: it names the control, the same word in
// every state (the face shows the state), except the Flow/Wait chip, whose caption is the mode it
// is in (`mode_caption`; "Flow" here, the default).
inline constexpr const char* mode_caption(bool wait) { return wait ? "Wait" : "Flow"; }
inline constexpr const char* caption_of(Ctl c) {
    constexpr const char* kCaptions[kCtlCount] = {
        "Replay", "Back",  "Play", "Ahead", "Sound", "Backing", "Parts", "Left",
        "Right",  "Tempo", "Metronome", "Flow", "Style", "Colours", "Pin"};
    return c == Ctl::Mode ? mode_caption(false) : kCaptions[static_cast<int>(c)];
}

// The controls of a bar in `form`, in order: the song's only with a song.
struct Lineup {
    std::array<Ctl, kCtlCount> ctl{};
    int count = 0;
    void add(Ctl c) {
        if (count < kCtlCount) {
            ctl[static_cast<std::size_t>(count++)] = c;
        }
    }
    const Ctl* begin() const { return ctl.data(); }
    const Ctl* end() const { return ctl.data() + count; }
};

inline Lineup lineup(BarForm form, bool has_song, bool pin, bool accompaniment = false) {
    Lineup l;
    if (has_song) {
        if (bar_has_replay(form)) {
            l.add(Ctl::Replay);
        }
        if (bar_has_skips(form)) {
            l.add(Ctl::Back);
        }
        l.add(Ctl::Play);
        if (bar_has_skips(form)) {
            l.add(Ctl::Forward);
        }
        if (bar_has_replay(form)) {
            l.add(Ctl::Sound);
        }
        if (accompaniment) {
            l.add(Ctl::Accompaniment);
            l.add(Ctl::AccompanimentMenu);
        }
        l.add(Ctl::LeftHand);
        l.add(Ctl::RightHand);
        l.add(Ctl::Tempo);
        if (bar_has_metronome_toggle(form)) {
            l.add(Ctl::Metronome);
        }
        if (bar_has_mode(form)) {
            l.add(Ctl::Mode);
        }
    }
    const int looks = bar_look_shortcuts(form);
    if (looks >= 1) {
        l.add(Ctl::Style);
    }
    if (looks >= 2) {
        l.add(Ctl::Colours);
    }
    if (pin && bar_has_replay(form)) { // the lean and bare forms never keep the pin
        l.add(Ctl::Pin);
    }
    return l;
}

struct Control {
    const char* name = "";
    float w = 0.0f; // hit width
};

// The controls of a bar in one form, in the order they are drawn, and the gaps and dividers
// around them.
struct Plan {
    static constexpr int kMax = 24;
    std::array<Control, kMax> control{};
    int count = 0;
    float gaps = 0.0f;

    void add(const char* name, float w) {
        if (count < kMax) {
            control[static_cast<std::size_t>(count++)] = {name, w};
        }
    }
    float width() const {
        float w = gaps;
        for (int i = 0; i < count; ++i) {
            w += control[static_cast<std::size_t>(i)].w;
        }
        return w;
    }
};

// What a bar needs to know about the song to size itself.
struct Song {
    bool has_song = false;
    bool dotted = false; // the tempo chip's note is a dotted quarter
    bool accompaniment = false; // the song has accompaniment parts: its button and chevron show
};

// How wide one style draws each control, and what it puts between and round the groups.
struct StyleCells {
    float button = 0.0f;   // a transport button, the metronome, a look shortcut, the pin
    float tempo = 0.0f;    // 0: the tempo chip's own width (`chip_width`)
    float mode = 0.0f;     // 0: `mode_width`
    float switch_ = 0.0f;  // the metronome, the look shortcuts and the pin (0: `button`)
    float between = 0.0f;  // between two groups
    float per_group = 0.0f; // round each group (a card's padding)
    float ends = 0.0f;     // at the bar's two ends together
};

inline float cell_of(Ctl c, const StyleCells& s, BarForm form, Song song) {
    const float sw = s.switch_ > 0.0f ? s.switch_ : s.button;
    switch (c) {
    case Ctl::LeftHand:
    case Ctl::RightHand:
        return hand_cell(s.button, tok::kCell);
    case Ctl::Tempo:
        return s.tempo > 0.0f ? s.tempo : chip_width(bar_chip_has_badge(form), song.dotted);
    case Ctl::Mode:
        return s.mode > 0.0f ? s.mode : mode_width(form, s.button);
    case Ctl::AccompanimentMenu:
        return tok::kCell; // a chevron: no wider than a hit area and its gap ask
    case Ctl::Accompaniment:
    case Ctl::Metronome:
    case Ctl::Style:
    case Ctl::Colours:
    case Ctl::Pin:
        return sw;
    default:
        return s.button;
    }
}

inline Plan plan_with(const StyleCells& s, BarForm form, Song song, bool pin) {
    Plan p;
    p.gaps += s.ends;
    int groups = 0;
    Group last = Group::Song;
    for (const Ctl c : lineup(form, song.has_song, pin, song.accompaniment)) {
        if (groups == 0 || group_of(c) != last) {
            ++groups;
            last = group_of(c);
        }
        p.add(name_of(c), cell_of(c, s, form, song));
    }
    p.gaps += static_cast<float>(groups) * s.per_group;
    if (groups > 1) {
        p.gaps += static_cast<float>(groups - 1) * s.between;
    }
    return p;
}

// Strip: the controls across the top row, a divider between two groups.
inline constexpr float kStripCell = tok::kCell;
inline constexpr StyleCells kStrip{kStripCell, 0.0f, 0.0f, 0.0f, tok::kS2, 0.0f, 0.0f};
inline Plan strip_plan(BarForm form, Song song, bool pin = true) {
    return plan_with(kStrip, form, song, pin);
}

// Dock: a rounded dock with its ends, centred in the window; a divider between two groups.
inline constexpr float kDockCell = tok::kCell;
inline constexpr StyleCells kDock{kDockCell, 0.0f, 0.0f, 0.0f, tok::kS2, 0.0f, 2.0f * tok::kS1};
inline Plan dock_plan(BarForm form, Song song, bool pin = true) {
    return plan_with(kDock, form, song, pin);
}

// Groups: a card round each group, a caption under each icon, centred in the window.
inline constexpr float kGroupsCell = 64.0f;      // a switch with its caption (cell)
inline constexpr float kGroupsTransport = tok::kCell; // a transport button
inline constexpr float kGroupsPad = tok::kS1;
// Each group is a card `kGroupsCardInset` shorter than the top row, centred on it. The transport's
// faces are `kGroupsTransportFace` square, the other switches' and the tempo chip's
// `kGroupsFace` (both as asked: face_layout.hpp may make them smaller to keep their captions).
inline constexpr float kGroupsCardInset = tok::kS1;
inline constexpr float kGroupsTransportFace = 32.0f;
inline constexpr float kGroupsFace = 28.0f;
// The Flow/Wait chip is a captioned icon as narrow as a transport button.
inline constexpr StyleCells kGroups{kGroupsTransport, kGroupsCell,       kGroupsTransport,
                                    kGroupsCell,      tok::kS2,          2.0f * kGroupsPad,
                                    0.0f};
inline Plan groups_plan(BarForm form, Song song, bool pin = true) {
    return plan_with(kGroups, form, song, pin);
}

// The dock: `kDockRowInset` shorter than the top row, at most `kDockMaxHeight`; its faces are
// `kDockFaceInset` shorter than the dock.
inline constexpr float kDockMaxHeight = 46.0f;
inline constexpr float kDockRowInset = 6.0f;
inline constexpr float kDockFaceInset = 8.0f;
inline constexpr float dock_height(float row_h) {
    return std::min(row_h - kDockRowInset, kDockMaxHeight);
}

// Where a style puts its bar in the top row (split_top_row): the groups and the dock centred in
// the window at every size, the strip against the gear.
inline constexpr bool centred(BarStyle style) { return style != BarStyle::Strip; }

// Every form, richest first, for whoever measures them all.
inline constexpr BarForm kForms[] = {BarForm::Full, BarForm::Compact, BarForm::Tight,
                                     BarForm::Minimal, BarForm::Lean, BarForm::Bare,
                                     BarForm::Least};

} // namespace etude::app::ui::bar_metrics
