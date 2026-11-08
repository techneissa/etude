// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/screen_layout.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace etude {

// The toast: one small floating pill for transient feedback (a mode change, a refused tap, a hint).
// One shows at a time; a newer message replaces the current one and starts its own clock. It never
// takes a touch, except two: a warning's icon (a long press shows its words) and the Undo of an edit
// (`ToastAction::Undo`).
//
// A toast shows for `kToastShowS` seconds and fades out over the last `kToastFadeS` of them; with
// reduced effects it stays at full opacity and disappears at the end. A problem the player should
// not miss (a settings file that could not be read, a failed save) stays `kToastWarnShowS`.
inline constexpr double kToastShowS = 1.5;
inline constexpr double kToastWarnShowS = 4.0;
// A toast that offers an action (Undo after an edit) stays until a finger has had time to reach it.
inline constexpr double kToastActionShowS = 5.0;
inline constexpr double kToastFadeS = 0.35;

// What a toast offers to do. Undo takes the edit the toast reports back; the host decides how long
// that stays possible (it dismisses the toast as soon as something else changed what Undo would
// undo).
// The token bit of an Undo that is not a section edit's (the host's own, such as a saved palette
// put back): the section edit counter never has it, so the two never read each other's offers.
inline constexpr std::uint32_t kUndoOwnBit = 0x80000000u;

enum class ToastAction : std::uint8_t {
    None,
    Undo,
};

// How long a toast stays: a warning its own time, an action's toast the time to reach it (the
// longer of the two when it is both).
constexpr double toast_show_s(bool warning, ToastAction action = ToastAction::None) {
    const double plain = action == ToastAction::None ? kToastShowS : kToastActionShowS;
    return warning && kToastWarnShowS > plain ? kToastWarnShowS : plain;
}

// How opaque a toast `age_s` seconds after it began is, 0 (gone) to 1. A negative age (a clock
// that stepped back) counts as the start.
float toast_alpha(double age_s, bool warning, bool reduced_effects,
                  ToastAction action = ToastAction::None);

// The toast on screen: its text in a fixed buffer (showing one never allocates), when it began and
// whether it is a warning. One at a time: `show` replaces whatever is there and starts a new clock.
// A part of a toast's text that is a name (a song's title): where a toast is too long for its room,
// the name is what is shortened, the words round it stay. Bytes of the text; empty for none.
struct TextSpan {
    std::size_t begin = 0;
    std::size_t size = 0;
};

// The song-opened toast: the text is the piece's name (bold, a little larger) and `detail` a second
// line in a quieter colour (the composer when known, else what the open noted); empty for none.
struct SongLines {
    bool on = false;
    std::string_view detail;
    bool composer = false; // `detail` starts with the composer, which gets its icon
};

// The message waiting for the next frame's toast. Several can be set in one frame and the last
// wins whole: a plain message clears the song form, so a song-opened toast's bold title and
// composer line never carry over to the message that replaced it.
struct PendingToast {
    std::string text;
    bool warning = false;
    TextSpan name;
    bool song = false;
    std::string detail;
    bool composer = false;

    bool empty() const { return text.empty(); }
    // A plain message (`name` the part of it that is a song's title).
    void set(std::string message, bool is_warning, TextSpan name_span = {}) {
        text = std::move(message);
        warning = is_warning;
        name = name_span;
        song = false;
        detail.clear();
        composer = false;
    }
    // The song-opened form: `title` bold, `second_line` under it.
    void set_song(std::string title, std::string second_line, bool is_warning,
                  bool has_composer = false) {
        text = std::move(title);
        warning = is_warning;
        name = {0, text.size()};
        song = true;
        detail = std::move(second_line);
        composer = has_composer;
    }
    SongLines lines() const { return {song, detail, song && composer}; }
    void clear() { *this = PendingToast{}; }
};

class ToastState {
  public:
    static constexpr std::size_t kMaxBytes = 191; // longer text is cut and ends in an ellipsis
    static constexpr std::size_t kMaxDetailBytes = 191;

    // Shows `text` from `now_s` on, `name` the part of it that is a name. Empty text changes
    // nothing.
    void show(std::string_view text, double now_s, bool warning, TextSpan name = {},
              SongLines song = {});
    // For a message that arrives between frames: held until `begin_frame` gives the frame's time,
    // which then is its start. A second `queue` before that replaces the first.
    void queue(std::string_view text, bool warning, TextSpan name = {}, SongLines song = {});
    void begin_frame(double now_s);

