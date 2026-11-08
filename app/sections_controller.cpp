// SPDX-License-Identifier: GPL-3.0-or-later
#include "sections_controller.hpp"

#include "etude/roll_view.hpp"
#include "etude/section_overview.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace etude::app {

namespace {

constexpr double kSame = 1e-6;
// A new practice list item stays on its section this many passes.
constexpr int kNewItemRepeats = 3;

const SongSections& empty_sections() {
    static const SongSections kEmpty;
    return kEmpty;
}

EditResult failed(EditError error) { return EditResult{false, error, kNoSection}; }

} // namespace

SectionsController::SectionsController(SongController& songs, SectionLibrary& library)
    : songs_(songs), library_(library) {}

const SongSections* SectionsController::current() const {
    return hash_ != 0 ? library_.find(hash_) : nullptr;
}

std::string SectionsController::take_message() {
    std::string out;
    out.swap(message_);
    return out;
}

void SectionsController::say_error(EditError error) {
    switch (error) {
    case EditError::TooShort:
        say("That would make a section too short");
        break;
    case EditError::TooMany:
        say("That is as many sections as a song can have");
        break;
    case EditError::AtMarker:
        say("There is already a cut there");
        break;
    case EditError::Overlaps:
        say("The loop crosses a cut: move or remove the cut first");
        break;
    case EditError::NotAdjacent:
    case EditError::NotEnough:
        say("There is no section after this one to join");
        break;
    case EditError::None:
    case EditError::NotFound:
    case EditError::OutOfRange:
        say("That can't be done there");
        break;
    }
}

// ---- Binding

void SectionsController::bind(std::int64_t /*now_ns*/) {
    serial_ = songs_.load_serial();
    hash_ = songs_.has_song() ? songs_.song().hash : 0;
    history_.clear();
    ++edit_serial_;
    plan_.reset();
    run_.reset();
    suggestions_on_ = false;
    suggestion_ = SectionSuggestions{};
    cuts_.clear();
    if (bound()) {
        // Data a damaged or hand-edited file left behind is repaired to fit this song.
        const Song& song = songs_.song().song;
        if (const SongSections* cur = current(); cur != nullptr && !cur->valid(song)) {
            SongSections fixed = *cur;
            fixed.fit_to_song(song);
            library_.put(std::move(fixed));
        }
    }
    dirty_ = true;
}

void SectionsController::history_changed() {
    dirty_ = true;
    if (suggestions_on_) {
        set_suggestions(true);
    }
}

void SectionsController::set_history_note(HistoryNote note) {
    if (history_note_ != note) {
        history_note_ = note;
        dirty_ = true;
    }
}

// ---- Editing

template <class F>
EditResult SectionsController::edit(F&& f) {
    if (!bound()) {
        return failed(EditError::NotFound);
    }
    const SongSections* cur = current();
    SongSections before = cur != nullptr ? *cur : SongSections{};
    before.song_hash = hash_;
    SongSections work = before;
    const EditResult r = f(work);
    if (!r.ok) {
        say_error(r.error);
        return r;
    }
    if (!(work == before)) {
        commit(std::move(work), before);
    }
    return r;
}

void SectionsController::commit(SongSections work, const SongSections& before) {
    history_.push(before);
    ++edit_serial_;
    library_.put(std::move(work));
    if (library_.find(hash_) != nullptr) {
        library_.edit(hash_); // marks the song as just used
    }
    dirty_ = true;
}

void SectionsController::after_edit(std::int64_t now_ns, const PlanStep* merged) {
    refresh_cuts();
    if (plan_.active()) {
        const SongSections& sections = current() != nullptr ? *current() : empty_sections();
        apply_step(merged != nullptr ? *merged : plan_.refresh(sections), now_ns);
    }
    dirty_ = true;
}

void SectionsController::apply_step(const PlanStep& step, std::int64_t now_ns) {
    songs_.apply_plan_step(step, now_ns);
    if (step.kind == PlanStep::Kind::Finished) {
        say("Practice list finished");
    }
    // A list that ends or stops takes its run with it (the run's own End asks for the stop, and
    // finds the run done).
    if (step.kind == PlanStep::Kind::Finished || step.kind == PlanStep::Kind::Stopped) {
        run_.abandon();
    }
    dirty_ = true;
}

