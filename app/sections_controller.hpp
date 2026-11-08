// SPDX-License-Identifier: GPL-3.0-or-later
// The sections of the loaded song, as the touch UI works with them: edits with undo, the practice
// list running on the song's loop, the cuts the suggestions propose, listening to a stretch of the
// song, and the hand-path hint. It binds the UI's SectionsPort (app/ui/sections_port.hpp) to the
// SectionLibrary the settings file keeps and to the SongController. No SDL, no files.
//
// Each frame, after SongController::update:  update(now)
//   - notices a new song and binds to its sections (damaged data is repaired to fit the song);
//   - feeds the loop passes the song controller counted to the running practice list;
//   - keeps the "looping" section in step with the song's A-B loop.
#pragma once

#include "song_controller.hpp"
#include "ui/sections_port.hpp"

#include "etude/hand_path.hpp"
#include "etude/practice_plan.hpp"
#include "etude/results_card.hpp"
#include "etude/run_record.hpp"
#include "etude/section_editing.hpp"
#include "etude/section_suggestions.hpp"
#include "etude/sections.hpp"
#include "etude/session_state.hpp"
#include "etude/trouble_run.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace etude::app {

class SectionsController final : public ui::SectionsPort {
  public:
    // Both must outlive this object. The library is the one the settings file saves.
    SectionsController(SongController& songs, SectionLibrary& library);

    // The practice history (null for none), for the suggestions and the hand path. Call
    // history_changed() after a run is added.
    void set_history(const std::vector<RunRecord>* runs) { runs_ = runs; }
    void history_changed();
    // What the practice history is doing, for the hand-path hint: it is still being read from disk
    // (the runs held are not all of them), or its database is busy and new runs are only kept in
    // memory for now. Either way the hint says so instead of advising.
    enum class HistoryNote { None, Loading, Busy, Unwritable };
    // What each note says: on the hand path card while it holds, and as a toast when it matters
    // to what the player just did (a run finished while the history was loading or busy).
    static constexpr const char* kHistoryLoadingText = "Loading practice history\xE2\x80\xA6";
    static constexpr const char* kHistoryBusyText =
        "Practice history is busy; new runs are kept and saved later.";
    // The history file or its folder cannot be written by the program: runs are kept until that is fixed.
    static constexpr const char* kHistoryUnwritableText =
        "Practice history can't be saved: Etude may not change its file or folder.";
    void set_history_note(HistoryNote note);

    void update(std::int64_t now_ns);

    // ---- Saved state

    // Takes the selected section and the running practice list item into the session.
    void save_into(SessionState& session) const;
    // Brings them back after the song and the session were applied; what no longer exists is
    // dropped.
    void restore(const SessionState& session, std::int64_t now_ns);

    // ---- Keys

    // The previous (-1) or next (+1) section looped; while a practice list runs, its skip.
    void step(int direction, std::int64_t now_ns);

    // ---- The results card's bars

    // Practises the bars the results card chose. The practice list becomes those bars (one section
    // each, made if the song has none there; one undo step) and runs: One by one plays each until a
    // pass is clean; Together plays neighbouring bars as one A-B loop until stopped, or, when the
    // bars are not neighbours, each stretch once through the list. Returns false, saying why, when
    // no bar could be made a section. With `run_bars` (the same bars, one by one) the list runs as
    // a trouble-bar run: a check-in after each bar, skip and end, a summary at the end.
    bool practise_bars(const BarPractice& practice, std::int64_t now_ns,
                       const TroubleBars* run_bars = nullptr);

    // ---- The trouble-bar run

    // The run: its phase, bars and what each came to. The check-in and the summary draw from it.
    const TroubleRun& run() const { return run_; }
    // Next bar, Again, Skip or Stop, from the check-in's buttons, the band and the keys.
    void run_command(RunCommand command, std::int64_t now_ns) override;
    // Forgets a finished run's summary.
    void run_dismiss();