    // Opacity at `now_s` (see `toast_alpha`), 0 when there is none or it is over; once over, the
    // toast is gone and `active` is false.
    float alpha(double now_s, bool reduced_effects);
    // The toast offers `action` (set after `show`, on the toast just shown); `token` is what the
    // host will compare later to see whether the action is still possible (see `resolve_action`).
    // The next toast, of any kind, replaces it.
    void set_action(ToastAction action, std::uint32_t token = 0) {
        action_ = action;
        token_ = token;
    }
    ToastAction action() const { return active() ? action_ : ToastAction::None; }
    // The token the action was offered with.
    std::uint32_t action_token() const { return token_; }
    // The toast of an edit: it offers Undo when the edit went through, which is when the host's
    // edit counter moved from `before` to `after`; a refused edit leaves a plain message. The token
    // is the counter as the edit left it.
    bool offer_undo(std::uint32_t before, std::uint32_t after) {
        if (!active() || before == after) {
            return false;
        }
        set_action(ToastAction::Undo, after);
        return true;
    }
    // Once a frame, with the counter as it is now: an Undo offered for an edit that is no longer
    // the latest (something was edited or undone since) would take back something else, so the
    // toast goes with its offer. True when it did.
    bool resolve_action(std::uint32_t current) {
        if (action() == ToastAction::Undo && token_ != current) {
            dismiss();
            return true;
        }
        return false;
    }
    // Takes the toast away at once (its action is no longer possible, or it was used).
    void dismiss() {
        text_[0] = '\0';
        queued_ = false;
        action_ = ToastAction::None;
    }
    bool active() const { return text_[0] != '\0'; }
    bool queued() const { return queued_; }
    const char* text() const { return text_; }
    bool warning() const { return warning_; }
    // The name's part of `text()`, kept inside what the buffer kept (empty for none).
    TextSpan name() const { return name_; }
    // The song-opened form: `text()` is the name, `detail()` the second line ("" for none).
    bool song() const { return song_; }
    const char* detail() const { return detail_; }
    // The second line begins with a composer (the composer icon shows only then).
    bool composer() const { return song_ && composer_; }

  private:
    void put(std::string_view text, bool warning, TextSpan name, SongLines song);

    char text_[kMaxBytes + 1] = "";
    char detail_[kMaxDetailBytes + 1] = "";
    bool song_ = false;
    bool composer_ = false;
    double start_s_ = 0.0;
    bool warning_ = false;
    bool queued_ = false;
    ToastAction action_ = ToastAction::None;
    std::uint32_t token_ = 0;
    TextSpan name_;
};

// The regions a toast keeps clear of, and the places it may go. The host fills one every frame from
// the real rects (a fixed array, so a frame allocates nothing). All in window pixels.
//
// A toast never covers a header, a control the player is using, the bar or the falling notes:
// - `avoid`: rects that stay clear, whatever else is true: the open sheet's header and rail, the
//   section editor's toolbar and ruler, the tempo popover, the bar while it shows (its buttons and
//   the overview under them).
// - `notes`: the falling notes and the keyboard below them, from the bottom of the top row down. A
//   toast stays out of it, except inside `surface` (the open sheet or editor, which hides the notes
//   behind it: they are paused and dimmed there).
// - `slot`: the toast's own strip, just under the top row (and under the bar's band and the lane
//   while they show), where a toast with no control of its own goes, centred. It lies over the top
//   of the notes' area and a toast may stand in it; the top row's status line is never part of it.
//   `strip`: a free strip of the open sheet or editor (a footer), empty if none.
inline constexpr std::size_t kMaxToastAvoid = 16;

struct ToastRegions {
    RectF avoid[kMaxToastAvoid];
    // `give_way[i]`: the pill may slide along the slot to stand beside avoid[i] (see place_toast);
    // set by add_avoid_beside. Every other rect drops the message when it blocks the slot.
    bool give_way[kMaxToastAvoid] = {};
    std::size_t avoid_count = 0;
    // Set when a rect could not be kept (a non-finite one, or the list was full): nothing can be
    // placed then, since a toast must never go where a region it could not see is.
    bool invalid = false;
    // Unset (empty, or not finite) counts as the whole window, so a host that forgot it gets no
    // toast over the notes rather than one over everything.
    RectF notes;
    RectF surface;
    RectF slot;
    RectF strip;