void SectionsController::apply_run_step(const RunStep& step, std::int64_t now_ns) {
    dirty_ = true;
    const SongSections* cur = current();
    switch (step.kind) {
    case RunStep::Kind::None:
        break;
    case RunStep::Kind::Play:
        if (cur != nullptr && plan_.active()) {
            apply_step(plan_.jump_to_item(*cur, static_cast<std::size_t>(step.bar)), now_ns);
            if (!songs_.playing()) {
                songs_.play(now_ns);
            }
        }
        break;
    case RunStep::Kind::Pause:
        songs_.pause(now_ns);
        break;
    case RunStep::Kind::End:
        if (plan_.active()) {
            apply_step(plan_.stop(), now_ns);
        }
        songs_.pause(now_ns);
        break;
    }
}

void SectionsController::run_command(RunCommand command, std::int64_t now_ns) {
    if (!run_.active()) {
        return;
    }
    apply_run_step(run_.command(command), now_ns);
}

void SectionsController::run_dismiss() {
    if (run_.phase() == RunPhase::Done) {
        run_.reset();
        dirty_ = true;
    }
}

bool SectionsController::split_at(double seconds, std::int64_t now_ns) {
    if (!bound()) {
        return false;
    }
    const Song& song = songs_.song().song;
    const EditResult r = edit([&](SongSections& w) { return w.add_marker(song, seconds, false); });
    if (r.ok) {
        message_ = "Cut at " + position_label(song, seconds);
        after_edit(now_ns);
    }
    return r.ok;
}

void SectionsController::add_marker_at_playhead(std::int64_t now_ns) {
    if (!bound()) {
        return;
    }
    const Song& song = songs_.song().song;
    const double at = songs_.position_s();
    const EditResult r = edit([&](SongSections& w) { return w.add_marker(song, at, true); });
    if (r.ok) {
        message_ = "Cut at " + position_label(song, snap_to_grid(song, at, false));
        after_edit(now_ns);
    }
}

void SectionsController::save_loop_as_section(std::int64_t now_ns) {
    if (!bound() || !songs_.loop_enabled()) {
        say("Set a loop first");
        return;
    }
    const Song& song = songs_.song().song;
    const double a = songs_.loop_start_s();
    const double b = songs_.loop_end_s();
    const EditResult r = edit([&](SongSections& w) { return w.add_range(song, a, b, true); });
    if (r.ok) {
        say("Loop saved as a section");
        after_edit(now_ns);
    }
}

bool SectionsController::move_marker(std::size_t marker, double seconds, std::int64_t now_ns) {
    if (!bound()) {
        return false;
    }
    const Song& song = songs_.song().song;
    const EditResult r =
        edit([&](SongSections& w) { return w.move_marker(song, marker, seconds, false); });
    if (r.ok) {
        after_edit(now_ns);
    }
    return r.ok;
}

bool SectionsController::rename(SectionId id, std::string_view name) {
    const EditResult r = edit([&](SongSections& w) { return w.rename(id, name); });
    if (r.ok) {
        dirty_ = true;
    }
    return r.ok;
}

bool SectionsController::merge_with_next(SectionId id, std::int64_t now_ns) {
    if (!bound()) {
        return false;
    }
    const Song& song = songs_.song().song;
    std::vector<SectionId> absorbed;
    SectionId survivor = kNoSection;
    const EditResult r = edit([&](SongSections& w) {
        const auto it = std::find_if(w.sections.begin(), w.sections.end(),
                                     [&](const Section& s) { return s.id == id; });
        if (it == w.sections.end() || it + 1 == w.sections.end()) {
            return failed(EditError::NotAdjacent);
        }
        const SectionId ids[2] = {it->id, (it + 1)->id};
        survivor = ids[0];
        return w.merge(song, ids, &absorbed);
    });
    if (r.ok) {
        say("Sections joined");
        PlanStep step;
        if (plan_.active() && current() != nullptr) {
            step = plan_.sections_merged(*current(), survivor, absorbed);
        }
        after_edit(now_ns, plan_.active() ? &step : nullptr);
    }
    return r.ok;
}

bool SectionsController::erase_section(SectionId id, std::int64_t now_ns) {
    if (!bound()) {
        return false;
    }
    const Song& song = songs_.song().song;
    const EditResult r = edit([&](SongSections& w) { return w.erase(song, id); });
    if (r.ok) {
        say("Section deleted");
        after_edit(now_ns);
    }
    return r.ok;
}

