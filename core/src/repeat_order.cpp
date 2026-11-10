// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/repeat_order.hpp"

#include "etude/song_builder.hpp"

#include <algorithm>
#include <bit>

namespace etude {
namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);
constexpr int kDefaultRepeatTimes = 2;

// A run of consecutive bars that sit under an ending bracket.
struct Volta {
    std::size_t first = 0;
    std::size_t last = 0;
    std::uint64_t numbers = 0; // every ending number used in the run
    std::size_t start = kNone; // where its backward repeat goes back to
    int top = 1;               // the highest ending number
};

std::vector<std::size_t> written_order(std::size_t n) {
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = i;
    }
    return order;
}

RepeatPlan written_plan(std::size_t n, bool capped, int fallbacks) {
    RepeatPlan plan;
    plan.order = written_order(n);
    plan.capped = capped;
    plan.fallbacks = fallbacks;
    return plan;
}

// The segno a D.S. goes to: the one with its label, else the first segno there is.
std::size_t find_segno(const std::vector<BarMarks>& bars, const std::string& label) {
    std::size_t first = kNone;
    for (std::size_t i = 0; i < bars.size(); ++i) {
        if (!bars[i].segno) {
            continue;
        }
        if (bars[i].segno_label == label) {
            return i;
        }
        if (first == kNone) {
            first = i;
        }
    }
    return first;
}

// The coda a To Coda goes to: the one with its label, else any; the first one after the To Coda
// bar wins, else the first one there is.
std::size_t find_coda(const std::vector<BarMarks>& bars, const std::string& label,
                      std::size_t from) {
    auto pick = [&](bool match_label) {
        std::size_t first = kNone;
        for (std::size_t i = 0; i < bars.size(); ++i) {
            if (!bars[i].coda || (match_label && bars[i].coda_label != label)) {
                continue;
            }
            if (i > from) {
                return i;
            }
            if (first == kNone) {
                first = i;
            }
        }
        return first;
    };
    const std::size_t named = label.empty() ? kNone : pick(true);
    return named != kNone ? named : pick(false);
}

} // namespace

std::uint64_t parse_ending_numbers(const char* text) {
    std::uint64_t set = 0;
    if (text == nullptr) {
        return set;
    }
    auto number = [&](const char*& s, int& out) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        long long v = 0;
        for (; *s >= '0' && *s <= '9'; ++s) {
            v = std::min(v * 10 + (*s - '0'), 1000LL);
        }
        out = static_cast<int>(v);
        return true;
    };
    auto add = [&](int from, int to) {
        for (int k = std::max(from, 1); k <= std::min(to, kMaxEndingNumber); ++k) {
            set |= std::uint64_t{1} << k;
        }
    };
    const char* s = text;
    while (*s != '\0') {
        int a = 0;
        if (!number(s, a)) {
            ++s;
            continue;
        }
        int b = a;
        const char* t = s;
        while (*t == ' ') {
            ++t;
        }
        if (*t == '-' || *t == '\xE2') { // a hyphen, or the first byte of an en dash
            const char* u = t + (*t == '-' ? 1 : 3);
            while (*u == ' ') {
                ++u;
            }
            int c = 0;
            if (number(u, c)) {
                b = c;
                s = u;
            }
        }
        add(a, std::max(a, b));
    }
    return set;
}

