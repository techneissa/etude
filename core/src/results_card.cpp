// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/results_card.hpp"

#include "etude/drill.hpp"

#include "etude/weighted_score.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace etude {

namespace {

int percent(std::optional<double> accuracy) {
    return accuracy ? static_cast<int>(std::lround(*accuracy * 100.0)) : -1;
}

// How many misses a bar is worth: a wrong press counts for part of one.
double heat(const RunBar& bar) { return bar.missed + kWrongPressWeight * bar.wrong; }

} // namespace

int bar_accuracy_pct(int graded, int missed, int wrong) {
    if (graded <= 0) {
        return -1;
    }
    const double error =
        std::min(1.0, (std::max(0, missed) + kWrongPressWeight * std::max(0, wrong)) / graded);
    const int pct = static_cast<int>(std::lround((1.0 - error) * 100.0));
    return pct >= 100 && error > 0.0 ? 99 : pct;
}

std::array<CardBar, kMaxCardBars> worst_bars(std::span<const RunBar> bars, int* count, int max) {
    std::array<CardBar, kMaxCardBars> out{};
    std::array<double, kMaxCardBars> out_heat{};
    const int limit = std::clamp(max, 0, kMaxCardBars);
    int n = 0;
    // `measures` is sorted by bar, so a bar goes after every one that is as hot: on a tie the
    // earlier bar stays first.
    for (const RunBar& bar : bars) {
        const double h = heat(bar);
        if (h <= 0.0) {
            continue;
        }
        int at = n;
        while (at > 0 && out_heat[static_cast<std::size_t>(at) - 1] < h) {
            --at;
        }
        if (at >= limit) {
            continue;
        }
        for (int i = std::min(n, limit - 1); i > at; --i) {
            out[static_cast<std::size_t>(i)] = out[static_cast<std::size_t>(i) - 1];
            out_heat[static_cast<std::size_t>(i)] = out_heat[static_cast<std::size_t>(i) - 1];
        }
        out[static_cast<std::size_t>(at)] = {bar.measure, bar.measure, 0,
                                             bar.missed,  bar.wrong,   bar.graded};
        out_heat[static_cast<std::size_t>(at)] = h;
        n = std::min(n + 1, limit);
    }
    if (count != nullptr) {
        *count = n;
    }
    return out;
}

std::array<CardBar, kMaxCardBars> worst_bars(const RunRecord& run, int* count, int max) {
    return worst_bars(std::span<const RunBar>(run.measures), count, max);
}

void name_written_bar(CardBar& bar, std::span<const Measure> measures) {
    const auto it = std::find_if(measures.begin(), measures.end(),
                                 [&](const Measure& m) { return m.number == bar.measure; });
    if (it == measures.end()) {
        bar.written = bar.measure;
        return;
    }
    bar.written = it->written_number();
    // The played bars that are this written bar, in order: a repeat plays it again.
    int same = 0;
    int before = 0;
    for (const Measure& m : measures) {
        if (m.written_number() != bar.written) {
            continue;
        }
        ++same;
        if (m.number < bar.measure) {
            ++before;
        }
    }
    bar.pass = same > 1 ? before + 1 : 0;
}

std::array<CardBar, kMaxCardBars> suggested_bars(const RunRecord& run,
                                                 const SongDifficulty& difficulty,
                                                 std::span<const CardBar> taken, int* count,
                                                 int max) {
    std::array<CardBar, kMaxCardBars> out{};
    int n = 0;
    const int limit = std::clamp(max, 0, kMaxCardBars);
    for (const int number : hardest_bars(difficulty, static_cast<int>(difficulty.bars.size()))) {
        if (n >= limit) {
            break;
        }
        const auto played = std::find_if(run.measures.begin(), run.measures.end(),
                                         [&](const RunBar& b) { return b.measure == number; });
        // Only a bar played clean: one with a miss or a wrong press is a trouble bar's business.
        if (played == run.measures.end() || played->graded <= 0 || played->missed > 0 ||
            played->wrong > 0) {
            continue;
        }
        const bool named = std::any_of(taken.begin(), taken.end(),
                                       [&](const CardBar& b) { return b.measure == number; });
        if (named) {
            continue;
        }
        CardBar bar;
        bar.measure = number;
        bar.written = number;
        bar.graded = played->graded;
        bar.suggested = true;
        bar.half_stars = half_stars_of(difficulty.bar_at(number));
        out[static_cast<std::size_t>(n++)] = bar;
    }
    if (count != nullptr) {
        *count = n;
    }
    return out;
}

