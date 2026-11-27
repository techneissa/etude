// SPDX-License-Identifier: GPL-3.0-or-later
// SectionsController: the practice list and its run.
#include "sections_controller_internal.hpp"

#include <cstdio>
#include <span>
#include <vector>

namespace etude::app {

using sections_detail::failed;
using sections_detail::kSame;

namespace {

// A new practice list item stays on its section this many passes.
constexpr int kNewItemRepeats = 3;

} // namespace

// ---- The practice list

void SectionsController::plan_add(SectionId id) {
    edit([&](SongSections& w) {
        if (w.find(id) == nullptr) {
            return failed(EditError::NotFound);
        }
        PlanItem item;
        item.section = id;
        item.repeats = kNewItemRepeats;
        return w.plan_append(item);
    });
    dirty_ = true;
}

void SectionsController::plan_add_all() {
    edit([&](SongSections& w) {
        EditResult last = failed(EditError::NotFound);
        for (const Section& s : std::vector<Section>(w.sections)) {
            PlanItem item;
            item.section = s.id;
            item.repeats = kNewItemRepeats;
            last = w.plan_append(item);
            if (!last.ok) {
                break;
            }
        }
        return last;
    });
    dirty_ = true;
}

void SectionsController::plan_remove(std::size_t index) {
    edit([&](SongSections& w) { return w.plan_remove(index); });
    dirty_ = true;
}

void SectionsController::plan_move(std::size_t from, std::size_t to) {
    edit([&](SongSections& w) { return w.plan_move(from, to); });
    dirty_ = true;
}

void SectionsController::plan_set(std::size_t index, const PlanItem& item) {
    edit([&](SongSections& w) { return w.plan_set(index, item); });
    dirty_ = true;
}

void SectionsController::plan_clear() {
    edit([&](SongSections& w) {
        w.plan_clear();
        return EditResult{true, EditError::None, kNoSection};
    });
    dirty_ = true;
}

void SectionsController::plan_start(std::int64_t now_ns) {
    const SongSections* cur = current();
    if (cur == nullptr || cur->plan.empty()) {
        say("Add sections to the practice list first");
        return;
    }
    if (plan_.active()) {
        apply_step(plan_.stop(), now_ns);
    }
    const PlanStep step = plan_.start(*cur, cur->plan, songs_.speed_percent(), 0);
    if (step.kind == PlanStep::Kind::None) {
        say("None of the list's sections exist any more");
        return;
    }
    apply_step(step, now_ns);
    songs_.play(now_ns);
}

bool SectionsController::practise_bars(const BarPractice& practice, std::int64_t now_ns,
                                       const TroubleBars* run_bars) {
    if (!bound()) {
        say("Open a song first");
        return false;
    }
    if (practice.count <= 0) {
        say("Those bars are too short to loop");
        return false;
    }
    // A loop the transport would refuse is found before anything is edited, so a refusal never
    // leaves the list changed.
    for (int i = 0; i < practice.count; ++i) {
        const BarLoop& loop = practice.loops[static_cast<std::size_t>(i)];
        if (!(loop.end_s - loop.start_s >= kMinLoopSeconds)) {
            say("Those bars are too short to loop");
            return false;
        }
    }
    const Song& song = songs_.song().song;
    int pieces = 0;
    EditError first_error = EditError::None; // the first piece that could not be made
    const EditResult r = edit([&](SongSections& w) {
        w.plan_clear();
        for (int i = 0; i < practice.count; ++i) {
            const BarLoop& loop = practice.loops[static_cast<std::size_t>(i)];
            // A stretch that crosses a cut the song already has is played as the pieces between
            // its cuts: a section never spans a cut.
            double from = loop.start_s;
            std::vector<double> ends;
            for (const double cut : w.markers()) {
                if (cut > from + kSame && cut < loop.end_s - kSame) {
                    ends.push_back(cut);
                }
            }
            ends.push_back(loop.end_s);
            for (const double to : ends) {
                const EditResult made = w.add_range(song, from, to, false);
                from = to;
                if (!made.ok) {
                    first_error = first_error == EditError::None ? made.error : first_error;
                    continue;
                }
                const EditResult added = w.plan_append(bar_plan_item(made.id, practice.kind));
                if (added.ok) {
                    ++pieces;
                } else {
                    first_error = first_error == EditError::None ? added.error : first_error;
                }
            }
        }
        return pieces > 0 ? EditResult{true, EditError::None, kNoSection}
                          : failed(first_error != EditError::None ? first_error
                                                                  : EditError::TooShort);
    });
    if (!r.ok) {
        return false;
    }
    after_edit(now_ns);
    const int bars = practice.count;
    if (practice.single_loop()) {
        // Neighbouring bars are one stretch: a plain A-B loop that plays until it is stopped.
        if (plan_.active()) {
            apply_step(plan_.stop(), now_ns);
        }
        const BarLoop& loop = practice.loops[0];
        if (!songs_.set_loop(loop.start_s, loop.end_s, now_ns)) {
            say("Those bars cannot be looped");
            return false;
        }
        songs_.replay(now_ns);
        dirty_ = true;
        say("Bars looped together; the practice list keeps them");
        return true;
    }
    plan_start(now_ns);
    // One section made for each bar: the list's items are the run's bars, in order.
    if (run_bars != nullptr && practice.kind == BarPracticeKind::OneByOne && plan_.active() &&
        pieces == practice.count && run_bars->count == practice.count) {
        run_.start(std::span<const TroubleBar>(run_bars->bar.data(),
                                               static_cast<std::size_t>(run_bars->count)));
        dirty_ = true;
    }
    char text[96];
    if (practice.kind == BarPracticeKind::OneByOne) {
        std::snprintf(text, sizeof(text), "Practice list: %d bar%s, one by one", bars,
                      bars == 1 ? "" : "s");
    } else {
        std::snprintf(text, sizeof(text), "Practice list: %d stretch%s, once each", bars,
                      bars == 1 ? "" : "es");
    }
    say(text);
    return true;
}

void SectionsController::plan_stop(std::int64_t now_ns) {
    if (!plan_.active()) {
        return;
    }
    if (run_.active()) {
        run_command(RunCommand::End, now_ns);
        return;
    }
    apply_step(plan_.stop(), now_ns);
    say("Practice list stopped");
}

void SectionsController::plan_skip(int direction, std::int64_t now_ns) {
    const SongSections* cur = current();
    if (!plan_.active() || cur == nullptr) {
        return;
    }
    if (run_.active()) {
        // Forward is the card's Skip (while a bar plays: give it up; at the check-in: pass over the
        // next bar); back: this bar again.
        run_command(direction > 0 ? RunCommand::Skip : RunCommand::Again, now_ns);
        return;
    }
    apply_step(direction > 0 ? plan_.skip_next(*cur) : plan_.skip_previous(*cur), now_ns);
}
} // namespace etude::app