RepeatPlan plan_repeats(const std::vector<BarMarks>& bars, std::size_t max_bars) {
    const std::size_t n = bars.size();
    if (n == 0 ||
        std::none_of(bars.begin(), bars.end(), [](const BarMarks& b) { return b.any(); })) {
        return written_plan(n, false, 0);
    }
    const std::size_t cap =
        std::max(n, std::min({n * kMaxRepeatExpansion, max_bars, kMaxSongMeasures}));
    int fallbacks = 0;

    // Which ending numbers each bar is under.
    std::vector<std::uint64_t> under(n, 0);
    {
        std::uint64_t open = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (bars[i].endings != 0) {
                open = bars[i].endings;
            } else if (bars[i].ending_stops && open == 0) {
                ++fallbacks; // a bracket closed that was never opened
            }
            under[i] = open;
            if (bars[i].ending_stops || bars[i].backward_repeat) {
                open = 0;
            }
        }
    }

    // Forward repeats still open at the start of each bar, counting real signs only.
    std::vector<std::size_t> open_at(n, 0);
    {
        std::size_t depth = 0;
        for (std::size_t i = 0; i < n; ++i) {
            open_at[i] = depth;
            if (bars[i].forward_repeat) {
                ++depth;
            }
            if (bars[i].backward_repeat && depth > 0) {
                --depth;
            }
        }
    }

    // The backward repeats that play: the written ones, and the ones an ending run implies.
    std::vector<bool> backward(n, false);
    std::vector<int> times(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        backward[i] = bars[i].backward_repeat;
        times[i] = bars[i].times;
    }

    // Runs of bars under brackets (voltas). A run needs a repeat to be a volta: a written backward
    // repeat in it, or a forward repeat still open before it (a file that left the backward sign
    // out of its first ending). The end of every ending but the last then goes back to the start
    // when it has no backward repeat of its own. A run with neither is dropped and plays through.
    std::vector<Volta> voltas;
    std::vector<std::size_t> volta_of(n, kNone);
    int implied = 0;
    for (std::size_t i = 0; i < n;) {
        if (under[i] == 0) {
            ++i;
            continue;
        }
        Volta v;
        v.first = i;
        int run_times = 0;
        bool written = false;
        for (; i < n && under[i] != 0; ++i) {
            v.last = i;
            v.numbers |= under[i];
            written = written || bars[i].backward_repeat;
            run_times = std::max(run_times, bars[i].times);
        }
        const auto first_it = under.begin() + static_cast<std::ptrdiff_t>(v.first);
        const auto last_it = under.begin() + static_cast<std::ptrdiff_t>(v.last) + 1;
        if (!written && open_at[v.first] == 0) {
            ++fallbacks;
            std::fill(first_it, last_it, std::uint64_t{0});
            continue;
        }
        std::vector<std::size_t> made;
        for (std::size_t k = v.first; k < v.last; ++k) {
            const bool ends_ending = under[k + 1] != under[k] || bars[k + 1].endings != 0;
            if (ends_ending && !backward[k]) {
                backward[k] = true;
                times[k] = run_times;
                made.push_back(k);
            }
        }
        const bool any_backward = written || !made.empty();
        if (!any_backward) {
            ++fallbacks;
            std::fill(first_it, last_it, std::uint64_t{0});
            continue;
        }
        implied += static_cast<int>(made.size());
        v.top = static_cast<int>(std::bit_width(v.numbers)) - 1;
        std::fill(volta_of.begin() + static_cast<std::ptrdiff_t>(v.first),
                  volta_of.begin() + static_cast<std::ptrdiff_t>(v.last) + 1, voltas.size());
        voltas.push_back(v);
    }

    // Where each backward repeat goes back to: its own forward repeat (they nest like brackets),
    // else the start of its volta (a second ending's repeat has no forward of its own), else the
    // bar after the previous repeat.
    std::vector<std::size_t> start_of(n, kNone);
    std::vector<bool> is_start(n, false);
    {
        std::vector<std::size_t> open;
        std::size_t floor = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (bars[i].forward_repeat) {
                open.push_back(i);
            }
            if (!backward[i]) {
                continue;
            }
            Volta* v = volta_of[i] == kNone ? nullptr : &voltas[volta_of[i]];
            std::size_t from = floor;
            if (!open.empty()) {
                from = open.back();
                open.pop_back();
            } else if (v != nullptr && v->start != kNone) {
                from = v->start;
            }
            start_of[i] = from;
            is_start[from] = true;
            if (v != nullptr && v->start == kNone) {
                v->start = from;
            }
            if (open.empty()) {
                floor = i + 1;
            }
        }
        fallbacks += static_cast<int>(open.size()); // forward repeats that nothing closes
    }

    // Plays so far of the repeat that starts at each bar; it starts again when its start is
    // reached any way but its own jump back.
    std::vector<int> count(n, 1);
    std::size_t came_back_to = kNone;

    RepeatPlan plan;
    plan.fallbacks = fallbacks;
    plan.repeats_implied = implied;
    bool jumped = false;
    bool coda_taken = false;
    std::size_t steps = 0;
    const std::size_t max_steps = cap * 8 + n * 2 + 64;
    std::size_t i = 0;
    while (i < n) {
        if (++steps > max_steps) {
            return written_plan(n, true, fallbacks);
        }
        if (is_start[i] && came_back_to != i) {
            count[i] = 1;
        }
        came_back_to = kNone;
        const BarMarks& b = bars[i];
        const Volta* v = volta_of[i] == kNone ? nullptr : &voltas[volta_of[i]];
        std::size_t next = i + 1;
        bool plays = true;
        if (v != nullptr) {
            const int p = jumped ? v->top : count[v->start];
            plays = p <= kMaxEndingNumber && ((under[i] >> p) & 1) != 0;
        }
        bool done = false;
        if (plays) {
            if (plan.order.size() >= cap) {
                return written_plan(n, true, fallbacks);
            }
            plan.order.push_back(i);
            if (!jumped && backward[i]) {
                const std::size_t from = start_of[i];
                const int plays_wanted =
                    std::clamp(times[i] > 0 ? times[i] : kDefaultRepeatTimes, 1, kMaxRepeatTimes);
                if (count[from] < plays_wanted ||
                    (v != nullptr && v->start == from && v->top > count[from])) {
                    ++count[from];
                    next = from;
                    came_back_to = from;
                    ++plan.repeats_followed;
                }
            }
            if (next == i + 1 && jumped) {
                if (b.fine) {
                    done = true;
                } else if (b.tocoda && !coda_taken) {
                    const std::size_t coda = find_coda(bars, b.tocoda_label, i);
                    if (coda != kNone) {
                        coda_taken = true;
                        next = coda;
                        ++plan.repeats_followed;
                    } else {
                        ++plan.fallbacks;
                    }
                }
            }
            if (next == i + 1 && !done && !jumped && (b.dacapo || b.dalsegno)) {
                const std::size_t target = b.dacapo ? 0 : find_segno(bars, b.dalsegno_label);
                if (target != kNone) {
                    jumped = true;
                    next = target;
                    ++plan.repeats_followed;
                } else {
                    ++plan.fallbacks;
                }
            }
        }
        if (done) {
            break;
        }
        i = next;
    }
    return plan;
}

} // namespace etude
