// SPDX-License-Identifier: GPL-3.0-or-later
// The touch UI's design tokens and the widgets the bar styles and the settings sheet are built
// from, drawn with Dear ImGui.
//
// Tokens. Every size is in design points; `Kit::u` turns them into ImGui points, so a denser
// display gets proportionally larger controls. The drawn controls are compact, their hit areas are
// not: every tappable thing is at least `kHit` tall (about 11.6 mm at 96 dpi) even where it draws
// smaller.
#pragma once

#include "../port_lists.hpp"
#include "../ui_model.hpp"
#include "add_songs_layout.hpp"
#include "face_layout.hpp"
#include "fit_text.hpp"
#include "ink_measure.hpp"
#include "morph.hpp"
#include "reset_to.hpp"
#include "segment_layout.hpp"
#include "tap.hpp"
#include "tokens.hpp"

#include "etude/scroll_drag.hpp"
#include "etude/slider_drag.hpp"
#include "etude/toast.hpp"
#include "etude/touch_gesture.hpp"
#include "etude/unit_format.hpp"

#include <array>
#include <functional>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct SDL_Renderer;

namespace etude::app {
class SheetPreview;
struct ProfileInfo;
}

namespace etude::app::ui {

class LivePreview;
class MedalAtlas;
class InputPort;
class SongPort;
class SectionsPort;
class UploadPort;
class SourcesPort;
class AboutPort;
class IconPort;

// Tokens (sizes, type, motion) are in tokens.hpp.

// Colours: two surface tones, the state fills, one accent, three text tones, one warning. The
// selected state is the accent with a hairline round it, so it never reads as a lit right hand (whose
// default colour is the same blue): a lit hand is its own colour with no line and its L or R mark.
namespace col {
inline constexpr ImVec4 kScrim{0.016f, 0.024f, 0.04f, 0.46f};
// A screen of its own (the library, Learn): the scene's darkness, opaque, so nothing of the notes
// shows through.
inline constexpr ImVec4 kScreenBg{0.035f, 0.043f, 0.065f, 1.0f};
inline constexpr ImVec4 kSurface{0.086f, 0.098f, 0.133f, 0.975f}; // the sheet, the bar, cards
inline constexpr ImVec4 kRaised{0.125f, 0.141f, 0.188f, 1.0f};    // toasts, tiles, popover rows
inline constexpr ImVec4 kFill{1.0f, 1.0f, 1.0f, tok::kFillAlpha}; // the idle face, tracks
inline constexpr ImVec4 kHover{1.0f, 1.0f, 1.0f, tok::kHoverAlpha};     // a mouse over it
inline constexpr ImVec4 kPressed{1.0f, 1.0f, 1.0f, tok::kPressedAlpha}; // a finger on it
inline constexpr ImVec4 kHairline{1.0f, 1.0f, 1.0f, 0.08f};
inline constexpr ImVec4 kAccent{0.353f, 0.667f, 1.0f, 1.0f}; // #5AAAFF: selection, primary, focus
inline constexpr ImVec4 kSelected{0.353f, 0.667f, 1.0f, tok::kSelectedFill};
inline constexpr ImVec4 kSelectedLine{0.353f, 0.667f, 1.0f, tok::kSelectedLine};
inline constexpr ImVec4 kAccentInk{0.031f, 0.055f, 0.094f, 1.0f}; // content on the accent
inline constexpr ImVec4 kText{0.933f, 0.949f, 0.98f, 1.0f};
inline constexpr ImVec4 kText2{0.604f, 0.643f, 0.722f, 1.0f};
inline constexpr ImVec4 kText3{0.42f, 0.455f, 0.533f, 1.0f};
inline constexpr ImVec4 kWarning{1.0f, 0.45f, 0.45f, 1.0f};
// The names the settings pages and the editor still use, for the roles above.
inline constexpr ImVec4 kSurfaceHi = kRaised;
inline constexpr ImVec4 kDanger = kWarning;
} // namespace col

ImVec4 with_alpha(ImVec4 c, float a);
ImVec4 mix(ImVec4 a, ImVec4 b, float t);

// --- Motion ------------------------------------------------------------------------------------

float ease_out(float t);
// Turns every motion below into a jump (reduced effects). Set once per frame.
void set_reduced_motion(bool reduced);
// Moves linear progress `p` toward `target` (0 or 1) over `duration` seconds.
float approach(float p, float target, float dt, float duration);
// A per-widget float that eases toward `target` (stored in ImGui's state storage under `id`).
float tween(ImGuiID id, float target, float duration);

// --- Kit ---------------------------------------------------------------------------------------

// The toast on screen (core ToastState: one at a time, a fixed buffer) and where it points.
struct Toast {
    ToastState state;
    // The Undo of a toast that is not a section edit's (a saved palette replaced or cleared): what
    // its chip does. Its toast's token has `kUndoOwnBit` set (see `Kit::say_undoable`).
    std::function<void()> undo;
    std::uint32_t undo_serial = 0;
    bool anchored = false; // near the control that caused it
    ImVec2 anchor_min{0, 0};
    ImVec2 anchor_max{0, 0};
};

// Whether the open song has sheet music: the host sets it every frame for the view switch.
struct SheetChoice {
    bool song = false;    // a song is open
    bool known = false;   // the answer is in
    bool offered = false; // it has sheet music
    std::string_view reason; // why not, when known and not offered
    bool refused() const { return song && known && !offered; }
};

struct Kit {
    float u = 1.0f;            // ImGui points per design point
    ImFont* regular = nullptr; // Atkinson Hyperlegible with Lucide merged in
    ImFont* bold = nullptr;
    ImFont* music = nullptr;   // the music font (Noto Music): the notes and the clef of the title medals' emblems
    float dt = 0.016f;
    double now = 0.0;        // seconds
    std::int64_t now_ns = 0; // the same, for SongPort calls
    bool touch = false;      // the last pointer event came from a finger: no hover states
    float top_row_bottom =
        0.0f;                // where the top row (the status text, the gear) ends; the host sets it
    Toast toast;             // transient feedback (see `say`)
    // What the toast keeps clear of and where it may go (core place_toast). The host clears it at the
    // start of every frame; the sheet, the editor and the tempo popover add their rects as they
    // draw, and the host fills in the rest before the toast is drawn.
    ToastRegions toast_regions;
    // A warning found no room for its words and shows as the icon alone. The host then hides a bar
    // that is shown but not pinned (as it does after its timeout), which frees the status row. Set
    // when the toast is drawn, read by the next frame.
    bool toast_needs_room = false;
    // Where the Undo chip of the toast is, in ImGui points as the last frame drew it (empty when
    // there is none): a finger that lands there is the chip's, and a screen reading raw finger
    // events (the section editor) leaves it out.
    RectF toast_chip;
    // Keeps the toast clear of `r` (core ToastRegions::add_avoid): a list that cannot take it fails
    // placement closed, and asserts here.
    void toast_avoid(const RectF& r);
    SheetChoice sheet;
    // The section band is drawn in its slim form (a short or very dense window with the bar
    // pinned, see core BarFit); the host sets it every frame.
    bool slim_band = false;
    // The band the icon controls drawn now may fill, top and bottom in points: the card or pill
    // they sit on (see `FaceBand`). Empty (`face_top >= face_bottom`): a control's own hit area.
    float face_top = 0.0f;
    float face_bottom = 0.0f;

