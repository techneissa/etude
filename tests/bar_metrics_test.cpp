// SPDX-License-Identifier: GPL-3.0-or-later
// Every control of the three bars keeps its hit area (12 mm) and a dead gap (2 mm) to the next, at
// the smallest supported window and on from it, pinned and not. The widths are the ones the bars draw with (app/ui/bar_metrics.hpp, the real
// tokens); the room is the real layout's (compute_layout, bar_room) and the form the real choice
// (fit_bar_form). Where nothing fits, what goes is a whole control.
#include "ui/bar_metrics.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app::ui;
namespace bm = etude::app::ui::bar_metrics;

namespace {

struct StyleCase {
    BarStyle style;
    const char* name;
};
constexpr StyleCase kStyles[] = {
    {BarStyle::Strip, "strip"}, {BarStyle::Groups, "groups"}, {BarStyle::Dock, "dock"}};

bm::Plan plan_of(BarStyle style, BarForm form, bm::Song song, bool pin) {
    switch (style) {
    case BarStyle::Strip:
        return bm::strip_plan(form, song, pin);
    case BarStyle::Groups:
        return bm::groups_plan(form, song, pin);
    case BarStyle::Dock:
        break;
    }
    return bm::dock_plan(form, song, pin);
}

BarFormWidths widths_of(BarStyle style, bm::Song song, float unit) {
    return {plan_of(style, BarForm::Full, song, true).width() * unit,
            plan_of(style, BarForm::Compact, song, true).width() * unit,
            plan_of(style, BarForm::Tight, song, true).width() * unit,
            plan_of(style, BarForm::Minimal, song, true).width() * unit,
            plan_of(style, BarForm::Minimal, song, false).width() * unit,
            plan_of(style, BarForm::Lean, song, false).width() * unit,
            plan_of(style, BarForm::Bare, song, false).width() * unit,
            plan_of(style, BarForm::Least, song, false).width() * unit};
}

// What the host does with a window: the room, the form, whether the pin stays.
struct Chosen {
    BarForm form = BarForm::Minimal;
    bool pin = true;
    float room = 0.0f; // for the form chosen
    float top_row_h = 0.0f;
};

// `profile_w`: the width of the profile chip that follows the library and learn buttons (0: none,
// a narrow row whose menu holds the profile). The host keeps the buttons and the chip clear of the
// bar, so the status area's lead reaches past them (app/ui/host_status.cpp).
Chosen choose(BarStyle style, bm::Song song, float width, float height, float unit,
              bool pinned_setting, float profile_w = 0.0f) {
    PinnedBar pb;
    pb.pinned = pinned_setting;
    pb.style = style;
    pb.has_song = song.has_song;
    const float px_per_mm = kDefaultPxPerMm * unit;
    const ScreenLayout lay = compute_layout(width, height, px_per_mm, LookSettings{}, pb);
    // The worst case for the room: the status text wants all it may keep.
    float lead = 0.0f;
    const float buttons_right = !lay.learn.empty()     ? lay.learn.right()
                                : !lay.library.empty() ? lay.library.right()
                                                       : 0.0f;
    if (profile_w > 0.0f && buttons_right > 0.0f) {
        lead = buttons_right + tok::kHitGap * unit + profile_w * unit - lay.status_text.x +
               tok::kS2 * unit;
    }
    const float comfy_keep = std::max(
        unit * (bm::centred(style) ? bm::kKeepStatusCentred : bm::kKeepStatus), lead);
    const float least_keep = std::max(unit * bm::kMinStatus, lead);
    const BarFormWidths widths = widths_of(style, song, unit);
    Chosen c;
    c.top_row_h = lay.top_row.h;
    const float least_room = bar_room(lay, least_keep);
    c.form = fit_bar_form(bar_room(lay, comfy_keep), least_room, widths);
    c.pin = bar_keeps_pin(c.form, widths, least_room);
    c.room = c.form == BarForm::Full ? bar_room(lay, comfy_keep) : least_room;
    return c;
}

const char* form_name(BarForm form) {
    switch (form) {
    case BarForm::Full:
        return "full";
    case BarForm::Compact:
        return "compact";
    case BarForm::Tight:
        return "tight";
    case BarForm::Minimal:
        return "minimal";
    case BarForm::Lean:
        return "lean";
    case BarForm::Bare:
        return "bare";
    case BarForm::Least:
        break;
    }
    return "least";
}


} // namespace

