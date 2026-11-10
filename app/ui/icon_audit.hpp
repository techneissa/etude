// SPDX-License-Identifier: GPL-3.0-or-later
// The UI audit: `etude --icon-audit OUT.tsv` renders the UI settled and checks three rules from
// what it drew, appending one line per finding to OUT.tsv (`just icon-audit` runs it over the
// window sizes, UI scales and screens, scripts/icon-audit.sh):
//
// - Icons centred. The frame is drawn once more with every icon left out; the difference is the
//   ink (ink_measure.hpp). For each icon button that registered itself (`audit::note`, from the
//   kit's widgets): the button's centre, where its ink sits, the offset, and whether it keeps the
//   rule (the ink box's centre within 0.5 px of the button's, a play triangle right of centre by
//   8% of its width).
// - Labels centred. Text drawn by the kit's `label_at` (a chip's words, a segment, a button, a
//   row's title) is left out of the same second frame, and its ink is measured the same way: the
//   baseline read from the pixels (the foot of the lowest row with a real share of the ink, so a
//   descender does not count) less half the font's cap height must be within 1 px of the middle of
//   the slot the label was given, and where the label is centred, the ink box's middle too. A
//   line-box centred label ("All": no descenders, so it sits high) fails this.
// - Chips. Every text chip the kit draws registers its face: a face under the row's chip height
//   (`tok::kFace`), or a text chip narrower than `tok::kChipMinW`, is a finding.
// - Hit areas. Every pressable item drawn (ImGui's item hooks, item_hook.hpp) is at least 12 mm
//   wide and tall (real millimetres at the scenario's panel size), unless its window cuts it (a row
//   scrolled half out); no two pressable items overlap and none is closer than 2 mm to another
//   (hit_check.hpp; `hit-pair` lines). Items a card or window covers, and the drag surfaces, take
//   no part in a pair.
// - States inside the face. The frame is drawn with every control forced into each state in turn
//   (hover, pressed, selected, focus: `forced_state`, read by the kit's state helper) and compared
//   with the same frame unforced: every pixel that changes lies inside a face some control
//   registered (`audit::face`) or in a caption its state recolours (`audit::ink`), and every face
//   lies inside its area (its card, pill or hit area). What moves on its own (the live preview) is
//   found by drawing the unforced frame twice, before and after, and left out.
// - A pressable has a face. Every pressable item that its window does not cut has a face
//   registered by the kit's state helper in its area (`no-face` lines): a control drawn by hand
//   shows no state and no run of the states check could see it.
//
// The run cannot pass without measuring: an icon whose ink is not found, a tap that never finds its
// button (`tap-missing`, after `kTapGiveUpFrames` frames) and a frame still moving after the
// verify frames (`settle` line, verdict `unsettled`) are findings, not skips.
//
//   --icon-audit FILE.tsv     append one line per icon button to FILE.tsv, then quit
//   --audit-label TEXT        the scenario's name, the first column of each line
//   --audit-tap NAME[,NAME]   first tap the first button whose name contains each NAME (opens a
//                             popover or card before the audit looks at the screen); "touch:NAME"
//                             taps with a finger, "right:NAME" with the right button (a hold menu)
//   --audit-wheel N           after the taps, scroll the window under the middle of the screen by N
//                             wheel clicks (negative: down), so the rows below the fold are drawn
//   --audit-png FILE.png      also save the settled frame
//   --ui-scale S              developer: draw everything S times larger (1.25, 1.5), as a denser
//                             screen would
#pragma once

#include "ink_measure.hpp"

#include <imgui.h>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app::ui::audit {

// What the audit draws every control as this frame (the kit's state helper reads it).
enum class Forced : std::uint8_t { None, Hover, Pressed, Selected, Focus };
const char* forced_name(Forced f);

struct Note {
    std::string name;
    ImVec2 min;
    ImVec2 max;
    InkKind kind = InkKind::Centred;
    bool check_y = true;
};

// An audit run is on (the widgets register their buttons) / the frame being drawn leaves the icons
// out.
bool on();
bool icons_hidden();
// The frame being drawn leaves the kit's labels out (the pass that measures them; the icons are
// drawn, so neither measure sees the other's ink).
bool labels_hidden();
// While quiet, the buttons, chips and labels drawn are not the audit's: a screen under a popover
// or a card takes no taps and the card over it hides some of them. Faces and hit areas are still
// registered (a disabled control still has its face and its size).
void set_quiet(bool quiet);
bool quiet();
// While set, what an overlay window that takes the touches in its own area (the band hanging from the
// revealed bar over the summary page) covers at all is not the audit's: the buttons, chips and hit
// areas drawn under it, and the labels whose capitals' band lies wholly inside it. The page that is drawn
// under it sets it for its own draw.
void set_overlay(ImVec2 min, ImVec2 max);
void clear_overlay();
// While one lives, what is drawn is not on screen (a page drawn only to measure it): the audit
// takes no button, face, ink or hit from it.
class Unseen {
  public:
    Unseen();
    ~Unseen();
    Unseen(const Unseen&) = delete;
    Unseen& operator=(const Unseen&) = delete;
};
Forced forced_state();
// The audit's own: starts or ends the run, and starts a frame (the notes are cleared; `hidden`
// leaves the icons out of it).
void start();
void stop();
void begin_frame(bool hidden, Forced forced = Forced::None, bool labels_hidden = false);
const std::vector<Note>& notes();