    // A label for the icon-only control under a long press, drawn on top by `finish_frame`.
    std::string tip;
    ImVec2 tip_min{0, 0};
    ImVec2 tip_max{0, 0};
    // The open (i) bubble.
    ImGuiID info_id = 0;
    std::string info_text;
    ImVec2 info_anchor{0, 0};
    bool info_seen = false;
    bool info_in_settings = false; // opened from a Settings page: it keeps its size (the popup rule)
    bool in_settings = false;      // the Settings sheet is drawing its pages
    // A destructive action waiting for its second tap.
    ImGuiID confirm_id = 0;
    double confirm_until = 0.0;

    bool reduced_motion = false; // LookSettings::reduced_effects: no UI animation either

    // The song and its sections, for pages and the section band (the host sets them every frame).
    SongPort* song = nullptr;
    SectionsPort* sections = nullptr;
    // The look's hand colours, which the hand toggles light up in.
    ImVec4 hand_left{1.0f, 0.663f, 0.302f, 1.0f};
    ImVec4 hand_right{0.353f, 0.667f, 1.0f, 1.0f};
    // The section band asked for the full section editor; the host opens it.
    bool open_editor = false;
    // Settings > Input's "Run the device check" was tapped; the host takes it as the card's answer.
    bool run_device_check = false;

