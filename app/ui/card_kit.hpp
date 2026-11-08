// SPDX-License-Identifier: GPL-3.0-or-later
// What the cards over the note area share (the results card, the order list, the check-in and the
// summary of a trouble-bar run): the button, the scale they draw at, a tap, and text in a grade's
// gradient.
#pragma once

#include "kit.hpp"
#include "replay_port.hpp"

#include "etude/color.hpp"
#include "etude/grade_palette.hpp"
#include "etude/results_card.hpp"

namespace etude::app::ui {

ImVec2 to_vec(float x, float y);

// The pin chip of a run, an icon chip like the others: lit while the run is pinned, dim while it
// cannot be (no notes were kept) or an answer is awaited. True on a tap, also while it is dim: the
// caller says why it cannot (a toast) and the tap changes nothing else.
bool pin_chip(Kit& k, const char* id, const PinInfo& pin, ImVec2 centre, ImVec2 hit);

// What a button shows before its label: a font icon (null: none).
struct ButtonIcon {
    const char* font = nullptr;
};

// The width the icon takes, the gap to the label not counted.
float button_icon_width(const Kit& k, const ButtonIcon& i);
void draw_button_icon(const Kit& k, ImDrawList* dl, const ButtonIcon& i, float x, float cy,
                      ImVec4 ink);

// While a card draws, the kit's unit is the card's own scale: the type, figures, buttons and chips
// grow with the card, and every size the kit's helpers take follows.
class ScaledKit {
  public:
    ScaledKit(Kit& k, float scale) : k_(k), saved_(k.u) { k_.u = scale; }
    ~ScaledKit() { k_.u = saved_; }
    ScaledKit(const ScaledKit&) = delete;
    ScaledKit& operator=(const ScaledKit&) = delete;

  private:
    Kit& k_;
    float saved_;
};

// How the run compares with the last one like it, for the line under the accuracy figure (empty while
// the history loads).
void card_comparison_line(const ResultsCard& c, FitText* out);

// A button of the card: its face in the kit's states (`draw_state`), the icon and the label
// centred. `primary` is the accent button. True when it was tapped.
bool card_button(Kit& k, const char* id, const ButtonIcon& icon, const char* label,
                 const RectF& cell, bool primary, bool enabled = true);

// The plain face behind a row of figures (the counts, a summary row): the idle fill, or with High
// contrast a hairline round it and no fill, so grade colours sit on the card's own surface, where
// they hold 7:1.
void figure_row_face(const Kit& k, ImDrawList* dl, const RectF& r);

// A cell of a row of figures: a number over its caption. A grade's cell shows the number in the
// grade's colour while it is above zero (a zero is dim); the caption, the grade's word, stays.
struct FigureCell {
    const char* caption;
    int value;
    Grade grade;
};

// `cells` laid out evenly in `r` over `figure_row_face`: the results card's counts row, and the
// check-in's.
void grade_counts_row(Kit& k, ImDrawList* dl, const RectF& r, const FigureCell* cells, int count);

// A grade's gradient as ink for text: its top and foot colours as the UI draws them.
struct GradeInk {
    ImVec4 top;
    ImVec4 bottom;
};
GradeInk grade_ink(const Kit& k, Grade grade);

// `text` at `pos` (its top left) shaded from `ink.top` at the top of its capitals to `ink.bottom`
// at their foot. Adds the glyphs to `dl` and shades those vertices: nothing is allocated.
void text_gradient(const Kit& k, ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                   const GradeInk& ink, const char* text);

} // namespace etude::app::ui