bool SectionsController::undo(std::int64_t now_ns) {
    if (!bound() || !history_.can_undo()) {
        return false;
    }
    const SongSections* cur = current();
    SongSections now_state = cur != nullptr ? *cur : SongSections{};
    now_state.song_hash = hash_;
    std::optional<SongSections> back = history_.undo(now_state);
    if (!back) {
        return false;
    }
    back->song_hash = hash_;
    back->fit_to_song(songs_.song().song);
    ++edit_serial_;
    library_.put(std::move(*back));
    if (library_.find(hash_) != nullptr) {
        library_.edit(hash_);
    }
    say("Undone");
    after_edit(now_ns);
    return true;
}

// ---- Looping a section

SectionId SectionsController::looping_section() const {
    if (!songs_.loop_enabled()) {
        return kNoSection;
    }
    const SongSections* cur = current();
    if (cur == nullptr) {
        return kNoSection;
    }
    for (const Section& s : cur->sections) {
        if (std::fabs(s.start_s - songs_.loop_start_s()) < kSame &&
            std::fabs(s.end_s - songs_.loop_end_s()) < kSame) {
            return s.id;
        }
    }
    return kNoSection;
}

void SectionsController::select_section(SectionId id, std::int64_t now_ns) {
    const SongSections* cur = current();
    if (cur == nullptr || cur->find(id) == nullptr) {
        return;
    }
    if (plan_.active()) {
        apply_step(plan_.stop(), now_ns);
    }
    if (looping_section() == id) {
        songs_.clear_loop(now_ns);
        dirty_ = true;
        return;
    }
    const LoopSpan span = cur->span_of(id);
    if (span.ok && songs_.set_loop(span.start_s, span.end_s, now_ns)) {
        songs_.seek(span.start_s, now_ns);
    }
    dirty_ = true;
}

void SectionsController::step(int direction, std::int64_t now_ns) {
    if (plan_.active()) {
        plan_skip(direction, now_ns);
        return;
    }
    const SongSections* cur = current();
    if (cur == nullptr || cur->sections.empty()) {
        return;
    }
    const auto& list = cur->sections;
    std::size_t index = 0;
    const SectionId now_id = looping_section();
    if (now_id != kNoSection) {
        index = static_cast<std::size_t>(
            std::find_if(list.begin(), list.end(), [&](const Section& s) { return s.id == now_id; }) -
            list.begin());
        index = direction > 0 ? (index + 1) % list.size() : (index + list.size() - 1) % list.size();
    } else {
        const Section* at = cur->at(songs_.position_s());
        index = at != nullptr ? static_cast<std::size_t>(at - list.data()) : 0;
    }
    select_section(list[index].id, now_ns);
}

// ---- Suggestions

void SectionsController::refresh_cuts() {
    cuts_.clear();
    if (!suggestions_on_) {
        return;
    }
    const SongSections* cur = current();
    cuts_ = proposed_cuts(suggestion_, cur != nullptr ? *cur : empty_sections());
}

void SectionsController::set_suggestions(bool on) {
    suggestions_on_ = on && bound();
    if (suggestions_on_) {
        const std::span<const RunRecord> runs =
            runs_ != nullptr ? std::span<const RunRecord>(*runs_) : std::span<const RunRecord>();
        suggestion_ = suggest_sections(songs_.song().song, runs, hash_);
        refresh_cuts();
        if (cuts_.empty()) {
            say(suggestion_.sections.empty() ? "Nothing to suggest for this song"
                                             : "Every suggested cut is in place");
        }
    } else {
        suggestion_ = SectionSuggestions{};
        cuts_.clear();
    }
    dirty_ = true;
}

bool SectionsController::accept_cut(std::size_t index, std::int64_t now_ns) {
    if (index >= cuts_.size()) {
        return false;
    }
    const double t = cuts_[index].time_s;
    return split_at(t, now_ns);
}

bool SectionsController::accept_all_cuts(bool trouble_only, std::int64_t now_ns) {
    if (!bound() || !suggestions_on_) {
        return false;
    }
    const Song& song = songs_.song().song;
    int placed = 0;
    const EditResult r = edit([&](SongSections& w) {
        placed = apply_suggestions(w, song, suggestion_, trouble_only);
        return placed > 0 ? EditResult{true, EditError::None, kNoSection}
                          : failed(EditError::OutOfRange);
    });
    if (r.ok) {
        message_ = placed == 1 ? "1 cut added" : std::to_string(placed) + " cuts added";
        after_edit(now_ns);
    }
    return r.ok;
}

// ---- Listening

void SectionsController::audition(double start_s, double end_s, std::int64_t now_ns) {
    songs_.audition(start_s, end_s, now_ns);
}

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

// ---- Hand path

