// SPDX-License-Identifier: GPL-3.0-or-later
#include "runs_file.hpp"

#include "etude/content_hash.hpp"

namespace etude::app {

namespace {

void put_u32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}

std::uint32_t get_u32(std::string_view bytes, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(
                 static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(i)]))
             << (8 * i);
    }
    return v;
}

std::uint32_t check_of(std::string_view record) {
    return static_cast<std::uint32_t>(content_hash(record) & 0xFFFFFFFFu);
}

} // namespace

std::string encode_runs(const std::vector<std::string>& records) {
    std::string out(kRunsHeader);
    for (const std::string& r : records) {
        put_u32(out, static_cast<std::uint32_t>(r.size()));
        put_u32(out, check_of(r));
        out += r;
    }
    return out;
}

RunsDecode decode_runs(std::string_view bytes) {
    RunsDecode out;
    if (bytes.empty()) {
        return out;
    }
    if (bytes.substr(0, kRunsHeader.size()) != kRunsHeader) {
        out.bad_header = true;
        return out;
    }
    std::size_t at = kRunsHeader.size();
    while (at < bytes.size()) {
        if (bytes.size() - at < 8) {
            out.truncated = true;
            break;
        }
        const std::uint32_t length = get_u32(bytes, at);
        const std::uint32_t check = get_u32(bytes, at + 4);
        if (length == 0 || length > kMaxRunBytes) {
            // The framing itself is damaged: nothing after it can be trusted to line up.
            ++out.damaged;
            break;
        }
        if (bytes.size() - at - 8 < length) {
            out.truncated = true;
            break;
        }
        const std::string_view record = bytes.substr(at + 8, length);
        at += 8 + length;
        if (check_of(record) != check) {
            ++out.damaged;
            continue;
        }
        RunRecord run;
        switch (parse_run_record(record, run)) {
        case RunParseStatus::Ok:
            out.runs.push_back(std::move(run));
            out.kept.emplace_back(record);
            break;
        case RunParseStatus::NewerVersion:
            ++out.newer;
            out.kept.emplace_back(record);
            break;
        case RunParseStatus::Truncated:
        case RunParseStatus::BadMagic:
        case RunParseStatus::Corrupt:
            ++out.damaged;
            break;
        }
    }
    return out;
}

} // namespace etude::app
