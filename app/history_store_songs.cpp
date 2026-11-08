// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_store.hpp"

#include "history_store_internal.hpp"

#include "runs_file.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cctype>
#include <map>
#include <set>
#include <span>
#include <system_error>
#include <utility>
#include <variant>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSelectSources =
    "SELECT content_hash, catalogue, catalogue_id, title, composer, licence, licence_url, credit, "
    "source_url, page_url, downloaded_utc_s FROM song_sources";
constexpr std::string_view kSelectOverrides =
    "SELECT hash_a, hash_b FROM version_overrides WHERE profile_id = ?1";
constexpr std::string_view kSelectPractice =
    "SELECT content_hash, speed_percent, count_in_bars, hands, transpose_semitones, profile_id "
    "FROM song_practice ORDER BY profile_id";
constexpr std::string_view kSelectPractice8 =
    "SELECT content_hash, speed_percent, count_in_bars, hands, transpose_semitones, "
    "loop_start_ms, loop_end_ms, loop_enabled, section_id, plan_item, parts_set, profile_id "
    "FROM song_practice ORDER BY profile_id";
constexpr std::string_view kSelectPractice10 =
    "SELECT content_hash, speed_percent, count_in_bars, hands, transpose_semitones, "
    "loop_start_ms, loop_end_ms, loop_enabled, section_id, plan_item, parts_set, no_shift_offer, "
    "profile_id FROM song_practice ORDER BY profile_id";
constexpr std::string_view kSelectPractice11 =
    "SELECT content_hash, speed_percent, count_in_bars, hands, transpose_semitones, "
    "loop_start_ms, loop_end_ms, loop_enabled, section_id, plan_item, parts_set, no_shift_offer, "
    "split_point, profile_id FROM song_practice ORDER BY profile_id";
constexpr std::string_view kSelectParts =
    "SELECT content_hash, part_key, profile_id FROM song_practice_parts ORDER BY profile_id";

} // namespace

bool HistoryStore::has_events() const {
    constexpr std::int64_t kEventsVersion = 9;
    return !migrations_.empty() && migrations_.back().version >= kEventsVersion;
}

bool HistoryStore::has_versions() const {
    constexpr std::int64_t kVersionsVersion = 6;
    return !migrations_.empty() && migrations_.back().version >= kVersionsVersion;
}

bool HistoryStore::has_practice() const {
    constexpr std::int64_t kPracticeVersion = 7;
    return !migrations_.empty() && migrations_.back().version >= kPracticeVersion;
}

bool HistoryStore::has_practice_extras() const {
    constexpr std::int64_t kExtrasVersion = 8;
    return !migrations_.empty() && migrations_.back().version >= kExtrasVersion;
}

bool HistoryStore::has_practice_offer() const {
    constexpr std::int64_t kOfferVersion = 10;
    return !migrations_.empty() && migrations_.back().version >= kOfferVersion;
}

bool HistoryStore::has_practice_split() const {
    constexpr std::int64_t kSplitVersion = 11;
    return !migrations_.empty() && migrations_.back().version >= kSplitVersion;
}

