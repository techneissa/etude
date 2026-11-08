// SPDX-License-Identifier: GPL-3.0-or-later
// The old practice history file, runs.dat, which history.db (history_store.hpp) replaced. It is
// only read now, once, when the first start with the database moves its runs in: one file of run
// records, each one the bytes serialize_run_record() makes, framed so a damaged record is skipped
// and the rest read.
//
// Layout of runs.dat:
//   "etude runs 1\n"                      the header (15 bytes)
//   per record:
//     length    4 bytes, little-endian      the record's size, 1 .. kMaxRunBytes
//     check     4 bytes, little-endian      the low 32 bits of content_hash() of the record
//     record    `length` bytes              serialize_run_record()
//
// Reading never fails: an empty input is an empty history; a record whose check or contents are
// wrong is skipped and counted; input that ends inside a record (cut off) keeps everything before
// it; a wrong header means it is not a runs file at all. Records a newer build wrote are counted
// and kept byte for byte in `kept`.
#pragma once

#include "etude/run_record.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app {

inline constexpr std::string_view kRunsFileName = "runs.dat";
inline constexpr std::string_view kRunsHeader = "etude runs 1\n";
inline constexpr std::size_t kMaxRunBytes = 1u << 20;

struct RunsDecode {
    std::vector<RunRecord> runs;   // oldest first
    std::vector<std::string> kept; // every record to write back (good and newer), raw, in order
    int damaged = 0;               // records skipped: bad check, bad contents
    int newer = 0;                 // records a newer build wrote: kept, not read
    bool truncated = false;        // the file ends inside a record
    bool bad_header = false;       // not a runs file at all
};

// The file's bytes for these raw records.
std::string encode_runs(const std::vector<std::string>& records);
RunsDecode decode_runs(std::string_view bytes);

} // namespace etude::app