    // The live look preview (pages draw it; the host owns it) and what it needs to render.
    LivePreview* preview = nullptr;
    // One preview for each saved config's card (the Configs page), so the three can show at once:
    // a preview renders into its own target.
    std::array<LivePreview*, kUserPresetSlots> config_previews{};
    // The household, for the Configs page's copy row (null when the app has none).
    const ProfileInfo* profiles = nullptr;
    // The medals' texture atlas (the host owns it; null in a test that draws no medal).
    MedalAtlas* medals = nullptr;
    // The library's own, for a song's opening bars (its Scene is made the first time it draws).
    LivePreview* library_preview = nullptr;
    // And its sheet music (its worker starts the first time it is shown).
    SheetPreview* library_sheet = nullptr;
    // The microphone, for the Input pages; the host sets it every frame.
    InputPort* input = nullptr;
    // The MIDI ports the watcher thread listed, for the Input page's port rows; null: only Auto.
    const PortLists* midi_ports = nullptr;
    // The phone upload server, for the Add songs page; null when the app has none.
    UploadPort* upload = nullptr;
    // The network folder and the USB stick, for the same page; null when the app has none.
    SourcesPort* sources = nullptr;
    // The update check and the links, for the About page; null when the app has none.
    AboutPort* about = nullptr;
    // The best title on the device, for the About page's icon colours; null: only the default.
    IconPort* icons = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool high_contrast = false; // the look's High contrast: the grade colours lift with it
    float fb_scale = 1.0f;     // output pixels per ImGui point
    bool short_screen = false; // under 600 points tall: pages trim what they can

    // Drag to scroll. `slider_active`: a slider is following the touch this frame, so the touch is
    // the slider's, not the sheet's.
    bool slider_active = false;
    // A settings slider's track is held by a finger or the mouse this frame (a press that has not
    // moved yet counts). The frame loop reads it through Ui::slider_held().
    bool slider_held = false;
    SliderDrag slider;
    ScrollDrag scroll;         // the touch that scrolls the sheet, and its coasting
    ImGuiID scroll_window = 0; // the child window `scroll` is following, 0 for none
    int scroll_frame = -1;     // the last frame that window called drag_scroll

    float s(float v) const { return v * u; }

    // Shows `text` as the toast, replacing the one on screen, near the control just drawn (the last
    // item) when called right after it, else centred under the bar. Empty text does nothing.
    // `warning` keeps it up longer (something the player must not miss). The one way to give
    // transient feedback; the status line keeps only lasting state.
    void say(std::string_view text, bool warning = false);
    void say_free(std::string_view text, bool warning = false);
    // The words of a section edit just made (`sections->take_message()`), as a toast near the
    // control just drawn. `before` is `sections->edit_serial()` read before the edit: when it moved,
    // the edit went through and the toast carries an Undo that takes it back, for as long as the
    // toast shows and nothing else has been edited or undone since; otherwise (a refused edit) it
    // is a plain message.
    void say_edit(std::uint32_t before);
    // Like `say`, with an Undo chip that runs `undo` (once) while the toast shows; the next toast
    // takes the offer away. For a change that went through at once and is not a section edit.
    void say_undoable(std::string_view text, std::function<void()> undo);
    // Like `say_free`, but from outside a frame: it starts on the next one. `name` is the part of
    // the text that is a song's title (see `TextSpan`).
    void say_later(std::string_view text, bool warning = false, TextSpan name = {},
                   SongLines lines = {});
    // Starts a toast queued by `say_later`.
    void start_pending_toast();
};

// Keeps the faces and captions of the icon controls drawn in its scope inside `top`..`bottom` (a
// group's card, the pill under the bar), `kFaceInset` clear of its edges (face_layout.hpp).
class FaceBand {
  public:
    FaceBand(Kit& k, float top, float bottom) : k_(k), top_(k.face_top), bottom_(k.face_bottom) {
        k.face_top = top;
        k.face_bottom = bottom;
    }
    ~FaceBand() {
        k_.face_top = top_;
        k_.face_bottom = bottom_;
    }
    FaceBand(const FaceBand&) = delete;
    FaceBand& operator=(const FaceBand&) = delete;