HistoryStore::Attempt HistoryStore::load_practice(std::string& why) {
    if (!has_practice()) {
        return Attempt::Ready;
    }
    const bool extras = has_practice_extras();
    const bool split = has_practice_split();
    sql::Statement select;
    const bool offer = has_practice_offer();
    int rc = select.prepare(db_.get(), split    ? kSelectPractice11
                                       : offer  ? kSelectPractice10
                                       : extras ? kSelectPractice8
                                                : kSelectPractice);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    // Every profile's rows, `profile_id` the last column of the query.
    const int profile_col = split ? 13 : offer ? 12 : extras ? 11 : 5;
    std::map<std::pair<std::int64_t, std::uint64_t>, std::size_t> at;
    while ((rc = select.step()) == SQLITE_ROW) {
        const std::uint64_t hash = hash_from_column(select.column_int(0));
        if (hash == 0) {
            continue;
        }
        SongPractice p;
        p.speed_percent = static_cast<int>(select.column_int(1));
        p.count_in_bars = static_cast<int>(select.column_int(2));
        p.hands = static_cast<HandFilter>(select.column_int(3));
        p.transpose_semitones = static_cast<int>(select.column_int(4));
        if (extras) {
            p.loop_start_s = static_cast<double>(select.column_int(5)) / 1000.0;
            p.loop_end_s = static_cast<double>(select.column_int(6)) / 1000.0;
            p.loop_enabled = select.column_int(7) != 0;
            p.section_id = static_cast<int>(select.column_int(8));
            p.plan_item = static_cast<int>(select.column_int(9));
            p.parts_set = select.column_int(10) != 0;
        }
        if (offer) {
            p.no_shift_offer = select.column_int(11) != 0;
        }
        if (split) {
            p.split_point = static_cast<int>(select.column_int(12));
        }
        const std::int64_t profile = select.column_int(profile_col);
        at[{profile, hash}] = thread_practice_.size();
        thread_practice_.push_back({profile, hash, std::move(p)});
    }
    if (rc != SQLITE_DONE) {
        return fail(rc, why);
    }
    if (extras) {
        sql::Statement parts;
        rc = parts.prepare(db_.get(), kSelectParts);
        if (rc != SQLITE_OK) {
            return fail(rc, why);
        }
        while ((rc = parts.step()) == SQLITE_ROW) {
            const auto it = at.find({parts.column_int(2), hash_from_column(parts.column_int(0))});
            if (it != at.end()) {
                thread_practice_[it->second].practice.muted_parts.push_back(
                    static_cast<std::uint32_t>(parts.column_int(1)));
            }
        }
        if (rc != SQLITE_DONE) {
            return fail(rc, why);
        }
    }
    for (ProfiledPractice& entry : thread_practice_) {
        entry.practice = sanitized(entry.practice);
    }
    return Attempt::Ready;
}

// One song's row and its muted parts, in one transaction.
int HistoryStore::write_practice(const Job& job) {
    if (!has_practice() || !profile_known(job.profile_id)) {
        return SQLITE_OK; // no table, or the profile is gone: the choices are nobody's, not another's
    }
    const bool extras = has_practice_extras();
    const SongPractice& p = job.practice;
    int rc = SQLITE_OK;
    if (extras) {
        rc = begin_.step();
        begin_.reset();
        if (rc != SQLITE_DONE) {
            return rc;
        }
    }
    const std::int64_t profile = job.profile_id;
    upsert_practice_.bind(1, profile);
    upsert_practice_.bind(2, hash_to_column(job.practice_hash));
    upsert_practice_.bind(3, std::int64_t{p.speed_percent});
    upsert_practice_.bind(4, std::int64_t{p.count_in_bars});
    upsert_practice_.bind(5, std::int64_t{static_cast<int>(p.hands)});
    upsert_practice_.bind(6, std::int64_t{p.transpose_semitones});
    if (extras) {
        upsert_practice_.bind(7, static_cast<std::int64_t>(std::llround(p.loop_start_s * 1000.0)));
        upsert_practice_.bind(8, static_cast<std::int64_t>(std::llround(p.loop_end_s * 1000.0)));
        upsert_practice_.bind(9, std::int64_t{p.loop_enabled ? 1 : 0});
        upsert_practice_.bind(10, std::int64_t{p.section_id});
        upsert_practice_.bind(11, std::int64_t{p.plan_item});
        upsert_practice_.bind(12, std::int64_t{p.parts_set ? 1 : 0});
    }
    if (has_practice_offer()) {
        upsert_practice_.bind(13, std::int64_t{p.no_shift_offer ? 1 : 0});
    }
    if (has_practice_split()) {
        upsert_practice_.bind(14, std::int64_t{p.split_point});
    }
    // The row is dated by when the change was made (0: now), so a held change replayed later keeps
    // its own time and the next record of the song, made after it, is not taken for older.
    const int stamp = has_practice_split()   ? 15
                      : has_practice_offer() ? 14
                      : extras               ? 13
                                             : 7;
    upsert_practice_.bind(stamp, job.at_utc_s);
    rc = upsert_practice_.step();
    upsert_practice_.reset();
    rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
    if (!extras) {
        return rc;
    }
    if (rc == SQLITE_OK) {
        delete_parts_.bind(1, profile);
        delete_parts_.bind(2, hash_to_column(job.practice_hash));
        rc = delete_parts_.step();
        delete_parts_.reset();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
    }
    for (std::size_t i = 0; rc == SQLITE_OK && i < p.muted_parts.size(); ++i) {
        insert_part_.bind(1, profile);
        insert_part_.bind(2, hash_to_column(job.practice_hash));
        insert_part_.bind(3, std::int64_t{p.muted_parts[i]});
        rc = insert_part_.step();
        insert_part_.reset();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
    }
    if (rc == SQLITE_OK) {
        rc = commit_.step();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
        commit_.reset();
    }
    if (rc != SQLITE_OK) {
        rollback_.step();
        rollback_.reset();
    }
    return rc;
}