TEST_CASE("bar: at 800x480 every control of every bar keeps its cell, pinned and not, with or without a dotted beat") {
    for (const StyleCase& st : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const bool dotted : {false, true}) {
                CAPTURE(st.name);
                CAPTURE(pinned);
                CAPTURE(dotted);
                const bm::Song song{true, dotted};
                const Chosen c = choose(st.style, song, 800.0f, 480.0f, 1.0f, pinned);
                // The smallest window (800 points: a panel that is a 96 dpi screen's size) gets at
                // least the Minimal form; the pin is the control that
                // gives way there (its switch stays in Settings > View), and a denser panel's
                // window is fewer points and gets a narrower form (a control is never shrunk to
                // fit).
                CHECK(static_cast<int>(c.form) <= static_cast<int>(BarForm::Minimal));
                CHECK(c.top_row_h >= tok::kHit); // the hit height of every control in the row
                const bm::Plan plan = plan_of(st.style, c.form, song, c.pin);
                CHECK(plan.width() <= c.room);
                for (int i = 0; i < plan.count; ++i) {
                    const bm::Control& ctl = plan.control[static_cast<std::size_t>(i)];
                    CAPTURE(ctl.name);
                    CHECK(ctl.w - tok::kHitGap >= tok::kHit); // the hit area, less the gap
                }
                CHECK(plan.count >= 7);
            }
        }
    }
}

TEST_CASE("bar: the Minimal form fits 800x480 in all three styles, with the room left over printed") {
    for (const StyleCase& st : kStyles) {
        const bm::Song song{true, true}; // the widest chip
        const BarFormWidths w = widths_of(st.style, song, 1.0f);
        PinnedBar pb;
        pb.style = st.style;
        pb.has_song = true;
        const ScreenLayout lay = compute_layout(800.0f, 480.0f, kDefaultPxPerMm, LookSettings{}, pb);
        const float room = bar_room(lay, bm::kMinStatus);
        std::printf("bar %-6s 800x480: room %.0f px, full %.0f compact %.0f tight %.0f minimal %.0f\n",
                    st.name, static_cast<double>(room), static_cast<double>(w.full),
                    static_cast<double>(w.compact), static_cast<double>(w.tight),
                    static_cast<double>(w.minimal));
        CHECK(w.minimal_no_pin <= room);
    }
}