  private:
    Kit& k_;
    float top_;
    float bottom_;
};

// An icon control's face and caption (face_layout.hpp) for a control centred on `centre` with a
// `hit` area: inside the current `FaceBand`, or the hit area without one. The face is asked
// `face_w` by `face_h` (square when they are equal); `caption` (may be null) is measured, drawn
// smaller when its cell is too narrow, and dropped when it cannot fit.
struct FacePlan {
    FaceLayout lay;
    float caption_size = 0.0f; // 0 when there is no caption
};
FacePlan plan_face(const Kit& k, ImVec2 centre, ImVec2 hit, float face_w, float face_h,
                   const char* caption);
// The area `plan_face` lays a control out in.
Box face_area(const Kit& k, ImVec2 centre, ImVec2 hit);

// --- States --------------------------------------------------------------------------------------
// One helper draws every control's state, inside its face and nowhere else:
//   idle      the face's idle fill (`Idle`), or nothing for an icon button;
//   hover     `col::kHover` over it, from a mouse only, never a finger;
//   pressed   `col::kPressed` from the frame the finger lands (no ease in), the content brighter;
//             it fades out over `tok::kFast`;
//   selected  the accent at `tok::kSelectedFill` with a hairline of it, the content in the accent;
//             a lit hand is its own colour (`tint`) with no line;
//   focus     a `tok::kFocusRing` accent ring inset `tok::kHairline` in the face;
//   disabled  no hover or pressed, the content at `tok::kDisabledAlpha`.
struct State {
    bool hover = false;
    float pressed = 0.0f;  // 1 while held, then fading
    float selected = 0.0f; // eased 0..1
    bool focus = false;
    bool disabled = false;
    const ImVec4* tint = nullptr; // a hand's colour: lit in it, no line
};
enum class Idle : std::uint8_t {
    None,   // an icon button: nothing until a state
    Fill,   // a filled control: `col::kFill`
    Accent, // the primary action: filled with the accent
};
// The state of the item just drawn (ImGui's last item): hover from a mouse only, pressed from its
// first frame, `selected` eased toward `on`, `disabled` as given; and the audit's forced state.
// `live` false (a bar still fading in) takes no hover or press.
State item_state(const Kit& k, bool on, bool live = true, bool disabled = false,
                 const ImVec4* tint = nullptr);
// Draws `s` on `face` with corners `radius` (0: the kit's `tok::kRadiusControl`, never more than
// half the face). `area` is what the face must stay inside (its card, pill or hit area), for the
// audit; `name` names the control there.
void draw_state(const Kit& k, ImDrawList* dl, const char* name, const Box& face, const Box& area,
                const State& s, Idle idle = Idle::None, float radius = 0.0f);
// The content colour in state `s`: `base`, the accent (or the hand's colour) as it is selected,
// brighter while pressed, faded when disabled. On the accent fill (`Idle::Accent`) pass
// `col::kAccentInk` and `on_accent`.
ImVec4 state_ink(const State& s, ImVec4 base, bool on_accent = false);
// The corner radius of a face `w` by `h`: `radius` (0 for the control radius), at most half the
// shorter side.
float face_radius(const Kit& k, const Box& face, float radius = 0.0f);

// While the item just drawn is held past the long-press time, `label` (may be null) shows over it.
void long_press_tip(Kit& k, const char* label);
// Draws the long-press label and the (i) bubble on top of everything. Call once at the end.
void finish_frame(Kit& k);

// --- Drawing -----------------------------------------------------------------------------------

ImU32 u32(ImVec4 c); // multiplied by the current style alpha

// A control's icon: a font glyph (a string from icons.hpp) or a drawn icon that morphs into the
// next one the control shows.
struct Icon {
    const char* glyph = nullptr;
    MorphGlyph morph = MorphGlyph::Count;
    Icon(const char* g) : glyph(g) {} // NOLINT(google-explicit-constructor): any icon string
    Icon(MorphGlyph m) : morph(m) {}  // NOLINT(google-explicit-constructor)
};
void glyph(const Kit& k, ImDrawList* dl, const char* icon, ImVec2 centre, float size,
           ImVec4 colour);
// Where the ink of one character lies, from the pen position the text is drawn at (`min` the top
// left, `max` the bottom right), at the font size `size`: measured from the glyph's bitmap, so a
// letter can be placed by its ink. False when the font has no such glyph.
bool font_ink(ImFont* font, float size, char c, ImVec2* min, ImVec2* max);
// The drawn left and right hands side by side (the same glyphs as the bar's hand toggles), for a
// row or card that speaks of both. `size` is one hand's; the pair is `hand_pair_width(size)` wide
// and centred on `centre`. `left` and `right` choose which are drawn (one alone sits in its own
// slot, so a row's text starts at the same x either way).
void hand_pair(const Kit& k, ImDrawList* dl, ImVec2 centre, float size, ImVec4 colour,
               bool left = true, bool right = true);
float hand_pair_width(float size);
// `text` as the screen shows it: a note named in it ("Prelude in Bb", "C#4") carries the signs ♭
// and ♯, never the letter b or a number sign (core `accidental_display_text_into`). Text with no
// note name comes back as it was (no copy); the rest is written into one of a few strings kept
// between calls, valid until the eighth call after it. Every text the kit draws or measures goes
// through it (`text_at`, `text_size`, `label_at`, `label_stack`, `fit`), so the same words measure
// and draw alike; text drawn straight with `AddText` calls it first.
const char* screen_text(const char* text);
std::string_view screen_text(std::string_view text);
void text_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImVec4 colour,
             const char* text);