HistoryStore::Attempt HistoryStore::load_versions(std::string& why) {
    if (!has_versions()) {
        return Attempt::Ready;
    }
    sql::Statement select;
    int rc = select.prepare(db_.get(), kSelectOverrides);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    select.bind(1, kDefaultProfileId);
    while ((rc = select.step()) == SQLITE_ROW) {
        const std::uint64_t a = hash_from_column(select.column_int(0));
        const std::uint64_t b = hash_from_column(select.column_int(1));
        if (a != 0) {
            thread_versions_.push_back({a, b, true});
        }
    }
    if (rc != SQLITE_DONE) {
        return fail(rc, why);
    }
    return Attempt::Ready;
}

// The rows of a job, in one transaction.
int HistoryStore::write_versions(const Job& job) {
    if (!has_versions()) {
        return SQLITE_OK;
    }
    int rc = begin_.step();
    begin_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    rc = SQLITE_OK;
    for (const VersionRow& row : job.versions) {
        sql::Statement& st = row.add ? insert_override_ : delete_override_;
        st.bind(1, kDefaultProfileId);
        st.bind(2, hash_to_column(row.a));
        st.bind(3, hash_to_column(row.b));
        rc = st.step();
        st.reset();
        if (rc != SQLITE_DONE) {
            break;
        }
        rc = SQLITE_OK;
    }
    if (rc == SQLITE_OK) {
        rc = commit_.step();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
        commit_.reset();
    }
    if (rc != SQLITE_OK) {
        rollback_.step();
        rollback_.reset();
    }
    return rc;
}

bool HistoryStore::has_sources() const {
    constexpr std::int64_t kSourcesVersion = 4;
    return !migrations_.empty() && migrations_.back().version >= kSourcesVersion;
}

HistoryStore::Attempt HistoryStore::load_sources(std::string& why) {
    if (!has_sources()) {
        return Attempt::Ready;
    }
    sql::Statement select;
    int rc = select.prepare(db_.get(), kSelectSources);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    while ((rc = select.step()) == SQLITE_ROW) {
        SongOrigin s;
        s.catalogue = select.column_text(1);
        s.id = select.column_text(2);
        s.title = select.column_text(3);
        s.composer = select.column_text(4);
        s.licence = select.column_text(5);
        s.licence_url = select.column_text(6);
        s.credit = select.column_text(7);
        s.url = select.column_text(8);
        s.page = select.column_text(9);
        s.downloaded_utc_s = select.column_int(10);
        thread_sources_.emplace_back(hash_from_column(select.column_int(0)), std::move(s));
    }
    if (rc != SQLITE_DONE) {
        return fail(rc, why);
    }
    return Attempt::Ready;
}

// The sources of a job, in one transaction.
int HistoryStore::write_sources(const Job& job) {
    if (!has_sources()) {
        return SQLITE_OK;
    }
    int rc = begin_.step();
    begin_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    rc = SQLITE_OK;
    for (const auto& [hash, s] : job.sources) {
        upsert_source_.bind(1, hash_to_column(hash));
        upsert_source_.bind(2, s.catalogue);
        upsert_source_.bind(3, s.id);
        upsert_source_.bind(4, s.title);
        upsert_source_.bind(5, s.composer);
        upsert_source_.bind(6, s.licence);
        upsert_source_.bind(7, s.licence_url);
        upsert_source_.bind(8, s.credit);
        upsert_source_.bind(9, s.url);
        upsert_source_.bind(10, s.page);
        upsert_source_.bind(11, s.downloaded_utc_s);
        rc = upsert_source_.step();
        upsert_source_.reset();
        if (rc != SQLITE_DONE) {
            break;
        }
        rc = SQLITE_OK;
    }
    if (rc == SQLITE_OK) {
        rc = commit_.step();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
        commit_.reset();
    }
    if (rc != SQLITE_OK) {
        rollback_.step();
        rollback_.reset();
    }
    return rc;
}

} // namespace etude::app