const char* ordinal_suffix(int n) {
    const int last_two = std::abs(n) % 100;
    if (last_two >= 11 && last_two <= 13) {
        return "th";
    }
    switch (std::abs(n) % 10) {
    case 1:
        return "st";
    case 2:
        return "nd";
    case 3:
        return "rd";
    default:
        break;
    }
    return "th";
}

bool hand_offered(const HandsPresent& present, HandFilter hands) {
    switch (hands) {
    case HandFilter::Left:
        return present.left;
    case HandFilter::Right:
        return present.right;
    case HandFilter::Both:
        break;
    }
    return present.both();
}

ResultsCard make_results_card(const RunRecord& run, const AttemptChange& change, bool comparable,
                              const HandsPresent& present, bool in_plan,
                              std::span<const Measure> measures, const SongDifficulty* difficulty,
                              bool weighted) {
    ResultsCard c;
    c.mode = run.mode;
    c.hands = run.hands;
    c.in_plan = in_plan;
    c.present = present;
    c.hand_choice = !in_plan;
    c.accuracy_pct = percent(run.accuracy(run.hands));
    if (run.hands == HandFilter::Both && run.right.graded > 0 && run.left.graded > 0) {
        c.right_pct = percent(run.right.accuracy());
        c.left_pct = percent(run.left.accuracy());
    }
    if (weighted && difficulty != nullptr && c.accuracy_pct >= 0) {
        c.weighted_pct = percent(weighted_accuracy(run, *difficulty));
    }
    c.counts = run.counts;
    c.wrong = run.wrong;
    c.longest_combo = run.longest_combo;
    if (!comparable) {
        c.comparison = CardComparison::Unknown;
    } else if (!change.has_previous) {
        c.comparison = CardComparison::FirstTry;
    } else {
        // The difference of the two percentages the screen shows, not of the raw accuracies.
        const int now = c.accuracy_pct >= 0 ? c.accuracy_pct : percent(change.accuracy);
        const int delta = now - percent(change.previous_accuracy);
        switch (change.trend) {
        case Trend::Better:
            c.comparison = CardComparison::Better;
            c.delta_pct = std::max(delta, 1);
            break;
        case Trend::Worse:
            c.comparison = CardComparison::Worse;
            c.delta_pct = std::max(-delta, 1);
            break;
        case Trend::Same:
            c.comparison = CardComparison::Same;
            break;
        }
    }
    c.new_best = comparable && change.new_best;
    // A practice list's step is a loop of its own: a tap on a bar would replace it, so a list's
    // card names none.
    if (!in_plan) {
        c.bars = worst_bars(run, &c.bar_count);
    }
    // Chosen at the start: the trouble bars. The suggestions below are offered, not chosen.
    c.start_bars = card_all_bars(c.bar_count);
    if (!in_plan && difficulty != nullptr && c.bar_count < kMaxCardBars) {
        int added = 0;
        const std::array<CardBar, kMaxCardBars> extra = suggested_bars(
            run, *difficulty,
            std::span<const CardBar>(c.bars.data(), static_cast<std::size_t>(c.bar_count)), &added,
            std::min(kMaxSuggestedBars, kMaxCardBars - c.bar_count));
        for (int i = 0; i < added; ++i) {
            c.bars[static_cast<std::size_t>(c.bar_count++)] = extra[static_cast<std::size_t>(i)];
        }
    }
    for (int i = 0; i < c.bar_count; ++i) {
        name_written_bar(c.bars[static_cast<std::size_t>(i)], measures);
    }
    return c;
}