ImVec2 text_size(ImFont* font, float size, const char* text);
// `text_size(...).x`, kept per font, size and words until the fonts are rebuilt (`forget_fonts`), for
// text measured every frame that seldom changes.
float text_width(ImFont* font, float size, const char* text);
// A label placed by its ink, never by its font's line box (which holds room for descenders: a
// word without them, "All" or a number, would sit high). Vertical: the capitals' band is centred
// on the slot's middle. Horizontal, by `align`: `Left` starts the pen at the slot's left edge,
// `Centre` centres the visible letters (the advance less the side bearings of the first and last
// letter), `Right` puts their right edge at the slot's right edge. Every text in a control goes
// through this; the icon audit leaves these labels out of its second frame and measures where they
// were (icon_audit.hpp).
// `slot` is the control's face (or a row's, or one line's box), in ImGui points.
LabelMetrics label_metrics(ImFont* font, float size, const char* text);
// The width `label_at` draws `text` at: a size under 40 pt is rounded to a whole point there, so
// anything placed against the end of a label (an info button) measures with this, not `text_size`.
float label_width(ImFont* font, float size, const char* text);
void label_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
              LabelAlign align, ImVec4 colour, const char* text);
// The pen position `label_at` would draw at, for text drawn another way (a gradient's).
ImVec2 label_pen(ImFont* font, float size, const Box& slot, LabelAlign align, const char* text);
// Two lines, one over the other, left at `slot.x0`, as one block centred vertically in `slot` (a
// title over its detail). `bottom` may be null for one line.
struct StackLine {
    ImFont* font = nullptr;
    float size = 0.0f;
    ImVec4 colour;
    const char* text = "";
};
// `extra_gap` opens the two lines further apart. `bands`, when given, gets the middle of each line's capital band (what an icon beside the line
// centres on).
struct StackBands {
    float top = 0.0f;
    float bottom = 0.0f;
};
void label_stack(const Kit& k, ImDrawList* dl, const Box& slot, const StackLine& top,
                 const StackLine* bottom, StackBands* bands = nullptr, float extra_gap = 0.0f);
// `text` cut to `max_w` with an ellipsis. Cached per (font, size, width, text), so a label that did
// not change is neither measured nor copied into the heap.
FitText fit(ImFont* font, float size, std::string_view text, float max_w);
// `fit` without telling the audit about a cut: for a caller that cuts part of a label and reports the
// whole label itself (`row_label`).
FitText fit_unreported(ImFont* font, float size, std::string_view text, float max_w);
// Empties the caches keyed by a font's address (ink, capital band, fitted lines, label metrics): a
// new UI's fonts may land at an address an old one had, and its text must be measured, and its
// glyphs baked, as a first run does. Called as the fonts are built (`add_ui_fonts`) and when the UI
// goes.
void forget_fonts();
// Where a card may draw. `Current`: inside the list's clip (a scrolling page's own), widened only
// by the card's shadow, and not past the clip of the window around it, so a card in a page that
// scrolls never paints over the sheet's header or the top bar. `Screen`: the whole screen, for a
// popover or a sheet whose card and shadow reach past the window they are drawn in.
enum class CardClip : std::uint8_t { Current, Screen };
// A soft shadow, then the surface, then a hairline and a faint top highlight.
void card(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius,
          ImVec4 fill = col::kSurface, ImDrawFlags corners = ImDrawFlags_RoundCornersAll,
          float shadow = 1.0f, CardClip clip = CardClip::Current);
// A card for a popover, a sheet or a toast: drawn on the whole screen (`CardClip::Screen`), because
// it and its shadow reach past the window it is drawn in.
inline void overlay_card(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius,
                         ImVec4 fill = col::kSurface,
                         ImDrawFlags corners = ImDrawFlags_RoundCornersAll, float shadow = 1.0f) {
    card(k, dl, a, b, radius, fill, corners, shadow, CardClip::Screen);
}
// The same for a sheet or a dialog: the deeper shadow they all use.
inline void sheet_card(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius,
                       ImVec4 fill = col::kSurface) {
    card(k, dl, a, b, radius, fill, ImDrawFlags_RoundCornersAll, 1.4f, CardClip::Screen);
}
void shadow(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float strength);
// A small preview of one note style, painted from its recipe at `glow` (the block glow): a
// white-key block and a narrower, darker, fuller black-key block.
void note_preview(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, NoteStyle style, RGBA colour,
                  float intensity, int glow = kDefaultBlockGlow);

// --- Controls ----------------------------------------------------------------------------------
// All of these lay out at ImGui's cursor and take the full content width unless a width is given.

// The always-visible settings button: icon only, no fill, no border. `active` while settings are
// open, when the gear turns into a cross. Pressed: a little smaller and brighter. Returns true on a
// tap.
bool gear(Kit& k, ImVec2 centre, float hit, bool active, float alpha);

