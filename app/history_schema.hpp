// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database's schema: one list of migrations, version 1 first. The schema
// lives in history_schema.cpp and nowhere else; this header only says how to read the list.
#pragma once

#include "etude/profile.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace etude::app {

// One step from version N-1 to N: SQL statements separated by ';'. The store runs a step and sets
// PRAGMA user_version = N in one transaction, so a database is always at a whole version.
struct Migration {
    int version = 0;
    std::string_view sql;
};

// Every migration this build knows, in order; versions are 1, 2, 3, ... with no gaps. The last
// one's version is the schema version a database must have to be used.
std::span<const Migration> history_migrations();

// The profile every database starts with (core profile.hpp): the library's rows (favourites,
// groups, versions, sources) always belong to it, and so does everything from before profiles.
using etude::kDefaultProfileId;

} // namespace etude::app