ResultsCard make_ungraded_card(RunMode mode, HandFilter hands, const HandsPresent& present,
                               bool in_plan) {
    ResultsCard c;
    c.mode = mode;
    c.hands = hands;
    c.ungraded = true;
    c.in_plan = in_plan;
    c.present = present;
    c.hand_choice = !in_plan;
    return c;
}

const char* hand_hint(HandFilter hands) {
    switch (hands) {
    case HandFilter::Left:
        return "No left-hand notes here";
    case HandFilter::Right:
        return "No right-hand notes here";
    case HandFilter::Both:
        break;
    }
    return "Only one hand has notes here";
}

HandKey card_hand_key(const ResultsCard* card, HandFilter hands) {
    if (card == nullptr || !card->hand_choice) {
        return HandKey::Pass;
    }
    return hand_offered(card->present, hands) ? HandKey::Start : HandKey::Hint;
}

bool card_offers(const ResultsCard& card, const CardChoice& choice) {
    switch (choice.action) {
    case CardAction::Replay:
    case CardAction::Done:
        return true;
    case CardAction::Faster:
        return card.drill_bpm > 0 && card.drill_bpm < kDrillMaxBpm; // none above the cap
    case CardAction::ToCard:
        return card.drill_bpm > 0;
    case CardAction::Hands:
        return card.hand_choice && hand_offered(card.present, choice.hands);
    case CardAction::OneByOne:
    case CardAction::Together:
        return (choice.bars & card_all_bars(card.bar_count)) != 0;
    case CardAction::None:
        break;
    }
    return false;
}

BarPractice plan_bar_practice(std::span<const int> chosen, std::span<const Measure> measures,
                              double duration_s, BarPracticeKind kind, bool keep_order) {
    BarPractice out;
    out.kind = kind;
    // The chosen bars by place in the song, once each.
    std::array<std::size_t, kMaxCardBars> at{};
    std::size_t n = 0;
    for (const int number : chosen) {
        const auto it = std::find_if(measures.begin(), measures.end(),
                                     [number](const Measure& m) { return m.number == number; });
        if (it == measures.end()) {
            ++out.dropped;
            continue;
        }
        const auto index = static_cast<std::size_t>(it - measures.begin());
        if (std::find(at.begin(), at.begin() + static_cast<std::ptrdiff_t>(n), index) !=
            at.begin() + static_cast<std::ptrdiff_t>(n)) {
            continue;
        }
        if (n == at.size()) {
            ++out.dropped;
            continue;
        }
        at[n++] = index;
    }
    // Insertion sort: at most kMaxCardBars entries.
    const bool sorted = !(keep_order && kind == BarPracticeKind::OneByOne);
    for (std::size_t i = 1; sorted && i < n; ++i) {
        for (std::size_t j = i; j > 0 && at[j] < at[j - 1]; --j) {
            std::swap(at[j], at[j - 1]);
        }
    }
    const auto bar_end = [&](std::size_t i) {
        return i + 1 < measures.size() ? measures[i + 1].start_s : duration_s;
    };
    const auto add = [&](std::size_t first, std::size_t last) {
        BarLoop loop{measures[first].start_s, bar_end(last), measures[first].number,
                     measures[last].number};
        if (!(loop.end_s - loop.start_s >= kMinLoopSeconds)) {
            out.dropped += static_cast<int>(last - first) + 1;
            return;
        }
        out.loops[static_cast<std::size_t>(out.count++)] = loop;
    };
    for (std::size_t i = 0; i < n;) {
        std::size_t last = i;
        if (kind == BarPracticeKind::Together) {
            while (last + 1 < n && at[last + 1] == at[last] + 1) {
                ++last;
            }
        }
        add(at[i], at[last]);
        i = last + 1;
    }
    return out;
}