// A control's face this frame: where its states are drawn (`face_min`..`face_max`) and the area the
// face must stay inside (`area_min`..`area_max`: its card, the pill it sits on, or its hit area), in
// ImGui points. A no-op unless an audit run is on. `dl` is the list it is drawn on,
// so a label is matched with a face of its own window, not one a card lies over.
void face(const char* name, ImVec2 face_min, ImVec2 face_max, ImVec2 area_min, ImVec2 area_max,
          const ImDrawList* dl);
struct Face {
    std::string name;
    ImVec2 min;
    ImVec2 max;
    ImVec2 area_min;
    ImVec2 area_max;
    const ImDrawList* dl = nullptr;
};
const std::vector<Face>& faces();
// A control's content outside its face that its state may recolour (a caption under it), in
// points: the audit lets those pixels change. A no-op unless an audit run is on.
void ink(ImVec2 min, ImVec2 max);
struct Box2 {
    ImVec2 min;
    ImVec2 max;
};
const std::vector<Box2>& inks();
// An area that moves on its own (the live look preview), in points: the audit leaves its pixels
// out of the state check. A no-op unless an audit run is on.
void live(ImVec2 min, ImVec2 max);
const std::vector<Box2>& lives();

// A pressable item this frame, from ImGui's item hooks: its window and label, its rectangle in
// points, and whether its window cuts it.
struct Hit {
    std::string name;
    ImVec2 min;
    ImVec2 max;
    bool cut = false;
    ImVec2 clip_min; // what its window shows
    ImVec2 clip_max;
    ImGuiID window = 0;
    bool quiet = false;   // drawn under a card or popover that takes every touch
    bool covered = false; // set by `resolve_hits`: a card or window above takes the touch
};
const std::vector<Hit>& hits();
// Once the frame is drawn: marks the hits a window above them covers (most of the hit under the
// window's rectangle), so the pairs check leaves them out. Call it before copying `hits()`.
void resolve_hits();
// The kit's unit this frame (ImGui points per design point).
void set_unit(float u);
float unit();
// ImGui points per real millimetre this frame (the display's, without the UI scale): the unit the
// hit-area rules are measured in.
void set_pt_per_mm(float pt_per_mm);
float pt_per_mm();

// A label this frame (the kit's `label_at`): its text, the slot it should be centred in, the area
// to look for its ink in (its line box) and the font's cap height (all in ImGui points). A no-op
// unless an audit run is on or the audit is quiet. A label its window's clip cuts is kept (it can
// lie over another's) but not measured.
struct Label {
    std::string text;
    ImVec2 slot_min;
    ImVec2 slot_max;
    ImVec2 area_min;
    ImVec2 area_max;
    ImVec2 band_min; // where its capitals stand (the letters' sides, the cap band): what must be
    ImVec2 band_max; // clear of any other label's for the measure to be its own
    float cap_h = 0.0f;
    LabelAlign align = LabelAlign::Centre;
    bool descenders = false; // its letters hang below the baseline (g j p q y ...)
    bool standing = true;    // it has letters or digits to stand on the baseline (not "–")
    bool cut = false;        // its window's clip cuts its capitals: only part of them is on screen
    bool out = false;        // its window's clip leaves its capitals out altogether
    const ImDrawList* dl = nullptr; // the list it is drawn on
};
void label(const ImDrawList* dl, const char* text, ImVec2 slot_min, ImVec2 slot_max,
           ImVec2 area_min, ImVec2 area_max, ImVec2 band_min, ImVec2 band_max, float cap_h,
           LabelAlign align, bool descenders, bool standing);
const std::vector<Label>& labels();

// A chip this frame: its face, whether it has words (an icon-only chip is round and has no minimum
// width of the words' chips), and whether the bar sized it (a bar's chip takes the height its card
// leaves). A no-op unless an audit run is on.
struct Chip {
    std::string name;
    ImVec2 min;
    ImVec2 max;
    bool words = false;
    bool bar = false;
};
void chip(const char* name, ImVec2 face_min, ImVec2 face_max, bool words, bool bar);
const std::vector<Chip>& chips();

