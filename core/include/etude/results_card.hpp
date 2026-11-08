// SPDX-License-Identifier: GPL-3.0-or-later
// What the results card shows when a run ends, and which of its buttons apply. Plain values only:
// the app builds the card once when a run finishes (never per frame) and draws from it.
#pragma once

#include "etude/difficulty.hpp"
#include "etude/hands_present.hpp"
#include "etude/note_window.hpp"
#include "etude/progress.hpp"
#include "etude/run_record.hpp"
#include "etude/screen_layout.hpp"
#include "etude/sections.hpp"
#include "etude/song.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <optional>
#include <span>

namespace etude {

// The bars the card can name: the ones with the most misses, each a chip that is chosen or not.
// How many it shows depends on its room (`card_size`): three at 800x480, up to this many on a big
// screen.
inline constexpr int kMaxCardBars = 6;
// The compact form (800x480) shows this many.
inline constexpr int kCardCompactBars = 3;

// One of those bars.
struct CardBar {
    int measure =
        0; // the song's bar number (RunBar::measure, counts played bars); a pickup bar is 0
    int written =
        0; // the bar number the printed score shows: what the card says (Measure::written_number)
    // Which time through the written bar this is, counting from 1, when the song plays that bar
    // more than once (a repeat); 0 when it plays once. A repeated passage is "Bar 9, 2nd time".
    int pass = 0;
    int missed = 0;
    int wrong = 0;
    int graded = 0; // the notes of the bar that were graded in that run
    // A bar the song's difficulty suggests, one of its hardest that went without a miss or a wrong
    // press: offered after the trouble bars, not chosen when the card appears. `half_stars` is its
    // rating (2..10); 0 for a trouble bar, which is named by how it went.
    bool suggested = false;
    int half_stars = 0;
    friend bool operator==(const CardBar&, const CardBar&) = default;
};

// How well a bar went, as a whole percent 0..100, from the notes graded in it, the ones missed and
// the wrong presses (each worth `kWrongPressWeight` of a miss, as the trouble bars are ranked): -1
// when nothing was graded, and never 100 while anything went wrong.
int bar_accuracy_pct(int graded, int missed, int wrong);

// How the run compares with the last one like it (same song, mode, hands and speed).
enum class CardComparison : std::uint8_t {
    Unknown, // the history is still loading: nothing is said, a guess would be wrong
    FirstTry,
    Better,
    Worse,
    Same,
};

// What a tap or a key on the card asks for.
enum class CardAction : std::uint8_t {
    None,
    Replay,   // the same section or loop, the same hands
    Hands,    // practise `CardChoice::hands` and go again
    OneByOne, // a practice list of the chosen bars, each looped until it passes
    Together, // the chosen bars looped back to back
    Done,     // dismiss
    Faster,   // a drill's card: the same drill five beats a minute faster
    ToCard,   // a drill's card: back to the Learn card the drill came from
};

struct CardChoice {
    CardAction action = CardAction::None;
    HandFilter hands = HandFilter::Both; // Hands
    // OneByOne and Together: the chosen bars, bit i for `ResultsCard::bars[i]`.
    std::uint32_t bars = 0;
    // OneByOne: the order the player put them in, indices into `ResultsCard::bars`; `order_count`
    // is 0 when the bars go in song order.
    std::array<std::uint8_t, kMaxCardBars> order{};
    int order_count = 0;
    friend bool operator==(const CardChoice&, const CardChoice&) = default;
};

// The most bars the card suggests by difficulty (always after the trouble bars, in the room they
// leave, at most `kMaxCardBars` named in all).
inline constexpr int kMaxSuggestedBars = 2;

// The bars chosen when a card appears: every one it names.
constexpr std::uint32_t card_all_bars(int count) {
    return count <= 0 ? 0u : count >= 32 ? ~0u : (1u << count) - 1u;
}

struct ResultsCard {
    std::uint32_t serial = 0; // told apart by the screen: a new card fades in
    RunMode mode = RunMode::Flow;
    HandFilter hands = HandFilter::Both; // what was practised
    // The run graded nothing (the practised hand had no notes where keys were pressed): no figures.
    bool ungraded = false;
    // Whole percent 0..100 of the practised notes; -1 for no accuracy at all (never 100%).
    int accuracy_pct = -1;
    // Each hand's own accuracy, set only when both hands were practised and both graded notes;
    // -1 otherwise.
    int right_pct = -1;
    int left_pct = -1;
    // The difficulty-weighted score as a whole percent, on the same scale as `accuracy_pct`; -1 when
    // the setting is off, the song is not rated or nothing was graded. Set only by the weighted
    // form of make_results_card; when -1 the card shows the plain score alone.
    int weighted_pct = -1;
    // Flow Mode's grades. Wait Mode has no timing grades: it shows `played` and `wrong`.
    RunCounts counts;
    int wrong = 0;
    int longest_combo = 0;
    CardComparison comparison = CardComparison::Unknown;
    // The change from last time: the difference of the two whole percentages the screen shows,
    // at least 1 for Better and Worse (the trend itself is judged on the raw accuracy).
    int delta_pct = 0;
    bool new_best = false;
    std::array<CardBar, kMaxCardBars> bars{};
    int bar_count = 0;
    // The bars chosen when the card appears (bit i for bars[i]): the trouble bars, not the
    // suggested ones.
    std::uint32_t start_bars = 0;
    // The hands that have notes in what was practised: the others are grey and not offered.
    HandsPresent present;
    // The Left, Right and Both choice is shown (not in a practice list, whose items set the hands).
    bool hand_choice = false;
    // A practice list is running: the card shows the result and the list goes on as before. It has
    // no hand choice (the list sets the hands), names no trouble bars (a loop of its own would
    // replace the list's step) and Replay plays the current item again.
    bool in_plan = false;
    // A Learn drill's run (#373): its tempo in beats a minute (0: not a drill, the ordinary card) and
    // its title. The card is then "How it went" with Again, Faster and the Learn card.
    int drill_bpm = 0;
    std::string drill_title;
};

// Whether the card offers practising `hands` next: the hand has notes (both hands: both have).
bool hand_offered(const HandsPresent& present, HandFilter hands);
// What a tap or key on a hand that is not offered says.
const char* hand_hint(HandFilter hands);

// What a hand key (1, 2, 3) does when `card` (null: none is up) might be answering it: Pass when
// there is no card or it has no hand choice (a practice list's), so the key sets the hands as it
// does without a card; Start when the card offers that hand; Hint when it does not (a hand with no
// notes), which says why and changes nothing.
enum class HandKey : std::uint8_t { Pass, Start, Hint };
HandKey card_hand_key(const ResultsCard* card, HandFilter hands);

// The bars of `run` with the most misses, most first, up to `max` (at most kMaxCardBars). A wrong
// press counts as `kWrongPressWeight` of a miss, so Wait Mode, which misses nothing, still names
// where the player fumbled. A bar with neither is never named; a tie goes to the earlier bar.
std::array<CardBar, kMaxCardBars> worst_bars(const RunRecord& run, int* count,
                                             int max = kMaxCardBars);

// The same over bars that are not a RunRecord's (a replay's, worked out from the played notes).
std::array<CardBar, kMaxCardBars> worst_bars(std::span<const RunBar> bars, int* count,
                                             int max = kMaxCardBars);

// Fills the bar's written number and its pass (which time through a written bar the song plays
// more than once) from the song's bars; a bar the song does not list is its own written number.
void name_written_bar(CardBar& bar, std::span<const Measure> measures);

// "1st", "2nd", "3rd", "4th", ... "11th", "12th", "13th", "21st": the suffix of `n`.
const char* ordinal_suffix(int n);

// The hardest bars of `difficulty` that `run` played and did not name as trouble (`taken`), hardest
// first, at most `max`: the suggestions after the trouble bars. A bar outside the run (a loop's
// other bars) is never suggested.
std::array<CardBar, kMaxCardBars> suggested_bars(const RunRecord& run,
                                                 const SongDifficulty& difficulty,
                                                 std::span<const CardBar> taken, int* count,
                                                 int max = kMaxSuggestedBars);

// The card for a finished run. `comparable` is false while the history loads. `measures` (the
// song's bars, may be empty) give the named bars their written number. With `difficulty` the
// room the trouble bars leave (up to `kMaxSuggestedBars`) goes to the song's hardest bars of the
// run, offered but not chosen. With `weighted` (and `difficulty`) the card also carries the
// difficulty-weighted score (`weighted_accuracy`); the plain one is as ever.
ResultsCard make_results_card(const RunRecord& run, const AttemptChange& change, bool comparable,
                              const HandsPresent& present, bool in_plan,
                              std::span<const Measure> measures = {},
                              const SongDifficulty* difficulty = nullptr, bool weighted = false);
// The card for a run that pressed keys but graded no note.
ResultsCard make_ungraded_card(RunMode mode, HandFilter hands, const HandsPresent& present,
                               bool in_plan);

// Whether `choice` is one the card offers: Replay and Done always; Hands when the card shows the
// choice and the hand has notes; OneByOne and Together when at least one of the named bars is
// chosen.
bool card_offers(const ResultsCard& card, const CardChoice& choice);

// ---- Practising the chosen bars -----------------------------------------------------------------

enum class BarPracticeKind : std::uint8_t {
    OneByOne, // each bar its own item, looped until it passes
    Together, // the bars back to back: neighbours form one loop
};

// One stretch to loop: a bar, or neighbouring bars joined.
struct BarLoop {
    double start_s = 0.0;
    double end_s = 0.0;
    int first_measure = 0; // Measure::number of the first and last bar of the stretch
    int last_measure = 0;
    friend bool operator==(const BarLoop&, const BarLoop&) = default;
};

struct BarPractice {
    BarPracticeKind kind = BarPracticeKind::OneByOne;
    std::array<BarLoop, kMaxCardBars> loops{}; // in song order
    int count = 0;
    int dropped = 0; // chosen bars too short to loop, or not in the song
    // Together with neighbouring bars only: one A-B loop that plays until stopped, not a list.
    bool single_loop() const { return kind == BarPracticeKind::Together && count == 1; }
};

// The loops for `chosen` (Measure::number of each bar, repeats ignored). OneByOne: one loop per
// bar, in song order, or in the order of `chosen` when `keep_order` (the player's own order).
// Together: bars that follow one another in the song join into one loop, so a gap makes a second
// loop (always song order). A bar shorter than `kMinLoopSeconds` is dropped (a joined stretch is
// judged whole). `duration_s` is the end of the last bar.
BarPractice plan_bar_practice(std::span<const int> chosen, std::span<const Measure> measures,
                              double duration_s, BarPracticeKind kind, bool keep_order = false);

// The practice-list item a loop of `practice` becomes in the section `id`: OneByOne plays the
// section until a pass is clean; Together plays it once, whatever the pass.
PlanItem bar_plan_item(SectionId id, BarPracticeKind kind);

// ---- The card's size ----------------------------------------------------------------------------

// The card takes this share of the note area's width and height at most, once the area is big
// enough for it to be larger than the compact form.
inline constexpr float kCardShare = 0.6f;
// The compact form's width in design points (800x480 draws this), the tallest the card's rows
// come to with one row of chips (headline, counts, bars header, chips, buttons and their gaps),
// the narrowest a trouble-bar chip may be, and the most the type and buttons grow past the UI
// scale.
inline constexpr float kCardBaseWidthPt = 520.0f;
inline constexpr float kCardFullHeightPt = 292.0f;
inline constexpr float kCardChipMinPt = 120.0f;
inline constexpr float kCardMaxGrow = 1.5f;
// The growth moves in steps of this much (a smaller change is none), so a dragged window edge does
// not ask the font for a new size at every pixel.
inline constexpr float kCardGrowStep = 0.05f;
// The card's padding and the gap between its chips, in design points (the UI's `tok::kS4` and
// `tok::kS2`; a test holds them equal): with the chips' least width they say how many fit a row.
inline constexpr float kCardPadPt = 16.0f;
inline constexpr float kCardChipGapPt = 8.0f;
// A card is 60% of the note area wide, but at most this many design points at its own scale.
inline constexpr float kCardMaxWidthPt = 900.0f;
// The clear space kept round the card inside the note area, in design points.
inline constexpr float kCardMarginPt = 8.0f;

struct CardSize {
    float scale = 1.0f; // pixels per design point the card draws at, at least `ui_unit`
    float width = 0.0f; // the card's width in pixels
};
// How big the card is in `area` (the note area) at the UI scale `ui_unit`: the type, figures and
// buttons draw at `scale` (the UI scale, or up to `kCardMaxGrow` times it when a share of the
// area's height holds the fullest card at a bigger one, never so big that the card shows fewer
// trouble-bar chips than at the UI scale: its chips keep their row) and the width is `kCardShare`
// of the area's, no narrower than the compact form's and no wider than the area less its margins.
CardSize card_size(const RectF& area, float ui_unit);

// ---- The summary page's size ----------------------------------------------------------------------

// The summary page ("How it went") is a card of its own size over the player: it may cover the chord
// strip above the notes and part of the keyboard, never the top row (or the bar's band and lane under
// it), and it leaves `kSummaryKeysShown` of the keyboard's height showing below it.
inline constexpr float kSummaryKeysShown = 1.0f / 3.0f;
// The page is this share of the window's width, at most `kSummaryMaxWidthPt` design points at the
// card's scale, centred.
inline constexpr float kSummaryShare = 0.85f;
inline constexpr float kSummaryMaxWidthPt = 1100.0f;

// The room the summary page may use: the window's width, from the top of the chord strip (the note
// area's top with no strip) to `kSummaryKeysShown` of the keyboard's height above the window's foot.
// Empty with no note area.
RectF summary_area(const ScreenLayout& layout);
// The page's size in `area` (`summary_area`) at the UI scale `ui_unit`: it draws at `card_scale` (the
// in-play card's, `card_size` of the note area; the UI's when 0) and is `kSummaryShare` of the area's
// width, at most `kSummaryMaxWidthPt` at that scale, no narrower than the compact form and inside
// the margins.
CardSize summary_size(const RectF& area, float ui_unit, float card_scale = 0.0f);

} // namespace etude
