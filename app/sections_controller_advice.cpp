// SPDX-License-Identifier: GPL-3.0-or-later
// SectionsController: the cuts the suggestions propose and the hand-path hint.
#include "sections_controller_internal.hpp"

#include "etude/section_overview.hpp"
#include "etude/unit_format.hpp"

#include <span>
#include <string>

namespace etude::app {

using sections_detail::empty_sections;
using sections_detail::failed;

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
} // namespace etude::app