// An icon in a square hit area at `centre`. `on` draws the selected state (accent icon on a soft
// accent square); `caption` (may be null) is drawn under the icon; `label` shows on long press.
// `live` false: drawn but not pressable (a bar that is still fading in).
// `tint` (may be null) is the colour the on state lights up in instead of the accent;
// `shake_x` moves the icon sideways (a refused tap); `hit_dy` moves the hit area down from the icon
// (a touch target kept clear of the controls above it).
bool icon_toggle(Kit& k, const char* id, Icon icon, const char* label, ImVec2 centre, ImVec2 hit,
                 float drawn, bool on, bool live, const char* caption = nullptr,
                 const ImVec4* tint = nullptr, float shake_x = 0.0f, float hit_dy = 0.0f,
                 float glyph_dy = 0.0f);

// A text chip: a pill with an optional icon, words and an optional trailing icon. The kit sizes it:
// a face `tok::kFace` tall in a `tok::kHit` hit area, at least `tok::kChipMinW` wide with words
// (`chip_width`; an icon alone is round, `tok::kHit` wide), the group centred in the face and every
// letter placed by its ink. The caller passes the position and the words; `ChipOpts` only says
// what is special about this one. Returns true on a tap.
using ChipIconFn = void (*)(const Kit& k, ImDrawList* dl, const char* icon, ImVec2 centre,
                            float size, ImVec4 colour, bool on);
struct ChipOpts {
    const char* icon = nullptr;
    const char* trail = nullptr; // a trailing icon (a sort's arrow)
    bool on = false;             // selected
    bool live = true;            // false: drawn but not pressable (a bar still fading in)
    float width = 0.0f;          // wider than its words want (a cell of a grid)
    float hit_h = 0.0f; // a bar's cell: its hit height and the face it leaves (0: the kit's)
    float face_h = 0.0f;
    ChipIconFn draw_icon = nullptr; // an icon the font does not have (the library's heart)
    // The words in the body size, bold: a panel of choices that are the point of its card. Give it
    // a `width` (`chip_width` measures the label size).
    bool large = false;
    float label_pt = 0.0f; // the words' size in design points (0: the kit's for the chip)
    // A colour of its own for the chip: its icon and words in it, a selected one lit in it (as a
    // hand's toggle is), where it is not the accent. Null: the kit's.
    const ImVec4* tint = nullptr;
    // With `tint`: a selected chip is filled white with dark words, and only its icon keeps the
    // tint (a little darker, to read on the white); unselected, it is as a tinted chip is.
    bool solid = false;
    // Exactly `width` wide, whatever the words: they take the room the cell leaves (a smaller side
    // pad, then an ellipsis) instead of growing the chip into its neighbours. A row of chips in a
    // fixed room.
    bool fixed = false;
    // A second line under the text (a long name wrapped at its space): both lines are centred
    // in the face, one over the other, each in its own half of it. Words only, no icon.
    std::string_view second = {};
};
float chip_width(const Kit& k, const char* icon, std::string_view text,
                 const char* trail = nullptr);
// The same for words at `label_pt` design points (0: the kit's size), so a chip of a larger popup
// holds its words whole.
float chip_width_at(const Kit& k, const char* icon, std::string_view text, float label_pt);
// A popup's button at `wide_step` `step` (tok::popup_type): the words a type step
// up, the face and hit area taller, wide enough for its words; step 0 is the kit's own chip.
ChipOpts popup_button(const Kit& k, int step, const char* icon, std::string_view text);
bool chip(Kit& k, const char* id, std::string_view text, ImVec2 pos, const ChipOpts& o = {});
// A pressable area whose face the caller draws (a card or a row, with `item_state` and
// `draw_state` right after); true when it was pressed. The caller notes it for the audit.
bool press_area(Kit& k, const char* id, const Box& area);

void section(Kit& k, const char* text);
bool switch_row(Kit& k, const char* icon, const char* label, bool* value,
                const char* info = nullptr);
bool nav_row(Kit& k, const char* icon, const char* label, std::string_view value,
             const char* info = nullptr);
bool radio_row(Kit& k, const char* label, const char* detail, bool on, bool dim = false);
// The same, true for a tap on the chosen row too (a choice that greys, and says why on any tap).
bool radio_choice(Kit& k, const char* label, const char* detail, bool on, bool dim = false);
// A row like a switch row, with the current choice written in a small pill at the right instead
// of a knob; a tap anywhere on the row returns true and the caller moves to the next choice. For a
// setting whose choices would not fit as a segmented control on one row.
bool cycle_row(Kit& k, const char* icon, const char* label, const char* value,
               const char* info = nullptr);

