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

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSelectPrefs =
    "SELECT content_hash, title, composer, genre, favourite, added_utc_s, chosen_utc_s "
    "FROM song_settings "
    "WHERE profile_id = ?1";
constexpr std::string_view kSelectGroups =
    "SELECT group_id, name, kind, source, position, created_utc_s FROM song_groups "
    "WHERE profile_id = ?1";
constexpr std::string_view kSelectGroupSongs =
    "SELECT group_id, content_hash, added_utc_s FROM song_group_members WHERE profile_id = ?1";

} // namespace

HistoryStore::Attempt HistoryStore::load_prefs(std::string& why) {
    sql::Statement select;
    int rc = select.prepare(db_.get(), kSelectPrefs);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    select.bind(1, kDefaultProfileId);
    while ((rc = select.step()) == SQLITE_ROW) {
        SongPrefs p;
        p.title = select.column_text(1);
        p.composer = select.column_text(2);
        p.genre = select.column_text(3);
        p.favourite = select.column_int(4) != 0;
        p.added_utc_s = select.column_int(5);
        p.chosen_utc_s = select.column_int(6);
        thread_prefs_.emplace_back(hash_from_column(select.column_int(0)), sanitized(p));
    }
    if (rc != SQLITE_DONE) {
        return fail(rc, why);
    }
    return Attempt::Ready;
}

HistoryStore::Attempt HistoryStore::load_groups(std::string& why) {
    if (!has_groups()) {
        return Attempt::Ready; // a schema from before the groups (tests of the migrations)
    }
    sql::Statement select;
    int rc = select.prepare(db_.get(), kSelectGroups);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    select.bind(1, kDefaultProfileId);
    while ((rc = select.step()) == SQLITE_ROW) {
        SongGroup g;
        g.id = hash_from_column(select.column_int(0));
        g.name = clean_group_name(select.column_text(1));
        g.kind = select.column_int(2) == 1 ? GroupKind::Catalogue : GroupKind::User;
        g.source = select.column_text(3);
        g.position = static_cast<std::int32_t>(
            std::clamp<std::int64_t>(select.column_int(4), INT32_MIN, INT32_MAX));
        g.created_utc_s = select.column_int(5);
        if (g.id != 0) {
            thread_groups_.push_back(std::move(g));
        }
    }
    if (rc != SQLITE_DONE) {
        return fail(rc, why);
    }
    sql::Statement members;
    rc = members.prepare(db_.get(), kSelectGroupSongs);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    members.bind(1, kDefaultProfileId);
    while ((rc = members.step()) == SQLITE_ROW) {
        thread_members_.push_back({hash_from_column(members.column_int(0)),
                                   hash_from_column(members.column_int(1)),
                                   members.column_int(2)});
    }
    if (rc != SQLITE_DONE) {
        return fail(rc, why);
    }
    return Attempt::Ready;
}

// The song settings of a job, all in one transaction.
int HistoryStore::write_prefs(const Job& job) {
    int rc = begin_.step();
    begin_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    rc = SQLITE_OK;
    for (const auto& [hash, p] : job.prefs) {
        upsert_prefs_.bind(1, kDefaultProfileId);
        upsert_prefs_.bind(2, hash_to_column(hash));
        upsert_prefs_.bind(3, p.title);
        upsert_prefs_.bind(4, p.composer);
        upsert_prefs_.bind(5, p.genre);
        upsert_prefs_.bind(6, std::int64_t{p.favourite ? 1 : 0});
        upsert_prefs_.bind(7, p.added_utc_s);
        upsert_prefs_.bind(8, p.chosen_utc_s);
        rc = upsert_prefs_.step();
        upsert_prefs_.reset();
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

// The group edits of a job, in one transaction, each as the one row or two it touches.
int HistoryStore::write_groups(const Job& job) {
    if (!has_groups()) {
        return SQLITE_OK;
    }
    int rc = begin_.step();
    begin_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    const auto run = [&](sql::Statement& st) {
        const int r = st.step();
        st.reset();
        return r == SQLITE_DONE ? SQLITE_OK : r;
    };
    rc = SQLITE_OK;
    for (const GroupEdit& e : job.groups) {
        const std::int64_t id = hash_to_column(e.group.id);
        switch (e.kind) {
        case GroupEdit::Kind::Create:
            insert_group_.bind(1, kDefaultProfileId);
            insert_group_.bind(2, id);
            insert_group_.bind(3, clean_group_name(e.group.name));
            insert_group_.bind(4, std::int64_t{e.group.kind == GroupKind::Catalogue ? 1 : 0});
            insert_group_.bind(5, e.group.source);
            insert_group_.bind(6, std::int64_t{e.group.position});
            insert_group_.bind(7, e.group.created_utc_s);
            rc = run(insert_group_);
            break;
        case GroupEdit::Kind::Rename:
            rename_group_.bind(1, kDefaultProfileId);
            rename_group_.bind(2, id);
            rename_group_.bind(3, clean_group_name(e.group.name));
            rc = run(rename_group_);
            break;
        case GroupEdit::Kind::Remove:
            delete_group_songs_.bind(1, kDefaultProfileId);
            delete_group_songs_.bind(2, id);
            rc = run(delete_group_songs_);
            if (rc == SQLITE_OK) {
                delete_group_.bind(1, kDefaultProfileId);
                delete_group_.bind(2, id);
                rc = run(delete_group_);
            }
            break;
        case GroupEdit::Kind::Add:
            add_group_song_.bind(1, kDefaultProfileId);
            add_group_song_.bind(2, id);
            add_group_song_.bind(3, hash_to_column(e.hash));
            add_group_song_.bind(4, e.at_utc_s);
            rc = run(add_group_song_);
            break;
        case GroupEdit::Kind::Drop:
            drop_group_song_.bind(1, kDefaultProfileId);
            drop_group_song_.bind(2, id);
            drop_group_song_.bind(3, hash_to_column(e.hash));
            rc = run(drop_group_song_);
            break;
        case GroupEdit::Kind::Move:
            move_group_.bind(1, kDefaultProfileId);
            move_group_.bind(2, id);
            move_group_.bind(3, std::int64_t{e.group.position});
            rc = run(move_group_);
            break;
        }
        if (rc != SQLITE_OK) {
            break;
        }
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