void SectionsController::refresh_hand() {
    hand_can_apply_ = false;
    hand_apply_.clear();
    hand_hint_.clear();
    hand_scope_ = "Whole song";
    if (!bound()) {
        return;
    }
    const Song& song = songs_.song().song;
    const SongSections* cur = current();
    const Section* section = nullptr;
    if (cur != nullptr) {
        section = cur->find(looping_);
    }
    if (section != nullptr) {
        hand_scope_ = section_label(*section);
    }
    const std::span<const RunRecord> runs =
        runs_ != nullptr ? std::span<const RunRecord>(*runs_) : std::span<const RunRecord>();
    if (history_note_ == HistoryNote::Loading) {
        hand_hint_ = kHistoryLoadingText;
        return;
    }
    if (history_note_ == HistoryNote::Busy) {
        hand_hint_ = kHistoryBusyText;
        return;
    }
    if (history_note_ == HistoryNote::Unwritable) {
        hand_hint_ = kHistoryUnwritableText;
        return;
    }
    const HandPathStatus status = hand_path(runs, hash_, scope_of_section(song, section));
    const PathSuggestion suggestion = next_practice(status);
    hand_hint_ = path_hint(status, suggestion);
    if (suggestion.advice != PathAdvice::AllDone) {
        hand_hands_ = suggestion.hands;
        hand_speed_ = suggestion.speed_percent;
        hand_can_apply_ = true;
        hand_apply_ = std::string(hands_name(suggestion.hands)) + " \xC2\xB7 " +
                      format_unit_string(suggestion.speed_percent, Unit::Percent);
    }
}

void SectionsController::apply_hand_path(std::int64_t now_ns) {
    if (!hand_can_apply_) {
        return;
    }
    songs_.set_practice_hands(hand_hands_, now_ns);
    songs_.set_speed(hand_speed_, now_ns);
    say("Hands and speed set");
}

// ---- Per frame

void SectionsController::update(std::int64_t now_ns) {
    if (songs_.load_serial() != serial_) {
        bind(now_ns);
    }
    for (const LoopPass& pass : songs_.take_loop_passes()) {
        const SongSections* cur = current();
        if (!plan_.active() || cur == nullptr) {
            continue;
        }
        if (run_.active()) {
            // The run decides when a bar is done (a clean pass) and waits at a check-in; the
            // list only holds the loops.
            apply_run_step(
                run_.on_pass({pass.clean, pass.graded, pass.missed, pass.wrong, pass.perfect,
                              pass.good, pass.early, pass.late, pass.played, pass.combo}),
                now_ns);
            continue;
        }
        apply_step(plan_.on_pass(*cur, pass.clean), now_ns);
    }
    // The player started the song again at a check-in (the transport button, the space key): that
    // is Again, and the check-in goes.
    if (run_.phase() == RunPhase::CheckIn && songs_.playing()) {
        run_.command(RunCommand::Again);
        dirty_ = true;
    }
    // A running list always has its section looping (every step it takes while running sets the
    // loop or keeps it). Something else that ends the loop (a seek outside it, a scrub, the loop
    // toggle, the \ key) leaves the list nothing to advance it: it stops, and says so. A run ends
    // the way its End button does, which also pauses the song so the summary is not dismissed by
    // the song going on.
    if (plan_.active() && !songs_.loop_enabled()) {
        if (run_.active()) {
            apply_run_step(run_.abandon(), now_ns);
            say("Run ended: its loop was cleared");
        } else {
            apply_step(plan_.stop(), now_ns);
            say("Practice list stopped: its loop ended");
        }
    }
    const SectionId looping = looping_section();
    const bool saveable = bound() && songs_.loop_enabled() && looping == kNoSection &&
                          !plan_.active();
    if (looping != looping_ || saveable != loop_saveable_) {
        looping_ = looping;
        loop_saveable_ = saveable;
        dirty_ = true;
    }
    const int item = plan_.active() ? static_cast<int>(plan_.item_index()) : -1;
    const std::size_t passes = plan_.active() ? static_cast<std::size_t>(plan_.passes_done()) : 0;
    if (item != last_plan_item_ || passes != last_plan_pass_) {
        last_plan_item_ = item;
        last_plan_pass_ = passes;
        dirty_ = true;
    }
    if (dirty_) {
        rebuild_view();
    }
    view_.can_undo = history_.can_undo();
}