// What the settings sheet's fit looks like this frame (settings_fit.hpp): the texts the kit cut to their
// room while the sheet drew, and the pages that scroll. `SettingsScope` marks the sheet's own drawing
// (its rail, header and page), so a cut elsewhere is not the sheet's. All no-ops unless an audit run
// is on.
class SettingsScope {
  public:
    SettingsScope();
    ~SettingsScope();
    SettingsScope(const SettingsScope&) = delete;
    SettingsScope& operator=(const SettingsScope&) = delete;
};
// `shown` is what `text` was cut to; noted only inside a `SettingsScope` and when they differ.
void cut(std::string_view text, std::string_view shown);
// The page `page` is drawn open with `max_scroll` points it can scroll (0 when it fits).
void page_scroll(const char* page, float max_scroll);
struct TextCut {
    std::string text;
    std::string shown;
};
struct PageScroll {
    std::string page;
    float max_scroll = 0.0f;
};
const std::vector<TextCut>& cuts();
const std::vector<PageScroll>& page_scrolls();
void clear_settings_fit(); // the audit's own, at the start of a frame

// Registers an icon button for this frame: `min`..`max` is the button's body in ImGui points, the
// box the icon should sit centred in (the pill, circle or square it is drawn on; the nominal
// square where it has none). `check_y` false leaves the vertical axis out, for an icon above a
// caption. A no-op unless an audit run is on.
void note(const char* name, ImVec2 min, ImVec2 max, InkKind kind = InkKind::Centred,
          bool check_y = true);

} // namespace etude::app::ui::audit

namespace etude::app {

// The frames between one scenario action (a tap, the wheel) and the next.
inline constexpr long kTapGapFrames = 20;

class IconAudit {
  public:
    // Takes one of the options above and its value; false if it is not one of them.
    bool parse(const char* option, const char* value);
    bool active() const { return !tsv_.empty(); }
    float ui_scale() const { return ui_scale_; }

    // Call at the top of each frame, before the events are polled and the UI drawn.
    void before_frame(SDL_Window* window, long frame);
    // Call after the UI is drawn, before presenting. True when the audit is over and the app should
    // exit.
    bool after_draw(SDL_Renderer* renderer, long frame);

  private:
    // Settling: the UI comes to rest. Verifying: the frame is taken again until it is the same, so a
    // spring or a fade still moving is not measured. Hidden: the frame without the icons. Then for
    // each forced state, a plain frame (StateRef) and the same frame with every control in that
    // state (StateForced).
    enum class Phase {
        Settling,
        Verifying,
        Hidden,
        HiddenLabels,
        StateRef,
        StateForced,
        StateAfter
    } phase_ = Phase::Settling;
    int state_ = 0; // index into the forced states
    SDL_Surface* ref_ = nullptr;
    SDL_Surface* forced_ = nullptr;
    std::vector<ui::audit::Face> forced_faces_;
    std::vector<ui::audit::Box2> forced_inks_;
    std::vector<ui::audit::Hit> settled_hits_;
    std::vector<ui::audit::Hit> forced_hits_;
    std::vector<ui::audit::Label> settled_labels_;
    std::vector<ui::audit::Face> settled_faces_; // the faces the settled labels sit on
    std::vector<ui::audit::Chip> settled_chips_;
    std::vector<ui::audit::TextCut> settled_cuts_;
    std::vector<ui::audit::PageScroll> settled_scrolls_;
    void snapshot_settled();
    bool frames_match(const SDL_Surface* again) const;
    void tap_next(SDL_Window* window, long frame);
    bool unsettled_ = false; // the frame was still moving when the verify frames ran out
    void write_icons(std::FILE* out, const ui::PixelView& a, const ui::PixelView& b, float fb) const;
    void write_labels(std::FILE* out, const ui::PixelView& a, const ui::PixelView& b, float fb) const;
    void write_hits(std::FILE* out) const;
    void write_chips(std::FILE* out) const;
    void write_settings_fit(std::FILE* out) const;
    void write_faceless(std::FILE* out) const;
    void write_state(std::FILE* out, const SDL_Surface* after) const;
    long verify_frames_ = 0;
    using Seen = ui::audit::Note;
    std::string tsv_;
    std::string label_ = "audit";
    std::string png_;
    std::vector<std::string> taps_;
    float wheel_ = 0.0f; // --audit-wheel: clicks to scroll once the taps are done
    bool wheeled_ = false;
    float ui_scale_ = 1.0f;
    std::size_t next_tap_ = 0;
    long last_action_frame_ = 0;
    Uint64 last_action_ms_ = 0; // the start, or the last tap
    std::vector<Seen> seen_;       // the buttons of the frame before
    std::vector<Seen> settled_;    // the buttons of the settled frame
    SDL_Surface* with_ = nullptr;  // the settled frame
};

} // namespace etude::app
