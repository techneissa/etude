// SPDX-License-Identifier: GPL-3.0-or-later
// The on-screen keyboard, one for every text field in the app (the library's search and names, a
// section's name, a look's name, the typed tempo), and the name card that stands above it.
//
// When it shows. A field a finger tapped raises it; a mouse click focuses the field for the
// computer keyboard alone, which types into any open field either way (and keeps the piano's
// typing keys out of it). A key typed on the computer keyboard while the on-screen one is up
// lowers it: that player has a keyboard. SDL cannot say whether one is plugged in, and a
// laptop with a touch screen always has one, so what the player does decides.
// Where it sits. At the bottom of the window, the whole width of it: every row runs edge to edge and
// the keys grow with the window (osk_layout.hpp); the card or popover with the field is laid out
// above it and never under it.
// What the keys do. Letters and symbols type as the finger lands, Backspace repeats while held,
// Clear empties the field, Done is Enter: the field's owner commits. Escape (or the card's Cancel)
// leaves without committing.
#pragma once

#include "kit.hpp"
#include "osk_focus.hpp"
#include "osk_layout.hpp"
#include "osk_text.hpp"

#include <span>
#include <string>
#include <string_view>

namespace etude::app::ui::osk {

// The press being handled came from a finger.
bool by_touch();

// Asks for the computer keyboard's text for this frame, and keeps the piano's typing keys out of
// it. Call it every frame a field is open.
void want_text();

// A name dialog is up (it drew last frame or this): what lies under it is not the audit's.
bool modal_open();

// A modal that names something is drawing this frame (the profile sheet): `modal_open` says so.
void note_modal();

// What one frame of the computer keyboard did to a field.
struct Typed {
    bool any = false;    // a character or Backspace arrived
    bool done = false;   // Enter
    bool cancel = false; // Escape
};

// One frame of the computer keyboard into `text`: the typed characters, Backspace, Enter, Escape.
Typed feed_keyboard(std::string& text, const FieldRules& rules);

class Keyboard {
  public:
    // Slides toward up (`want`) or down; returns how far up it is (0..1, eased).
    float step(const Kit& k, bool want);
    float progress() const { return p_; }
    // Draws the keys in `box`, whose full height is `full` (they sit at their size and slide in
    // with the box), typing into `text`. `numbers` shows the number pad. True when Done was
    // pressed.
    bool draw(Kit& k, const Box& box, float full, std::string& text, const FieldRules& rules,
              bool numbers = false);
    // Back to the letters (a new field starts there).
    void reset() { layer_ = Layer::Letters; }

  private:
    float p_ = 0.0f;
    Layer layer_ = Layer::Letters;
    double backspace_at_ = 0.0;
};

// A field's text in `slot` (the field's face is the caller's): scrolled to keep its end in view,
// the hint in its place while it is empty, the caret at its end while `caret`. `size_pt` is the
// text's size in design points (tok::kBody when 0), centred on the slot's middle either way.
void field_text(Kit& k, ImDrawList* dl, const Box& slot, const std::string& text, const char* hint,
                bool caret, float size_pt = 0.0f);

// What a name card shows; `plan_name_card` (osk_layout.hpp) places it.
struct NameCard {
    const char* title = "Name";
    std::span<const char* const> quick; // one-tap names: a tap puts one in the field
    const char* own = nullptr;          // a third button (the file's name, the default), or none
    bool own_enabled = true;
    bool can_save = true;
    bool focused = true; // the field is being typed in (else it waits for a tap)
    bool top = false;    // stands at the top of its area
    float width = osk_tok::kCardW;
    float above = 0.0f; // see NameCardSpec
    ImVec4 fill = col::kSurface;
};

enum class NameResult : unsigned char { None, Save, Cancel, Own };

struct NameDrawn {
    NameResult result = NameResult::None;
    bool field_tapped = false; // a tap on the field (a finger's raises the keyboard)
    Box card;
};

// The card in `area`, clear of a keyboard that begins at `keys_top` (`area.y1` with none): a title,
// the field with `text`, one-tap names, and the buttons; one row where the room is short. The
// ids are ##name-field, ##name-own, ##name-cancel, ##name-save and ##quick-N.
NameDrawn draw_name_card(Kit& k, const Box& area, float keys_top, std::string& text,
                         const NameCard& card);

// A modal dialog that names something: the card over the whole window, its keyboard under it. One
// per kind of name (a section's, a look's); the caller opens it, draws it every frame and reads
// the result.
class NameDialog {
  public:
    // `max_bytes` is what the core keeps of the name: the field takes no more than that.
    NameDialog(Owner owner, FieldRules rules) : owner_(owner), rules_(rules) {}
    // Asks to open with `text`. `by_touch` is whether a finger asked: it raises the keyboard at
    // once unless there are one-tap names to choose from first (a tap on the field raises it).
    void open(std::string_view text, bool by_touch, bool has_quick);
    bool is_open() const { return (open_ || requested_) && !closing_; }
    const std::string& text() const { return text_; }
    // Closes it (at the next draw: the modal belongs to ImGui's popup stack).
    void close() {
        closing_ = true;
        set_field_open(owner_, false);
    }
    // Draws it (nothing when closed). Save and Cancel close it; Own does not (the caller decides).
    NameResult draw(Kit& k, const char* popup_id, const NameCard& card);

  private:
    Owner owner_;
    FieldRules rules_;
    bool requested_ = false;
    bool open_ = false;
    bool closing_ = false;
    bool focused_ = true;
    bool keys_ = false;
    std::string text_;
    Keyboard keyboard_;
};

} // namespace etude::app::ui::osk
