// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_journal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

JournalAppend append_journal(const fs::path& path, std::span<const JournalEntry> entries) {
    JournalAppend result;
    const std::size_t asked = entries.size();
    std::error_code ec;
    std::size_t keep = 0;               // the bytes of an existing journal to keep
    std::vector<JournalEntry> combined; // the records read before a damaged length, and `entries`
    if (fs::exists(path, ec) || fs::is_symlink(path, ec)) {
        if (!fs::is_regular_file(path, ec)) {
            result.why = "it is not a file";
            return result;
        }
        const std::string existing = read_file(path);
        JournalDecode found = decode_journal(existing);
        if (found.bad_header) {
            result.why = "the file there is not a journal";
            return result;
        }
        if (found.newer_version != 0) {
            result.why = "the file there is from a newer Etude (journal version " +
                         std::to_string(found.newer_version) + ")";
            return result;
        }
        keep = found.valid_end; // 0 for an empty file; a torn tail is cut off
        if (found.framing_damaged) {
            // Appending would put the new records behind bytes no reader gets past, and cutting
            // there would lose the ones after: the file is kept whole, aside.
            fs::path aside;
            for (int n = 0; n < 8 && aside.empty(); ++n) {
                fs::path candidate = path;
                candidate += n == 0 ? ".rejected" : ".rejected." + std::to_string(n);
                if (!fs::exists(candidate, ec) && !fs::is_symlink(candidate, ec)) {
                    aside = candidate;
                }
            }
            if (aside.empty()) {
                result.why = "the file there is damaged and every name for a copy is taken";
                return result;
            }
            fs::rename(path, aside, ec);
            if (ec) {
                result.why =
                    "the file there is damaged and cannot be moved aside (" + ec.message() + ")";
                return result;
            }
            combined = std::move(found.entries);
            combined.insert(combined.end(), entries.begin(), entries.end());
            entries = combined;
            keep = 0;
        }
    }
    // Each entry as it goes to disk; nothing for one that is over the cap.
    std::vector<std::string> framed;
    framed.reserve(entries.size());
    for (const JournalEntry& entry : entries) {
        framed.push_back(encode_journal_entry(entry));
    }
    const std::size_t old = entries.size() - asked; // records read before a damaged length
    std::vector<bool> wrote(entries.size(), false);
    const auto note = [&result](const std::string& why) {
        if (result.why.empty()) {
            result.why = why;
        }
    };
    const auto finish = [&] {
        result.kept.assign(wrote.begin() + static_cast<std::ptrdiff_t>(old), wrote.end());
        result.saved = static_cast<std::size_t>(
            std::count(result.kept.begin(), result.kept.end(), true));
    };
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        note("cannot open the file for writing (" + std::string(std::strerror(errno)) + ")");
        finish();
        return result;
    }
    int write_errno = 0;
    const auto write_all = [fd, &write_errno](const std::string& bytes) {
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                write_errno = errno;
                return false;
            }
            done += static_cast<std::size_t>(n);
        }
        return true;
    };
    bool ok = ::ftruncate(fd, static_cast<off_t>(keep)) == 0 &&
              ::lseek(fd, static_cast<off_t>(keep), SEEK_SET) == static_cast<off_t>(keep);
    if (!ok) {
        write_errno = errno;
    }
    if (ok && keep == 0) {
        ok = write_all(encode_journal_header());
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (framed[i].empty()) {
            note("a record is over the size cap");
            continue;
        }
        if (!ok) {
            break;
        }
        ok = write_all(framed[i]);
        wrote[i] = ok;
    }
    if (!ok) {
        note("cannot write the file (" + std::string(std::strerror(write_errno)) + ")");
    }
    // What was written before a failure counts once it is on disk.
    const bool synced = ::fsync(fd) == 0;
    const int sync_errno = errno;
    ::close(fd);
    if (!synced) {
        wrote.assign(wrote.size(), false);
        note("cannot sync the file (" + std::string(std::strerror(sync_errno)) + ")");
        finish();
        return result;
    }
    // The new file's name must reach the disk too.
    const int dir = ::open(path.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
    if (dir >= 0) {
        (void)::fsync(dir);
        ::close(dir);
    }
    finish();
    return result;
}

} // namespace etude::app
