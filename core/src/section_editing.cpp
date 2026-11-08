// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/section_editing.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

constexpr double kSame = 1e-6; // two times closer than this are one instant

double song_length(const Song& song) {
    return std::isfinite(song.duration_s) && song.duration_s > 0.0 ? song.duration_s : 0.0;
}

bool has_boundary(const SongSections& sections, double t) {
    for (const Section& s : sections.sections) {
        if (std::fabs(s.end_s - t) <= kSame) {
            return true;
        }
    }
    return false;
}

AuditionRange clamp_range(const Song& song, double start_s, double end_s) {
    const double length = song_length(song);
    AuditionRange r;
    if (!(length > 0.0) || std::isnan(start_s) || std::isnan(end_s)) {
        return r;
    }
    const double want = end_s - start_s;
    r.start_s = std::clamp(start_s, 0.0, length);
    r.end_s = std::clamp(end_s, 0.0, length);
    // A range pushed against an end keeps its length by sliding along the song.
    if (r.end_s - r.start_s < std::min(want, length) - kSame) {
        if (start_s < 0.0) {
            r.start_s = 0.0;
            r.end_s = std::min(length, want);
        } else {
            r.end_s = length;
            r.start_s = std::max(0.0, length - want);
        }
    }
    return r;
}

} // namespace

void SectionHistory::push(const SongSections& before) {
    if (!states_.empty() && states_.back() == before) {
        return;
    }
    states_.push_back(before);
    while (states_.size() > kMaxSectionUndo) {
        states_.pop_front();
    }
}

std::optional<SongSections> SectionHistory::undo(const SongSections& current) {
    if (states_.empty()) {
        return std::nullopt;
    }
    SongSections restored = std::move(states_.back());
    states_.pop_back();
    restored.next_id = std::max(restored.next_id, current.next_id);
    return restored;
}

std::vector<ProposedCut> proposed_cuts(const SectionSuggestions& suggestions,
                                       const SongSections& sections, bool trouble_only) {
    std::vector<ProposedCut> out;
    const auto& list = suggestions.sections;
    for (std::size_t i = 1; i < list.size(); ++i) {
        // The boundary before list[i].
        const SuggestedSection& before = list[i - 1];
        const SuggestedSection& after = list[i];
        const double t = after.start_s;
        const bool trouble =
            before.kind == SuggestionKind::Trouble || after.kind == SuggestionKind::Trouble;
        if (t <= kSame || has_boundary(sections, t) || (trouble_only && !trouble)) {
            continue;
        }
        if (!out.empty() && std::fabs(out.back().time_s - t) <= kSame) {
            continue;
        }
        out.push_back({t, trouble ? SuggestionKind::Trouble : SuggestionKind::Phrase,
                       after.first_measure});
    }
    return out;
}

AuditionRange audition_from(const Song& song, double cursor_s) {
    return clamp_range(song, cursor_s, cursor_s + kAuditionSeconds);
}

AuditionRange audition_across(const Song& song, double cursor_s) {
    return clamp_range(song, cursor_s - kAuditionSeconds, cursor_s + kAuditionSeconds);
}

AuditionRange audition_tap(const Song& song, const SongSections& sections, double point_s) {
    const double length = song_length(song);
    if (!(length > 0.0) || std::isnan(point_s)) {
        return {};
    }
    const double point = std::clamp(point_s, 0.0, length);
    const Section* in = sections.at(point);
    AuditionRange r;
    r.start_s = point;
    r.end_s = in != nullptr ? std::min(in->end_s, length) : length;
    if (in != nullptr && in->end_s - point <= kSame && in != &sections.sections.back()) {
        // A hair below a cut is on it: the section after it plays.
        const Section& next = *(in + 1);
        r.start_s = in->end_s;
        r.end_s = std::min(next.end_s, length);
    }
    return r.valid() ? r : audition_from(song, point);
}

AuditionRange audition_idle(const Song& song, double cursor_s) {
    const double length = song_length(song);
    if (!(length > 0.0) || std::isnan(cursor_s)) {
        return {};
    }
    const double start = std::clamp(cursor_s, 0.0, length);
    AuditionRange r;
    r.start_s = start;
    r.end_s = std::min(start + kIdlePlaySeconds, length);
    return r.end_s - r.start_s >= kIdleMinSeconds ? r : AuditionRange{};
}

RepeatStep audition_repeat(bool repeating, bool pass_ended, bool loop_on) {
    if (!repeating) {
        return RepeatStep::Done;
    }
    if (!pass_ended) {
        return RepeatStep::Continue;
    }
    return loop_on ? RepeatStep::Again : RepeatStep::Done;
}

TapPlay tap_play(bool auditioning, bool double_tap) {
    if (auditioning) {
        return TapPlay::Stop;
    }
    return double_tap ? TapPlay::Leave : TapPlay::Play;
}

AuditionRange audition_section(const Section& section) {
    AuditionRange r;
    r.start_s = section.start_s;
    r.end_s = section.end_s;
    return r;
}

} // namespace etude