PlanItem bar_plan_item(SectionId id, BarPracticeKind kind) {
    PlanItem item;
    item.section = id;
    item.repeats = 1;
    item.clean_passes_only = kind == BarPracticeKind::OneByOne;
    return item;
}

namespace {

// The card of this note area drawn at `scale`: its width is the share of the area's (at most so
// many design points at its own scale), no narrower than the compact form and inside the margins.
CardSize card_at(const RectF& area, float u, float scale) {
    CardSize size;
    size.scale = scale;
    const float most = std::max(0.0f, area.w - 2.0f * kCardMarginPt * u);
    const float compact = std::min(kCardBaseWidthPt * u, most);
    const float wanted = std::min(kCardShare * area.w, kCardMaxWidthPt * scale);
    size.width = std::min(std::max(wanted, compact), most);
    return size;
}

// How many trouble-bar chips the card shows in a row: as many as fit at their least width, three
// in the compact form, at most kMaxCardBars (the layout draws exactly this many).
int chips_in_row(const CardSize& size) {
    const float inner = size.width - 2.0f * kCardPadPt * size.scale;
    const float gap = kCardChipGapPt * size.scale;
    const int fit =
        static_cast<int>(std::floor((inner + gap) / (kCardChipMinPt * size.scale + gap)));
    const bool compact = size.width <= kCardBaseWidthPt * size.scale + 0.5f;
    return std::clamp(fit, 1, compact ? kCardCompactBars : kMaxCardBars);
}

} // namespace

CardSize card_size(const RectF& area, float ui_unit) {
    const float u = ui_unit > 0.0f ? ui_unit : 1.0f;
    CardSize size;
    size.scale = u;
    if (area.empty()) {
        return size;
    }
    // The type and buttons grow only where the share of the height holds the fullest card at a
    // bigger scale than the UI's.
    const float by_height = kCardShare * area.h / kCardFullHeightPt;
    const float raw = std::clamp(by_height / u, 1.0f, kCardMaxGrow);
    float grow = std::floor(raw / kCardGrowStep + 1e-4f) * kCardGrowStep;
    // ... but never so far that the chips lose their row: a bigger card whose chips no longer fit
    // is a smaller card.
    const int chips = chips_in_row(card_at(area, u, u));
    while (grow > 1.0f + 1e-4f && chips_in_row(card_at(area, u, u * grow)) < chips) {
        grow -= kCardGrowStep;
        grow = grow < 1.0f + 1e-3f ? 1.0f : grow; // back to exactly the UI's scale, not a hair over
    }
    return card_at(area, u, u * grow);
}

RectF summary_area(const ScreenLayout& layout) {
    if (layout.note_area.empty()) {
        return {};
    }
    const float top = layout.chord_label.y;
    const float foot = layout.keyboard.empty()
                           ? layout.note_area.bottom()
                           : layout.keyboard.bottom() - kSummaryKeysShown * layout.keyboard.h;
    return {layout.note_area.x, top, layout.note_area.w, std::max(0.0f, foot - top)};
}

CardSize summary_size(const RectF& area, float ui_unit, float card_scale) {
    const float u = ui_unit > 0.0f ? ui_unit : 1.0f;
    CardSize size;
    size.scale = card_scale > 0.0f ? std::max(card_scale, u) : u;
    if (area.empty()) {
        return size;
    }
    const float most = std::max(0.0f, area.w - 2.0f * kCardMarginPt * u);
    const float compact = std::min(kCardBaseWidthPt * u, most);
    const float wanted = std::min(kSummaryShare * area.w, kSummaryMaxWidthPt * size.scale);
    size.width = std::min(std::max(wanted, compact), most);
    return size;
}

} // namespace etude