TEST_CASE("bar: no control is ever smaller than a hit area and its gap in any form of any style") {
    for (const StyleCase& st : kStyles) {
        for (const bool dotted : {false, true}) {
            for (const BarForm form : bm::kForms) {
                for (const bool song : {false, true}) {
                    for (const bool pin : {false, true}) {
                        const bm::Plan plan = plan_of(st.style, form, {song, dotted}, pin);
                        CAPTURE(st.name);
                        CAPTURE(static_cast<int>(form));
                        for (int i = 0; i < plan.count; ++i) {
                            const bm::Control& ctl = plan.control[static_cast<std::size_t>(i)];
                            CAPTURE(ctl.name);
                            CHECK(ctl.w >= tok::kCell);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("bar: the forms only get narrower, each by whole controls, in one order") {
    for (const StyleCase& st : kStyles) {
        const bm::Song song{true, false};
        CAPTURE(st.name);
        // Narrowest last: the minimal form with its pin, then without it, then lean, bare, least.
        const float widths[] = {plan_of(st.style, BarForm::Full, song, true).width(),
                                plan_of(st.style, BarForm::Compact, song, true).width(),
                                plan_of(st.style, BarForm::Tight, song, true).width(),
                                plan_of(st.style, BarForm::Minimal, song, true).width(),
                                plan_of(st.style, BarForm::Minimal, song, false).width(),
                                plan_of(st.style, BarForm::Lean, song, true).width(),
                                plan_of(st.style, BarForm::Bare, song, true).width(),
                                plan_of(st.style, BarForm::Least, song, true).width()};
        for (std::size_t i = 1; i < std::size(widths); ++i) {
            CAPTURE(i);
            CHECK(widths[i] < widths[i - 1]);
            CHECK(widths[i - 1] - widths[i] >= tok::kCell); // at least one whole control
        }
        const auto has = [&](BarForm form, const std::string& name) {
            const bm::Plan p = plan_of(st.style, form, song, true);
            for (int i = 0; i < p.count; ++i) {
                if (name == p.control[static_cast<std::size_t>(i)].name) {
                    return true;
                }
            }
            return false;
        };
        for (const BarForm form : bm::kForms) {
            CAPTURE(static_cast<int>(form));
            // Play, the hands and the tempo chip are in every form; the Flow/Wait chip in every one
            // but the least (its row is in the tempo popover there).
            for (const char* name : {"play", "left hand", "right hand", "tempo"}) {
                CHECK(has(form, name));
            }
            CHECK(has(form, "mode") == (form != BarForm::Least));
            CHECK(bar_has_mode(form) == (form != BarForm::Least));
            const bool bare = form == BarForm::Bare || form == BarForm::Least;
            const bool lean = form == BarForm::Lean || bare;
            CHECK(has(form, "pin") == !lean);
            CHECK(has(form, "replay") == !lean);
            // The sheet view has no button on the bar in any form: its side button, Settings >
            // View and the I key hold it.
            CHECK_FALSE(has(form, "view"));
            CHECK(has(form, "sound") == !lean);
            CHECK(has(form, "back") == !bare);
            CHECK(has(form, "forward") == !bare);
            CHECK(has(form, "metronome") == bar_has_metronome_toggle(form));
            CHECK(has(form, "style") == (form == BarForm::Full || form == BarForm::Compact));
            CHECK(has(form, "colour") == (form == BarForm::Full));
        }
    }
}

TEST_CASE("bar: there is no Look button, no Looks chip and no look switch in any form of any style") {
    for (const StyleCase& st : kStyles) {
        for (const BarForm form : bm::kForms) {
            for (const bool song : {false, true}) {
                const bm::Plan plan = plan_of(st.style, form, {song, false}, true);
                CAPTURE(st.name);
                CAPTURE(static_cast<int>(form));
                for (int i = 0; i < plan.count; ++i) {
                    const std::string name = plan.control[static_cast<std::size_t>(i)].name;
                    CHECK(name != "look");
                    CHECK(name != "look chip");
                    // The look shortcuts open their pages; nothing on the bar flips a look setting.
                    CHECK(name != "names");
                    CHECK(name != "c markers");
                }
            }
        }
    }
}

TEST_CASE("bar: every style holds the same controls in the same order in every form") {
    const auto names = [](const bm::Plan& p) {
        std::string out;
        for (int i = 0; i < p.count; ++i) {
            out += std::string(p.control[static_cast<std::size_t>(i)].name) + ",";
        }
        return out;
    };
    for (const BarForm form : bm::kForms) {
        for (const bool song : {false, true}) {
            for (const bool pin : {false, true}) {
                CAPTURE(static_cast<int>(form));
                CAPTURE(song);
                CAPTURE(pin);
                const std::string strip = names(bm::strip_plan(form, {song, false}, pin));
                CHECK(names(bm::groups_plan(form, {song, false}, pin)) == strip);
                CHECK(names(bm::dock_plan(form, {song, false}, pin)) == strip);
            }
        }
    }
    // The order, in full: the song's controls, the tempo group, the look shortcuts, the pin.
    CHECK(names(bm::strip_plan(BarForm::Full, {true, false}, true)) ==
          "replay,back,play,forward,sound,left hand,right hand,tempo,metronome,mode,style,"
          "colour,pin,");
    // A song with accompaniment parts adds its button and the menu's chevron after the song's
    // sound, in every form and every style; without parts they are not there.
    CHECK(names(bm::strip_plan(BarForm::Full, {true, false, true}, true)) ==
          "replay,back,play,forward,sound,accompaniment,accompaniment menu,left hand,right hand,"
          "tempo,metronome,mode,style,colour,pin,");
    CHECK(names(bm::strip_plan(BarForm::Bare, {true, false, true}, false)) ==
          "play,accompaniment,accompaniment menu,left hand,right hand,tempo,mode,");
    CHECK(names(bm::strip_plan(BarForm::Least, {true, false, true}, false)) ==
          "play,accompaniment,accompaniment menu,left hand,right hand,tempo,");
    for (const BarForm form : bm::kForms) {
        const bm::Plan with = bm::strip_plan(form, {true, false, true}, true);
        const bm::Plan without = bm::strip_plan(form, {true, false, false}, true);
        CHECK(with.count == without.count + 2);
        // The button keeps its cell, the chevron its hit area, and nothing else changes.
        CHECK(with.width() == doctest::Approx(without.width() + 2.0f * tok::kCell));
        CHECK(names(bm::groups_plan(form, {true, false, true}, true)) ==
              names(bm::dock_plan(form, {true, false, true}, true)));
        CHECK(names(bm::groups_plan(form, {true, false, true}, true)) ==
              names(bm::strip_plan(form, {true, false, true}, true)));
    }
    // The Flow/Wait chip's caption is the mode it is in; every other caption names its control.
    CHECK(std::string(bm::mode_caption(false)) == "Flow");
    CHECK(std::string(bm::mode_caption(true)) == "Wait");
    CHECK(std::string(bm::caption_of(bm::Ctl::Tempo)) == "Tempo");
    // Every control has a caption, one word.
    for (int i = 0; i < bm::kCtlCount; ++i) {
        const std::string caption = bm::caption_of(static_cast<bm::Ctl>(i));
        CAPTURE(caption);
        CHECK_FALSE(caption.empty());
        CHECK(caption.find(' ') == std::string::npos);
    }
}

TEST_CASE("bar: every style has the Flow/Wait chip in every form with a song but the least, a whole cell") {
    for (const StyleCase& st : kStyles) {
        for (const BarForm form : bm::kForms) {
            for (const bool pin : {false, true}) {
                CAPTURE(st.name);
                CAPTURE(static_cast<int>(form));
                const bm::Plan with = plan_of(st.style, form, {true, false}, pin);
                const bm::Plan without = plan_of(st.style, form, {false, false}, pin);
                int seen = 0;
                for (int i = 0; i < with.count; ++i) {
                    const bm::Control& c = with.control[static_cast<std::size_t>(i)];
                    if (std::string(c.name) == "mode") {
                        ++seen;
                        CHECK(c.w >= tok::kCell);
                    }
                }
                CHECK(seen == (form == BarForm::Least ? 0 : 1));
                for (int i = 0; i < without.count; ++i) {
                    CHECK(std::string(without.control[static_cast<std::size_t>(i)].name) != "mode");
                }
            }
        }
        // The name shows where the look shortcuts do; the narrower forms draw the icon.
        CHECK(bm::mode_has_name(BarForm::Full));
        CHECK(bm::mode_has_name(BarForm::Compact));
        for (const BarForm form : {BarForm::Tight, BarForm::Minimal, BarForm::Lean, BarForm::Bare,
                                   BarForm::Least}) {
            CHECK_FALSE(bm::mode_has_name(form));
        }
    }
}

TEST_CASE("bar: at 800x480 and x1.0 with the profile chip beside the buttons every style is Lean") {
    // The Player chip is about 112 points wide at 800x480: the status area keeps the buttons and
    // the chip clear of the bar, so the room is Lean's: no replay, sound, metronome or pin, but the
    // skip buttons and the Flow/Wait chip. Without the sheet-music button the bar was Bare here.
    constexpr float kProfileChip = 112.0f;
    for (const StyleCase& st : kStyles) {
        for (const bool pinned : {false, true}) {
            CAPTURE(st.name);
            CAPTURE(pinned);
            const Chosen c = choose(st.style, {true, false}, 800.0f, 480.0f, 1.0f, pinned,
                                    kProfileChip);
            CHECK(c.form == BarForm::Lean);
            CHECK_FALSE(c.pin);
            const bm::Plan plan = plan_of(st.style, c.form, {true, false}, c.pin);
            CHECK(plan.width() <= c.room);
        }
    }
}

TEST_CASE("bar: windows from 800 wide up keep every control whole at any scale") {
    const float sizes[][2] = {{800, 480}, {960, 540}, {1024, 600}, {1280, 720}, {1600, 960},
                              {1920, 1080}};
    for (const StyleCase& st : kStyles) {
        for (const auto& size : sizes) {
            for (const float unit : {1.0f, 1.25f, 1.5f, 2.0f}) {
                // A denser display has bigger controls in more pixels: the same window in points.
                const float w = size[0] * unit;
                const float h = size[1] * unit;
                const Chosen c = choose(st.style, {true, true}, w, h, unit, false);
                CAPTURE(st.name);
                CAPTURE(w);
                CAPTURE(unit);
                CHECK(static_cast<int>(c.form) <= static_cast<int>(BarForm::Minimal));
                // `choose` is given `w` pixels at `unit`: a window of w / unit points. Wider than
                // 800 points keeps the pin.
                if (w / unit > 800.5f) {
                    CHECK(c.pin);
                }
                CHECK(bm::chip_width(true, true) * unit >= tok::kCell * unit);
                const bm::Plan plan = plan_of(st.style, c.form, {true, true}, c.pin);
                CHECK(plan.width() * unit <= c.room);
            }
        }
    }
}

TEST_CASE("bar: an 800x480 window at a denser screen: a narrower form as it grows, whole controls only, the status keeps its least") {
    // The window stays 800x480 px; the controls grow with the screen's density (1.00 is 96 dpi),
    // and the layout's top row and gear grow with it. The bar gives up whole controls in one order
    // (BarForm) and never cuts the status text below kMinStatus while a narrower form is left.
    for (const StyleCase& st : kStyles) {
        for (const bool pinned : {false, true}) {
            int last = 0;
            bool pin_gone = false;
            for (const float unit : {1.00f, 1.01f, 1.02f, 1.05f, 1.10f, 1.20f, 1.25f, 1.40f, 1.50f}) {
                const bm::Song song{true, true}; // the widest chip
                const Chosen c = choose(st.style, song, 800.0f, 480.0f, unit, pinned);
                CAPTURE(st.name);
                CAPTURE(pinned);
                CAPTURE(unit);
                const bm::Plan plan = plan_of(st.style, c.form, song, c.pin);
                std::printf("bar %-6s 800x480 x%.2f%s: %s%s, bar %.0f of room %.0f px\n", st.name,
                            static_cast<double>(unit), pinned ? " pinned" : "", form_name(c.form),
                            c.pin ? "" : " (no pin)", static_cast<double>(plan.width() * unit),
                            static_cast<double>(c.room));
                for (int i = 0; i < plan.count; ++i) {
                    CHECK(plan.control[static_cast<std::size_t>(i)].w >= tok::kCell);
                }
                CHECK(c.top_row_h >= tok::kHit * unit);
                CHECK(plan.width() * unit <= c.room); // so the status keeps its least
                CHECK(static_cast<int>(c.form) >= last); // a denser screen never brings one back
                CHECK_FALSE((pin_gone && c.pin));
                pin_gone = pin_gone || !c.pin;
                last = static_cast<int>(c.form);
                // The look shortcuts go first: at 800x480 there are none.
                CHECK(bm::lineup(c.form, true, c.pin).count <= bm::lineup(BarForm::Tight, true, true).count);
            }
        }
    }
}

TEST_CASE("bar: a tempo of four digits or more is drawn smaller in the same slot, the bar keeps its width") {
    // The number's slot is three digits wide. More digits scale the font down to fit it.
    CHECK(bm::chip_number_scale(1) == 1.0f);
    CHECK(bm::chip_number_scale(3) == 1.0f);
    for (int digits = 4; digits <= 5; ++digits) {
        CAPTURE(digits);
        const float scale = bm::chip_number_scale(digits);
        CHECK(scale < 1.0f);
        // Digits are as wide as each other, so the drawn width is digits x scale: it is the slot's.
        CHECK(static_cast<float>(digits) * scale == doctest::Approx(static_cast<float>(bm::kChipDigits)));
        // Never so small it cannot be read: a 15 pt number stays above 9 pt.
        CHECK(15.0f * scale >= 9.0f);
    }
    // The chip's width does not depend on the number, so the forms and the fit do not either:
    // the widths are those of a three-digit tempo.
    for (const StyleCase& st : kStyles) {
        const BarFormWidths three = widths_of(st.style, {true, false}, 1.0f);
        const BarFormWidths four = widths_of(st.style, {true, false}, 1.0f);
        CHECK(three.tight == four.tight);
    }
    // The shown tempo itself: 1200 is four digits, and it is the number the chip is given.
    CHECK(std::to_string(1200).size() == 4);
}

TEST_CASE("bar: narrower than the minimum window, the pin goes first, then replay and sound, then the skips, never a shrunk control") {
    // The app's smallest window is 800 px wide; below it only a developer's --window goes, or a
    // bigger UI scale. There the pin leaves the bar first (the switch stays in Settings > View),
    // then the replay and song-sound buttons (Home and M), then the skips (the arrow keys, the
    // band's scrub). Narrower still than play, the hands and the two chips hold, the bar is cut.
    for (const StyleCase& st : kStyles) {
        const bm::Song song{true, true};
        const bm::Plan with_pin = plan_of(st.style, BarForm::Minimal, song, true);
        const bm::Plan without = plan_of(st.style, BarForm::Minimal, song, false);
        const bm::Plan bare = plan_of(st.style, BarForm::Least, song, false);
        // The pin is exactly one control: the rest is the same, whole.
        CHECK(without.count == with_pin.count - 1);
        CHECK(with_pin.width() - without.width() >= tok::kCell);
        bool went = false;
        int last = 0;
        for (float width = 800.0f; width >= 200.0f; width -= 8.0f) {
            const Chosen c = choose(st.style, song, width, 480.0f, 1.0f, false);
            CAPTURE(st.name);
            CAPTURE(width);
            const bm::Plan plan = plan_of(st.style, c.form, song, c.pin);
            for (int i = 0; i < plan.count; ++i) {
                CHECK(plan.control[static_cast<std::size_t>(i)].w >= tok::kCell);
            }
            CHECK(static_cast<int>(c.form) >= last);
            last = static_cast<int>(c.form);
            if (!c.pin) {
                went = true;
                CHECK(static_cast<int>(c.form) >= static_cast<int>(BarForm::Minimal));
                CHECK(with_pin.width() > c.room); // it left because it did not fit
                if (bare.width() > c.room) {
                    CHECK(c.form == BarForm::Least);
                    break; // below this even the least bar is wider than the window allows
                }
                CHECK(plan.width() <= c.room);
            } else {
                CHECK(plan.width() <= c.room);
                CHECK_FALSE(went); // a pin that went does not come back at a narrower window
            }
        }
        CHECK(went); // the sweep reached a window too narrow for the pin
        CHECK(last == static_cast<int>(BarForm::Least)); // and on to the least form
    }
}

TEST_CASE("bar: the status text keeps its readable least beside every bar at 800x480, x1.0 to x1.5") {
    // What the host does: the form, then the bar placed by split_top_row in the style's place.
    for (const StyleCase& st : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const float unit : {1.0f, 1.25f, 1.5f}) {
                const bm::Song song{true, true};
                PinnedBar pb;
                pb.pinned = pinned;
                pb.style = st.style;
                pb.has_song = true;
                const ScreenLayout lay =
                    compute_layout(800.0f, 480.0f, kDefaultPxPerMm * unit, LookSettings{}, pb);
                const Chosen c = choose(st.style, song, 800.0f, 480.0f, unit, pinned);
                const float keep = unit * (c.form == BarForm::Full
                                               ? (bm::centred(st.style) ? bm::kKeepStatusCentred
                                                                        : bm::kKeepStatus)
                                               : bm::kMinStatus);
                const float bar_w = plan_of(st.style, c.form, song, c.pin).width() * unit;
                const TopRowSplit split = split_top_row(lay, bar_w, keep, bm::centred(st.style));
                CAPTURE(st.name);
                CAPTURE(pinned);
                CAPTURE(unit);
                CHECK(split.bar.w == doctest::Approx(std::floor(bar_w))); // never cut
                CHECK(split.status_text.w >= bm::kMinStatus * unit - 1.0f);
                CHECK(split.status_text.right() <= split.bar.x);
            }
        }
    }
}

TEST_CASE("bar: the groups and the dock are centred in the window at 1366, 1920 and 2560, the strip ends at the gear") {
    const float sizes[][2] = {{1366, 768}, {1920, 1080}, {2560, 1440}, {2560, 1080}, {3440, 1440}};
    for (const StyleCase& st : kStyles) {
        for (const auto& size : sizes) {
            for (const bool pinned : {false, true}) {
                const float w = size[0];
                const float h = size[1];
                PinnedBar pb;
                pb.pinned = pinned;
                pb.style = st.style;
                pb.has_song = true;
                const ScreenLayout lay = compute_layout(w, h, kDefaultPxPerMm, LookSettings{}, pb);
                const float u = ui_scale(w, h, kDefaultPxPerMm);
                const bm::Song song{true, false};
                const BarFormWidths widths = widths_of(st.style, song, u);
                const float comfy =
                    u * (bm::centred(st.style) ? bm::kKeepStatusCentred : bm::kKeepStatus);
                const BarForm form = fit_bar_form(bar_room(lay, comfy),
                                                  bar_room(lay, u * bm::kMinStatus), widths);
                CAPTURE(st.name);
                CAPTURE(w);
                CAPTURE(pinned);
                CHECK(form == BarForm::Full); // every control, from 1366x768 up
                const float bar_w = plan_of(st.style, form, song, true).width() * u;
                const TopRowSplit split = split_top_row(lay, bar_w, comfy, bm::centred(st.style));
                if (bm::centred(st.style)) {
                    CHECK(std::fabs(split.bar.x + split.bar.w * 0.5f - w * 0.5f) <= 1.0f);
                } else {
                    CHECK(split.bar.right() == doctest::Approx(lay.status_text.right()));
                }
                CHECK(split.status_text.w >= comfy - 1.0f);
                // Nothing overlaps: the status text ends before the bar, the bar before the gear.
                CHECK(split.status_text.right() <= split.bar.x);
                CHECK(split.bar.right() <= lay.gear.x);
                CHECK(split.bar.w == doctest::Approx(std::floor(bar_w))); // never cut
            }
        }
    }
}

TEST_CASE("bar: every style keeps each form at exactly its room and gives up the next control 1 px under it") {
    // The drop points in order: each step's room is that form's whole width; one pixel less gives
    // the next narrower step. The minimal form drops its pin first, then the lean, bare and least
    // forms.
    struct Step {
        BarForm form;
        bool pin;
    };
    constexpr Step kSteps[] = {{BarForm::Full, true},    {BarForm::Compact, true},
                               {BarForm::Tight, true},   {BarForm::Minimal, true},
                               {BarForm::Minimal, false}, {BarForm::Lean, false},
                               {BarForm::Bare, false},    {BarForm::Least, false}};
    for (const StyleCase& st : kStyles) {
        for (const float u : {1.0f, 1.5f}) {
            for (const bool dotted : {false, true}) {
                const bm::Song song{true, dotted};
                const BarFormWidths widths = widths_of(st.style, song, u);
                for (std::size_t i = 0; i < std::size(kSteps); ++i) {
                    const Step& at = kSteps[i];
                    const float room = plan_of(st.style, at.form, song, at.pin).width() * u;
                    CAPTURE(st.name);
                    CAPTURE(u);
                    CAPTURE(dotted);
                    CAPTURE(form_name(at.form));
                    CAPTURE(at.pin);
                    // Exactly at: that form, with or without its pin as the step says.
                    const BarForm form = fit_bar_form(room, room, widths);
                    CHECK(form == at.form);
                    CHECK(bar_keeps_pin(form, widths, room) == at.pin);
                    // Just under: the next step (the least form stays, cut to the room).
                    const Step& next = i + 1 < std::size(kSteps) ? kSteps[i + 1] : kSteps[i];
                    const BarForm under = fit_bar_form(room - 1.0f, room - 1.0f, widths);
                    CHECK(under == next.form);
                    CHECK(bar_keeps_pin(under, widths, room - 1.0f) == next.pin);
                }
            }
        }
    }
}

TEST_CASE("bar_keeps_pin: only a minimal bar that does not fit loses it, the lean forms never have it") {
    const BarFormWidths w{720.0f, 640.0f, 560.0f, 420.0f, 370.0f, 280.0f, 180.0f};
    CHECK_FALSE(bar_keeps_pin(BarForm::Lean, w, 1000.0f));
    CHECK_FALSE(bar_keeps_pin(BarForm::Bare, w, 1000.0f));
    CHECK_FALSE(bar_keeps_pin(BarForm::Least, w, 1000.0f));
    CHECK(bar_keeps_pin(BarForm::Full, w, 100.0f));
    CHECK(bar_keeps_pin(BarForm::Compact, w, 0.0f));
    CHECK(bar_keeps_pin(BarForm::Tight, w, 0.0f));
    CHECK(bar_keeps_pin(BarForm::Minimal, w, 420.0f));  // exactly fits
    CHECK(bar_keeps_pin(BarForm::Minimal, w, 500.0f));
    CHECK_FALSE(bar_keeps_pin(BarForm::Minimal, w, 419.0f));
    CHECK_FALSE(bar_keeps_pin(BarForm::Minimal, w, 0.0f));
    const float nan = static_cast<float>(std::nan(""));
    CHECK_FALSE(bar_keeps_pin(BarForm::Minimal, w, nan));
    BarFormWidths bad;
    bad.minimal = nan;
    CHECK_FALSE(bar_keeps_pin(BarForm::Minimal, bad, 500.0f));
}

TEST_CASE("bar: the tempo chip is wide enough to touch, dotted or not, with or without the badge") {
    for (const bool badge : {false, true}) {
        for (const bool dotted : {false, true}) {
            CHECK(bm::chip_width(badge, dotted) >= tok::kCell);
        }
    }
    CHECK(bm::chip_width(true, true) > bm::chip_width(true, false));
    CHECK(bm::chip_width(true, false) > bm::chip_width(false, false));
}

TEST_CASE("bar: on the 7 inch panel at 800x480 and x1.5 the bar keeps clear of the menu button and the gear, whole") {
    // 154 mm over 800 px, the taste scale on top: the window is 388 points wide, so the bare bar
    // (with its Flow/Wait chip) is wider than the room between the buttons. The least form, which
    // leaves the Flow/Wait chip to the tempo popover, is whole and the status text is never cut
    // into by the bar covering a button.
    const float ppmm = 800.0f / 154.0f * 1.5f;
    const float unit = ui_scale(800.0f, 480.0f, ppmm);
    for (const StyleCase& st : kStyles) {
        for (const bool pinned : {false, true}) {
            CAPTURE(st.name);
            CAPTURE(pinned);
            PinnedBar pb;
            pb.pinned = pinned;
            pb.style = st.style;
            pb.has_song = true;
            const ScreenLayout lay = compute_layout(800.0f, 480.0f, ppmm, LookSettings{}, pb);
            REQUIRE_FALSE(lay.menu.empty());
            const bm::Song song{true, false};
            const BarFormWidths widths = widths_of(st.style, song, unit);
            const float lead = lay.menu.right() - lay.status_text.x + unit * tok::kS2;
            const float least_keep = std::max(unit * bm::kMinStatus, lead);
            const float least_room = bar_room(lay, least_keep);
            CHECK(widths.bare > least_room); // the bare bar would not fit: it was cut over the menu
            const BarForm form = fit_bar_form(bar_room(lay, least_keep), least_room, widths);
            CHECK(form == BarForm::Least);
            const float bar_w = plan_of(st.style, form, song, false).width() * unit;
            const TopRowSplit split =
                split_top_row(lay, bar_w, least_keep, bm::centred(st.style));
            CHECK(split.bar.w == doctest::Approx(std::floor(bar_w))); // whole
            CHECK(split.bar.x >= lay.menu.right());                   // not over the menu button
            CHECK(split.bar.right() <= lay.gear.x);                   // nor the gear
        }
    }
}
