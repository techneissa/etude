// SPDX-License-Identifier: GPL-3.0-or-later
// The results card: when a run ends, a card centred over the note area with the grade summary, how
// it compares with last time, the bars with the most misses (each a chip to choose, with two ways
// to practise the chosen ones), the choice of the hands to practise next and the buttons Replay and
// Done. What it shows and which buttons apply come from core (etude/results_card.hpp); where its
// parts go from results_card_layout.hpp. It holds no music state beyond which chips are chosen: it
// draws a card the app made when the run finished and says what was tapped.
#pragma once

#include "kit.hpp"
#include "replay_port.hpp"
#include "results_card_layout.hpp"
#include "summary_page.hpp"

#include "etude/bar_order.hpp"
#include "etude/results_card.hpp"

#include <cstdint>
#include <span>

namespace etude::app::ui {

// A font icon the card draws and the size it draws it at, in design points: the host bakes these in
// the frames after start-up, with the printable ASCII, so the card's first frame builds no glyph.
struct CardIcon {
    const char* glyph;
    float size;
};
std::span<const CardIcon> card_warm_icons();

class ResultsCardView {
  public:
    // Draws `result` (null: none) centred in `area`, the note area in ImGui points, and returns
    // what was tapped this frame (CardAction::None for nothing). A card with a new serial fades and
    // scales in over `tok::kMove`; with reduced effects it is simply there.
    // `review` says whether the run has a "How it went" page and holds what the page shows; the page
    // has the room of its own, `page_area` (core's `summary_area`; `area` when empty).
    CardChoice draw(Kit& k, const ResultsCard* result, const RectF& area,
                    const ReviewInfo& review = {}, const RectF& page_area = {});

    // The replay is up: the card and its page wait out of sight, and come back as they were.
    void rest() {
        shown_ = false;
        summary_.hide();
    }
    // What was tapped on the summary page since the last call (None for nothing), and cleared.
    ReviewChoice take_review_choice();
    // `board`: the page opens on its Board tab (the developer's flag).
    void open_summary(bool board = false, bool runs = false);
    void close_summary() { summary_open_ = false; }
    // A card is on screen (it was drawn last frame).
    bool shown() const { return shown_; }
    // The scale the card was last drawn at, in pixels per design point (the host warms the font at
    // it); 0 before the first card.
    float scale() const { return scale_; }

    // One by one with two bars or more chosen shows the order list in the card's place: drag a
    // bar, or move it with its buttons, then Start (Back returns to the card). While it is up
    // `ordering()` is true and the keys answer it: `order_key(true)` is Start, `false` is Back.
    bool ordering() const { return shown_ && ordering_; }
    void order_key(bool start) { key_ = start ? 1 : -1; }
    // Developer (the audit and the shots): the card's One by one with every bar chosen, once for a
    // card, without a tap.
    void start_order(const ResultsCard& result);

  private:
    CardChoice draw_bars(Kit& k, ImDrawList* dl, const ResultsCard& result, const CardLayout& l);
    CardChoice draw_buttons(Kit& k, const ResultsCard& result, const CardLayout& l);
    CardChoice draw_summary(Kit& k, const ReviewInfo& review, const RectF& area,
                            const RectF& page_area);
    void review_chip(Kit& k, const ReviewInfo& review, const CardLayout& l);
    // A Learn drill's "How it went" card, in the place of the card (drill_card.cpp).
    CardChoice draw_drill(Kit& k, const ResultsCard& result, const RectF& area);
    // The order list, in the place of the card.
    CardChoice draw_order(Kit& k, const ResultsCard& result, const RectF& area);

    SummaryPageView summary_;
    bool summary_open_ = false;
    ReviewChoice review_choice_;
    std::uint32_t serial_ = 0;
    bool had_card_ = false;
    bool shown_ = false;
    float scale_ = 0.0f;
    std::uint32_t chosen_ = 0; // bit i for `ResultsCard::bars[i]`
    float progress_ = 0.0f; // 0..1 linear, eased when drawn
    bool ordering_ = false;
    BarOrder order_;
    ReorderDrag drag_;
    int key_ = 0; // a key answered the order list: 1 Start, -1 Back, 0 none
    std::uint32_t demo_serial_ = 0xFFFFFFFFu; // the card `start_order` last opened the list for
    std::uint32_t demo_request_ = 0xFFFFFFFFu; // the card `start_order` asked it for
};

} // namespace etude::app::ui