void SectionsController::rebuild_view() {
    dirty_ = false;
    ++revision_;
    ui::SectionsView& v = view_;
    v.revision = revision_;
    v.available = bound();
    v.song = bound() ? &songs_.song().song : nullptr;
    v.song_serial = serial_;
    v.duration_s = bound() ? songs_.duration_s() : 0.0;
    v.sections.clear();
    v.markers.clear();
    v.plan.clear();
    v.cuts.clear();
    v.looping = looping_;
    v.loop_can_be_saved = loop_saveable_;
    v.can_undo = history_.can_undo();
    v.undo_depth = history_.depth();
    v.suggestions_on = suggestions_on_;
    v.suggestions_from_history = suggestion_.from_history;
    const SongSections* cur = current();
    v.data = cur != nullptr ? *cur : SongSections{};
    if (cur != nullptr) {
        for (const Section& s : cur->sections) {
            v.sections.push_back(
                {s.id, section_label(s), section_detail_text(s), s.start_s, s.end_s, s.custom_name});
        }
        v.markers = cur->markers();
        for (const PlanItem& item : cur->plan) {
            ui::PlanRow row;
            row.section = item.section;
            row.item = item;
            row.detail = plan_item_text(item);
            const Section* s = cur->find(item.section);
            row.exists = s != nullptr;
            row.name = s != nullptr ? section_label(*s) : "(deleted)";
            v.plan.push_back(std::move(row));
        }
    }
    for (const ProposedCut& c : cuts_) {
        v.cuts.push_back({c.time_s, c.kind == SuggestionKind::Trouble, c.measure});
    }
    v.plan_running = plan_.active();
    v.run_active = run_.active();
    v.run_can_skip = run_.phase() == RunPhase::Playing ||
                     (run_.phase() == RunPhase::CheckIn && run_.next_bar() >= 0);
    v.plan_item = plan_.active() ? static_cast<int>(plan_.item_index()) : -1;
    v.plan_status.clear();
    if (plan_.active() && plan_.item_index() < plan_.items().size()) {
        const PlanItem& item = plan_.items()[plan_.item_index()];
        PlanStatus st;
        st.active = true;
        const PracticePlan::LivePosition live = cur != nullptr
                                                    ? plan_.live_position(*cur)
                                                    : PracticePlan::LivePosition{};
        st.item_index = live.position;
        st.item_count = live.count;
        st.passes_done = plan_.passes_done();
        st.repeats = item.repeats;
        v.plan_status = plan_status_text(st, cur != nullptr ? cur->find(item.section) : nullptr);
    }
    refresh_hand();
    v.hand_scope = hand_scope_;
    v.hand_hint = hand_hint_;
    v.hand_apply = hand_apply_;
    v.hand_can_apply = hand_can_apply_;
}

// ---- Saved state

void SectionsController::save_into(SessionState& session) const {
    session.section_id = static_cast<int>(looping_);
    session.plan_item = plan_.active() ? static_cast<int>(plan_.item_index()) + 1 : 0;
    if (!bound()) {
        session.section_id = 0;
        session.plan_item = 0;
    }
}

void SectionsController::restore(const SessionState& session, std::int64_t now_ns) {
    if (songs_.load_serial() != serial_) {
        bind(now_ns);
    }
    const SongSections* cur = current();
    if (cur == nullptr) {
        return;
    }
    if (session.section_id > 0 && cur->find(static_cast<SectionId>(session.section_id)) != nullptr &&
        looping_section() != static_cast<SectionId>(session.section_id)) {
        const LoopSpan span = cur->span_of(static_cast<SectionId>(session.section_id));
        if (span.ok) {
            songs_.set_loop(span.start_s, span.end_s, now_ns);
        }
    }
    if (session.plan_item > 0 && static_cast<std::size_t>(session.plan_item) <= cur->plan.size() &&
        cur->find(cur->plan[static_cast<std::size_t>(session.plan_item) - 1].section) != nullptr) {
        // The speed to go back to: the saved one, unless the item's own speed-up set it.
        const std::size_t first = static_cast<std::size_t>(session.plan_item) - 1;
        const int base = cur->plan[first].speed_up.enabled ? 100 : songs_.speed_percent();
        const PlanStep step = plan_.start(*cur, cur->plan, base, first);
        apply_step(step, now_ns);
        if (step.kind == PlanStep::Kind::SetLoop && session.position_s >= step.loop_start_s &&
            session.position_s < step.loop_end_s) {
            songs_.seek(session.position_s, now_ns); // back to where in the section it was
        }
    }
    dirty_ = true;
}

} // namespace etude::app