    // Adds a rect to keep clear of; an empty one is ignored. A rect that is not finite, or one that
    // does not fit the full list, is not kept and makes the whole set invalid (`invalid`, and
    // `place_toast` then places nothing), and this returns false: the caller handles that, so a lost
    // rect is never silently a toast over it.
    [[nodiscard]] bool add_avoid(const RectF& r) {
        if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) ||
            !std::isfinite(r.h)) {
            invalid = true;
            return false;
        }
        if (r.empty()) {
            return true;
        }
        if (avoid_count >= kMaxToastAvoid) {
            invalid = true;
            return false;
        }
        avoid[avoid_count++] = r;
        return true;
    }
    // Like add_avoid, for a rect in the slot's row that the pill gives way to by sliding sideways
    // (a chord name) instead of being dropped.
    [[nodiscard]] bool add_avoid_beside(const RectF& r) {
        const std::size_t at = avoid_count;
        const bool kept = add_avoid(r);
        if (kept && avoid_count > at) {
            give_way[at] = true;
        }
        return kept;
    }
};

// Where a toast went.
enum class ToastSpot : std::uint8_t {
    None,   // nowhere fits: not shown (a warning gets the icon first, see `ToastForm`)
    Anchor, // under, or above, the control that caused it
    Slot,   // in the toast's own strip under the top row, clear of the status line
    Strip,  // in the free strip of the open sheet or editor
    Over,   // over the falling notes: only for a toast that must show (its Undo), when no other
            // place was clear
};

// What is drawn there.
enum class ToastForm : std::uint8_t {
    Full,      // the whole text
    Shortened, // narrower than the text: the host cuts the text to the box with an ellipsis
    Icon,      // a warning's round icon, no text: only a warning gets it, when not even the shortest
               // text fits anywhere
    None,
};

struct ToastPlacement {
    RectF box;
    ToastSpot spot = ToastSpot::None;
    ToastForm form = ToastForm::None;
};

// The shortest a shortened pill may be, in ems of the toast's font size (the pill's own padding
// comes on top): under it the text would be a few letters and an ellipsis.
inline constexpr float kToastMinTextEm = 6.0f;

// Where the pill of size `w` x `h` goes in a `display_w` x `display_h` window. `anchor` is the
// control that caused the message (empty if none). `min_w` is the narrowest a shortened pill may be, `icon` the side of the warning icon.
//
// A spot is used only when the whole pill lies inside the window, clear of every `avoid` rect and
// of the anchor, and either clear of `regions.notes` (the whole window when that is unset) or inside `regions.surface` or `regions.slot`; touching an edge
// is clear, any overlap is not. In order:
//   1. by the anchor: centred on it, `gap` under it, else `gap` above it (kept inside the window
//      sideways), at the full width;
//   2. in the slot, centred, at the full width;
//   3. in the strip, centred, at the full width;
//      then, when an `add_avoid_beside` rect (a chord name in the slot's row) blocks the middle, in
//      the slot at the full width slid sideways to the nearest place clear of every `avoid` rect:
//      the pill gives way, the rect stays;
//      then in the slot at the full width slid sideways to the nearest place clear of every `avoid`
//      rect (a chord name stands in the slot's row): the pill gives way, the rect stays;
//   4. in the slot, then in the strip, shortened to the room there when that is at least
//      `min_w` (`ToastForm::Shortened`);
//   5. a warning only: an `icon` x `icon` square (the host makes it at least a finger wide, as it
//      takes a touch) in the first of the anchor's places, the slot or the strip that holds it
//      (`ToastForm::Icon`);
//   6. only for `must_show` (the toast of an edit, whose Undo must stay reachable): the first clear
//      place from the top of the notes down, over the notes if need be (`ToastSpot::Over`), still
//      clear of every `avoid` rect and the anchor;
//   7. nothing: `ToastSpot::None`. A plain message is dropped rather than put over something.
// A pill wider than the window less a `gap` margin each side counts as that wide (and is shortened).
// Bad values count as 0; a set that is `invalid` places nothing.
ToastPlacement place_toast(float w, float h, float min_w, float icon, bool warning,
                           const RectF& anchor, float display_w, float display_h,
                           const ToastRegions& regions, float gap, bool must_show = false);

// A warning that is shown without its words (the icon alone, or not at all) needs room: the text is
// what the player must not miss.
constexpr bool toast_lacks_words(bool warning, ToastForm form) {
    return warning && form != ToastForm::Full && form != ToastForm::Shortened;
}

// Whether the bar gives way to such a warning: it is shown, so it hides as it does after its timeout
// and frees the status row for the words. A pinned bar gives way too (the pin setting stays on and
// the bar comes back when the warning is gone).
constexpr bool bar_gives_way(bool lacks_words, bool bar_shown) {
    return lacks_words && bar_shown;
}

} // namespace etude
