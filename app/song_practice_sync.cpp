// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_practice_sync.hpp"

namespace etude::app {

namespace {

void copy_loop(SongPractice& to, const SongPractice& from) {
    to.loop_start_s = from.loop_start_s;
    to.loop_end_s = from.loop_end_s;
    to.loop_enabled = from.loop_enabled;
    to.section_id = from.section_id;
    to.plan_item = from.plan_item;
}

bool same_loop(const SongPractice& a, const SongPractice& b) {
    return a.loop_start_s == b.loop_start_s && a.loop_end_s == b.loop_end_s &&
           a.loop_enabled == b.loop_enabled && a.section_id == b.section_id &&
           a.plan_item == b.plan_item;
}

} // namespace

SongPracticeSync::SongPracticeSync(HistoryStore& history, SongController& songs)
    : history_(history), songs_(songs) {}

SongPractice SongPracticeSync::defaults() const {
    SongPractice d;
    d.count_in_bars = songs_.options().default_count_in_bars;
    return sanitized(d);
}

SongPractice SongPracticeSync::held(std::uint64_t hash) const {
    const auto it = history_.song_practice().find(hash);
    return it != history_.song_practice().end() ? it->second : defaults();
}

SongPractice SongPracticeSync::resolved(SongPractice p) const {
    if (!p.parts_set && songs_.has_song() && !songs_.accompaniment_parts().empty()) {
        p.parts_set = true;
        p.muted_parts = open_default_on_ ? std::vector<std::uint32_t>{}
                                          : all_keys(songs_.accompaniment_parts());
    }
    return p;
}

SongPractice SongPracticeSync::stored_form(const SongPractice& p) const {
    SongPractice out = p;
    if (out.parts_set && !held(hash_).parts_set &&
        resolved(SongPractice{}).muted_parts == out.muted_parts) {
        out.parts_set = false; // the player never chose: the global default keeps deciding
        out.muted_parts.clear();
    }
    return out;
}

const SongPractice& SongPracticeSync::current() {
    songs_.practice_into(cur_);
    if (sections_ != nullptr) {
        sections_->save_into(scratch_);
        cur_.section_id = scratch_.section_id;
        cur_.plan_item = scratch_.plan_item;
    }
    sanitize(cur_);
    return cur_;
}

void SongPracticeSync::apply(const SongPractice& p, std::int64_t now_ns) {
    songs_.apply_practice(p, now_ns);
    if (sections_ != nullptr && (p.section_id > 0 || p.plan_item > 0)) {
        scratch_ = SessionState{};
        scratch_.section_id = p.section_id;
        scratch_.plan_item = p.plan_item;
        sections_->restore(scratch_, now_ns);
    }
}

const SongPractice& SongPracticeSync::kept(const SongPractice& now) {
    if (!one_off_loop_) {
        return now;
    }
    if (!same_loop(now, at_open_)) {
        one_off_loop_ = false; // the player moved the loop: it is theirs now
        return now;
    }
    kept_ = now;
    copy_loop(kept_, known_);
    return kept_;
}

void SongPracticeSync::save(std::uint64_t hash, const SongPractice& practice) {
    history_.set_song_practice(hash, practice);
    ++saves_;
}

// The watched song is going: `now` is what it was left on. Before the history has loaded the
// change is kept to be laid over its stored row then.
void SongPracticeSync::close_with(const SongPractice& now) {
    if (late_) {
        PracticeChanges& changes = late_changes_[hash_];
        if (from_session_) {
            // The session's values stay whole; it keeps no hand split, so that one is the player's
            // only when they moved it.
            changes = {now, true, true, true, true, false, true, true, true};
            changes.split = now.split_point != at_open_.split_point;
        } else {
            changes = note_changes(changes, at_open_, now);
        }
    } else if (const SongPractice& left = kept(now); left != known_) {
        save(hash_, stored_form(left));
    }
}

void SongPracticeSync::song_closing() {
    if (hash_ == 0) {
        return;
    }
    // A song replaced without a call here is already gone from the controller: the choices last
    // seen are what it was left on.
    const bool still_loaded = songs_.has_song() && songs_.song().hash == hash_;
    close_with(still_loaded ? current() : seen_);
    hash_ = 0;
    late_ = false;
    one_off_loop_ = false;
}

void SongPracticeSync::song_opened(bool from_session, std::int64_t now_ns) {
    hash_ = songs_.has_song() ? songs_.song().hash : 0;
    if (hash_ == 0) {
        return;
    }
    from_session_ = from_session;
    adopted_ = false;
    one_off_loop_ = false;
    late_ = history_.loading();
    open_default_on_ = accompaniment_on_; // a default changed while the song is open waits for the next
    known_ = resolved(held(hash_));
    if (from_session) {
        late_changes_.erase(hash_);
        // The session keeps no hand split; the history does, and it applies once it is known.
        if (!late_) {
            songs_.set_hand_split(known_.split_point, now_ns);
        }
    } else {
        // The stored choices, or the defaults while they are not known yet (with what the player
        // already changed on this song since the history started loading).
        SongPractice start = resolved(late_ ? defaults() : held(hash_));
        if (const auto it = late_changes_.find(hash_); it != late_changes_.end()) {
            start = apply_changes(start, it->second);
        }
        if (opening_loop_ && opening_loop_->hash == hash_) {
            start.loop_start_s = opening_loop_->start_s;
            start.loop_end_s = opening_loop_->end_s;
            start.loop_enabled = true;
            start.section_id = opening_loop_->section_id;
            start.plan_item = 0;
            sanitize(start);
            one_off_loop_ = true;
        }
        opening_loop_.reset();
        apply(start, now_ns);
    }
    at_open_ = current();
    seen_ = at_open_;
    changed_ns_ = 0;
}

void SongPracticeSync::drill_opened(std::int64_t now_ns) {
    // Not watched (no hash): no stored row is applied now or when the history arrives, and nothing
    // the drill is played with is written.
    hash_ = 0;
    late_ = false;
    one_off_loop_ = false;
    opening_loop_.reset();
    apply(defaults(), now_ns);
}

void SongPracticeSync::open_with_loop(std::uint64_t hash, double start_s, double end_s,
                                      int section_id) {
    opening_loop_ = OpeningLoop{hash, start_s, end_s, section_id};
}

void SongPracticeSync::restore_session(const RestorePlan& plan, const SessionState& session,
                                       const std::function<void(bool from_session)>& install,
                                       std::int64_t now_ns) {
    if (plan.load_song) {
        songs_.apply_session(session, now_ns);
        install(plan.outcome == RestoreOutcome::Restored);
    }
    if (plan.outcome != RestoreOutcome::SongChanged) {
        songs_.apply_session(session, now_ns);
    }
}

void SongPracticeSync::session_applied() {
    if (hash_ == 0) {
        return;
    }
    from_session_ = true;
    late_changes_.erase(hash_);
    at_open_ = seen_ = current();
    changed_ns_ = 0;
}

void SongPracticeSync::adopt() {
    if (hash_ == 0) {
        return;
    }
    adopted_ = true;
    seen_ = current();
    if (!late_) {
        known_ = seen_;
    }
}

void SongPracticeSync::update(std::int64_t now_ns) {
    if (hash_ != 0 && (!songs_.has_song() || songs_.song().hash != hash_)) {
        song_closing();
    }
    if ((late_ || !late_changes_.empty()) && !history_.loading()) {
        resolve_late(now_ns);
    }
    if (hash_ == 0) {
        return;
    }
    const SongPractice& now = current();
    if (now != seen_) {
        seen_ = now;
        changed_ns_ = now_ns;
    }
    if (!late_ && now_ns - changed_ns_ >= kSaveDelayNs) {
        const SongPractice& left = kept(now);
        if (left != known_) {
            save(hash_, stored_form(left));
            known_ = left;
        }
    }
}

// The history has loaded: each stored row takes the changes the player made before that.
void SongPracticeSync::resolve_late(std::int64_t now_ns) {
    if (late_ && hash_ != 0) {
        const SongPractice stored = resolved(held(hash_));
        if (from_session_) {
            known_ = stored; // the session's values stay; any difference from the history is saved
            late_changes_.erase(hash_);
            // The session keeps no hand split: the history's applies, unless the player moved it.
            if (current().split_point == at_open_.split_point) {
                songs_.set_hand_split(stored.split_point, now_ns);
                at_open_.split_point = stored.split_point;
            }
        } else {
            const PracticeChanges changes = note_changes(late_changes_[hash_], at_open_, current());
            late_changes_.erase(hash_);
            SongPractice base = stored;
            if (one_off_loop_ && same_loop(current(), at_open_)) {
                copy_loop(base, at_open_); // the section chip's loop outlives the history's arrival
            }
            apply(apply_changes(base, changes), now_ns);
            seen_ = current();
            changed_ns_ = now_ns;
            // Options of this run are not the player's to save: they count as already stored.
            known_ = adopted_ ? seen_ : stored;
        }
    }
    late_ = false;
    for (const auto& [hash, changes] : late_changes_) {
        const SongPractice stored = held(hash);
        const SongPractice merged = apply_changes(stored, changes);
        if (merged != stored) {
            save(hash, merged);
        }
    }
    late_changes_.clear();
}

void SongPracticeSync::flush() {
    song_closing();
    if (!late_changes_.empty()) {
        if (history_.loading()) {
            history_.wait_until_settled(kQuitWait);
            history_.poll();
        }
        if (!history_.loading()) {
            resolve_late(0);
        }
    }
}

} // namespace etude::app