    // ---- ui::SectionsPort
    const ui::SectionsView& view() const override { return view_; }
    std::uint32_t edit_serial() const override { return edit_serial_; }
    std::string take_message() override;
    void select_section(SectionId id, std::int64_t now_ns) override;
    void add_marker_at_playhead(std::int64_t now_ns) override;
    void save_loop_as_section(std::int64_t now_ns) override;
    bool split_at(double seconds, std::int64_t now_ns) override;
    bool move_marker(std::size_t marker, double seconds, std::int64_t now_ns) override;
    bool rename(SectionId id, std::string_view name) override;
    bool merge_with_next(SectionId id, std::int64_t now_ns) override;
    bool erase_section(SectionId id, std::int64_t now_ns) override;
    bool undo(std::int64_t now_ns) override;
    void set_suggestions(bool on) override;
    bool accept_cut(std::size_t index, std::int64_t now_ns) override;
    bool accept_all_cuts(bool trouble_only, std::int64_t now_ns) override;
    void audition(double start_s, double end_s, std::int64_t now_ns) override;
    void audition_repeating(double start_s, double end_s, std::int64_t now_ns) override {
        songs_.audition_repeating(start_s, end_s, now_ns);
    }
    void stop_audition(std::int64_t now_ns) override { songs_.stop_audition(now_ns); }
    bool auditioning() const override { return songs_.auditioning(); }
    double audition_position_s() const override { return songs_.audition_position_s(); }
    void plan_add(SectionId id) override;
    void plan_add_all() override;
    void plan_remove(std::size_t index) override;
    void plan_move(std::size_t from, std::size_t to) override;
    void plan_set(std::size_t index, const PlanItem& item) override;
    void plan_clear() override;
    void plan_start(std::int64_t now_ns) override;
    void plan_stop(std::int64_t now_ns) override;
    void plan_skip(int direction, std::int64_t now_ns) override;
    void apply_hand_path(std::int64_t now_ns) override;

  private:
    const SongSections* current() const;
    bool bound() const { return songs_.has_song() && hash_ != 0; }
    // Runs `f` on a copy of the song's sections and keeps the result when it succeeded and changed
    // something, as one undo step.
    template <class F>
    EditResult edit(F&& f);
    void commit(SongSections work, const SongSections& before);
    void bind(std::int64_t now_ns);
    void after_edit(std::int64_t now_ns, const PlanStep* merged = nullptr);
    void apply_step(const PlanStep& step, std::int64_t now_ns);
    void apply_run_step(const RunStep& step, std::int64_t now_ns);
    void refresh_cuts();
    void refresh_hand();
    void rebuild_view();
    SectionId looping_section() const;
    void say(const char* text) { message_ = text; }
    void say_error(EditError error);

    SongController& songs_;
    SectionLibrary& library_;
    const std::vector<RunRecord>* runs_ = nullptr;
    HistoryNote history_note_ = HistoryNote::None;

    std::uint32_t serial_ = 0xFFFFFFFFu; // the song controller's load_serial this is bound to
    std::uint64_t hash_ = 0;
    SectionHistory history_;
    PracticePlan plan_;
    TroubleRun run_;
    bool suggestions_on_ = false;
    SectionSuggestions suggestion_;
    std::vector<ProposedCut> cuts_;
    std::string message_;

    SectionId looping_ = kNoSection;
    bool loop_saveable_ = false;
    std::string hand_scope_;
    std::string hand_hint_;
    std::string hand_apply_;
    HandFilter hand_hands_ = HandFilter::Right;
    int hand_speed_ = 0;
    bool hand_can_apply_ = false;
    bool dirty_ = true;
    std::uint32_t revision_ = 0;
    std::uint32_t edit_serial_ = 0; // see SectionsPort::edit_serial
    std::size_t last_plan_pass_ = 0;
    int last_plan_item_ = -1;
    ui::SectionsView view_;
};

} // namespace etude::app