struct Seg {
    const char* icon = nullptr;  // may be null
    const char* label = nullptr; // may be null when there is an icon
};
// One row: an optional icon and label on the left, the segmented control beside it (or the full
// width when `label` is null). Returns true when the choice changed. A drawn `icon` is expected to
// change with the choice and morphs. Where the control starts is the column's (row_columns.hpp).
// `info` puts an (i) button after the label, as a switch row's, saying what the setting does.
bool segmented(Kit& k, const char* id, Icon icon, const char* label, std::span<const Seg> items,
               int* current, const char* info = nullptr);
// The same with the cells in the bit set `disabled` drawn faint: a tap on one changes nothing and
// sets `*refused` to its index (the caller says why).
bool segmented(Kit& k, const char* id, Icon icon, const char* label, std::span<const Seg> items,
               int* current, std::uint32_t disabled, int* refused, const char* info = nullptr);
// A row of tab cells that share one track, drawn as `segmented` draws its cells: the caller lays the
// cells out side by side (equal or not) with their hit areas' dead gap inside them, calls `tab_track`
// once, then `tab_cell` for each cell and puts the cell's words or icon in the pill it returns.
// The cells have no fill of their own, so the track reads as one smooth bar; the chosen cell is one
// raised pill that slides to the cell tapped, inset evenly (segment_layout.hpp).
// `cells` are the cells in order, `chosen` the index of the selected one (-1 none).
// Returns the cell whose words are bold this frame (-1 none): the one nearest the sliding pill's
// centre, so each word changes face once as the pill passes the middle between two cells.
int tab_track(Kit& k, ImDrawList* dl, const char* id, std::span<const Box> cells, int chosen);
struct TabHit {
    bool tapped = false;
    Box pill;   // the face inside the track: where the cell's icon and words are centred
    State state;
    float weight = 0.0f; // how chosen the cell reads (0..1), fading with the pill's slide
};
// One cell's hit area (the cell less the dead gap), its hover and press, and the audit's face; true
// in `tapped` on a lift inside it, the chosen cell too. `on` is the chosen cell.
TabHit tab_cell(Kit& k, ImDrawList* dl, const char* id, const Box& cell, bool on);
// The chosen pill's slide, shared by every track-and-pill control (`segmented`, `tab_track` and the
// review tabs; the pure steps are in segment_layout.hpp). `key` is the control's ID, `cell` the chosen
// cell, `row` its row; `pill` is where the pill belongs this frame. Returns where to draw it: on a
// change of cell its x and width ease from where it is to the new cell in `tok::kPillSlide`, ending
// exactly on it. No slide on the first draw, for a control not drawn the frame before, with Reduced
// effects, or to another row. The loop keeps drawing while the picture moves and idles once it lands.
Box slide_pill(Kit& k, ImGuiID key, int cell, int row, const Box& pill);
// How chosen cell `cell` of the control `key` reads, 0..1, fading over the same time and curve as
// the pill; `on` is whether it is the chosen cell.
float cell_weight(Kit& k, ImGuiID key, int cell, bool on);
// A cell's words while the pill slides: one copy, in the bold face when `bold` and the regular one
// when not, the ink fading from `ink_off` to `ink_on` with `weight` (a frame never draws two faces
// of one word).
void fade_label(Kit& k, ImDrawList* dl, float size, const Box& slot, LabelAlign align,
                const char* text, bool bold, float weight, ImVec4 ink_off, ImVec4 ink_on);
// A slider; the value shown is `*value * shown_scale` in `unit` with `decimals` places
// (unit_format.hpp). `reset_to` is the setting's own default: with it the row has a reset button at
// its trailing end, greyed and doing nothing while the value is the default, active once it differs,
// and a tap on it puts the default back (reset_to.hpp). The default comes from where the setting
// defines it, never a second copy of the number.
bool slider_row(Kit& k, const char* icon, const char* label, float* value, float lo, float hi,
                float shown_scale, Unit unit, int decimals = 0, const char* info = nullptr,
                std::optional<ResetTo<float>> reset_to = std::nullopt);
bool slider_row_int(Kit& k, const char* icon, const char* label, int* value, int lo, int hi,
                    Unit unit, const char* info = nullptr,
                    std::optional<ResetTo<int>> reset_to = std::nullopt);
// A number with a minus and a plus button beside the label, one per tap, kept in `lo`..`hi`;
// `unit` follows the number. Never stacks. `skip_dir` -1 or 1 draws a skip arrow (back or forward)
// with the value inside instead of `icon`; its number morphs as the value changes.
bool stepper_row(Kit& k, const char* icon, const char* label, int* value, int lo, int hi,
                 Unit unit, int skip_dir = 0, std::optional<ResetTo<int>> reset_to = std::nullopt);
// The same with a value that is words (a key's name): the text between the buttons, each button
// greyed when its direction is shut. Returns -1 or 1 for the button tapped, else 0.
int stepper_text_row(Kit& k, const char* icon, const char* label, const char* text, bool can_minus,
                     bool can_plus);
// The Phone row of the Add songs page, laid out by `g` (add_songs_layout.hpp) from the cursor: an
// icon, the title over a detail line (in `detail_ink`), and a big switch right after them. A tap
// anywhere on the row turns the switch. True when it was tapped.
bool switch_row_detail(Kit& k, const char* icon, const char* label, const char* detail,
                       ImVec4 detail_ink, bool* value, const ToggleRowGeometry& g);
// A tile with an icon, a title and one line, at `area` from `origin`: a place the page leads to.
// `muted` draws it faint (not set up yet). True on a tap.
bool detail_tile(Kit& k, const char* id, const char* icon, const char* title, const char* detail,
                 ImVec2 origin, const Rect& area, bool muted);
// A paragraph at the cursor, under the words of a row (past its icon), broken at spaces into at
// most `max_lines` lines (the rest is dropped: the caller keeps its text short).
void help_text(Kit& k, const char* text, ImVec4 ink, int max_lines);
// How tall `help_text` is for `text` in a row `width` wide.
float help_text_height(Kit& k, const char* text, float width, int max_lines);
// A row of the same height with an icon and words and nothing to press, for what a setting
// currently is.
void readout_row(Kit& k, const char* icon, const char* text);
// The header of a group that opens and closes, in two steps so the group can be sized from its
// state after a tap before anything is drawn. The box at `pos`, `size` big, is the hit; the caller
// draws its label (no wider than `disclosure_label_right`) and the group. The open state is kept
// per `id` (ImGui's state storage), `default_open` until first changed, so every caller has its
// own.
struct Disclosure {
    ImGuiID id = 0;
    bool open = true; // after this frame's tap
    bool held = false;
    bool hover = false;
};
// Reads the tap, flips the stored state on one, and returns it.
Disclosure disclosure_tap(Kit& k, const char* id, ImVec2 pos, ImVec2 size,
                          bool default_open = true);
// Draws the chevron at the right end of the box: pointing right when shut, a quarter turn to down
// when open, turning on a spring.
void disclosure_chevron(Kit& k, const Disclosure& d, ImVec2 pos, ImVec2 size);
// The x a label must stay left of, so it never runs under the chevron.
float disclosure_label_right(const Kit& k, ImVec2 pos, ImVec2 size);
// An icon button with a long-press label, `size` square hit, drawn smaller. `danger` turns red.
bool icon_action(Kit& k, const char* id, const char* icon, const char* label, float size,
                 bool enabled, bool danger = false, bool armed = false);
// A full-width quiet button: icon + text. A second tap within 3 s is needed when `confirm`.
bool text_button(Kit& k, const char* id, const char* icon, const char* label, bool confirm,
                 const char* confirm_label = nullptr);
// A button with an icon and a caption at `pos`, `size` big (the touch target; the face is a little
// smaller). `primary` fills it with the accent, `danger` tints it red. With `confirm`, a second
// tap within 3 s is needed and the caption changes to `confirm_label`. Returns true on a tap; a
// disabled one never does. Several can sit side by side, unlike text_button's full-width row. A
// bigger button gives its face (`face_h`, in design points, up to `size`) and its words (`label_pt`);
// its icon grows with the words. `icon_ink` (may be null) is a colour the icon takes: while the
// button is held, or always with `icon_ink_always`.
bool action_button(Kit& k, const char* id, const char* icon, const char* label, ImVec2 pos,
                   ImVec2 size, bool enabled, bool primary = false, bool danger = false,
                   bool confirm = false, const char* confirm_label = nullptr, float face_h = 0.0f,
                   float label_pt = 0.0f, const ImVec4* icon_ink = nullptr,
                   bool icon_ink_always = false);
// Sheet or card header: back chevron (when `back`), title, close (when `close`). Returns 1 for
// back, 2 for close, 0 otherwise.
int header(Kit& k, const char* title, bool back, bool close);
// Two-tap confirm state for `id`.
bool armed(const Kit& k, ImGuiID id);

// Drag anywhere to scroll the current child window (touch), except on a slider that took the
// touch. A drag starts after `kTouchSlopPt`; a finger's fast drag coasts on after the lift, its
// speed halving on the recogniser's schedule, and a new touch or either end of the page stops it
// at once. A mouse drag scrolls without coasting.
// `lock_axis`: a sideways drag is left to a row inside the window that scrolls sideways itself.
void drag_scroll(Kit& k, bool lock_axis = false);

} // namespace etude::app::ui
