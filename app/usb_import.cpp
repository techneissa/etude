// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_import.hpp"

#include "upload_service.hpp"

#include "etude/content_hash.hpp"
#include "etude/upload_rules.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <memory>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxIndexed = 50000; // the most files of the Songs folder looked at

// A descriptor that closes itself.
struct Fd {
    int fd = -1;
    Fd() = default;
    explicit Fd(int f) : fd(f) {}
    ~Fd() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept : fd(o.fd) { o.fd = -1; }
    Fd& operator=(Fd&& o) noexcept {
        std::swap(fd, o.fd);
        return *this;
    }
};

// How a pass over one file ended.
enum class Pass : std::uint8_t { Done, Gone, Failed };

// A file of the stick, opened with every step checked, so a stick that changed after the scan (a
// folder swapped for a link out of it, a FIFO put where a song was) gives nothing but a refusal:
// from the stick's top one folder at a time with openat and no link followed, without blocking (a
// FIFO does not hold open() for ever), and only a regular file on the stick's own device that is
// within the size cap.
struct StickFile {
    Fd fd;
    std::uint64_t size = 0;
    Pass status = Pass::Failed; // Done: opened; Gone: the stick is not there; Failed: refused
};

std::vector<std::string> path_parts(const std::string& relative) {
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (at <= relative.size()) {
        std::size_t end = relative.find('/', at);
        end = end == std::string::npos ? relative.size() : end;
        parts.push_back(relative.substr(at, end - at));
        at = end + 1;
    }
    return parts;
}

StickFile open_stick_file(const fs::path& stick, const std::string& relative) {
    StickFile out;
    Fd dir(::open(stick.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    struct stat top{};
    if (dir.fd < 0 || ::fstat(dir.fd, &top) != 0) {
        out.status = Pass::Gone;
        return out;
    }
    const std::vector<std::string> parts = path_parts(relative);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part.empty() || part == "." || part == "..") {
            return out;
        }
        const bool last = i + 1 == parts.size();
        const int flags = last ? (O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)
                               : (O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC);
        Fd next(::openat(dir.fd, part.c_str(), flags));
        struct stat st{};
        if (next.fd < 0 || ::fstat(next.fd, &st) != 0 ||
            static_cast<std::uint64_t>(st.st_dev) != static_cast<std::uint64_t>(top.st_dev)) {
            return out;
        }
        if (last) {
            if (!S_ISREG(st.st_mode) || static_cast<std::uint64_t>(st.st_size) > kMaxUploadBytes) {
                return out;
            }
            out.size = static_cast<std::uint64_t>(st.st_size);
            out.fd = std::move(next);
            out.status = Pass::Done;
            return out;
        }
        dir = std::move(next);
    }
    return out;
}

// Reads the stick's file `relative` chunk by chunk to the end, handing each chunk to `take` (false:
// stop, Failed). Gone when the file or the stick cannot be read any more; Failed past the size cap.
// `copying` says which pass this is, for the hook.
template <typename Take>
Pass read_file(const ImportJob& job, const std::string& relative, bool copying,
               const ImportHooks& hooks, Take&& take) {
    const StickFile in = open_stick_file(job.stick, relative);
    if (in.status != Pass::Done) {
        return in.status;
    }
    std::string buffer(std::max<std::size_t>(job.chunk, 1), '\0');
    std::uint64_t total = 0;
    for (;;) {
        if (hooks.stop && hooks.stop()) {
            return Pass::Gone;
        }
        const ssize_t n = ::read(in.fd.fd, buffer.data(), buffer.size());
        if (hooks.on_chunk) {
            hooks.on_chunk(copying);
        }
        std::error_code ec;
        if (n < 0 || !fs::is_directory(job.stick, ec)) {
            return Pass::Gone; // a read error, or the stick has been pulled
        }
        total += static_cast<std::uint64_t>(n);
        if (n == 0) {
            return Pass::Done;
        }
        if (total > kMaxUploadBytes || !take(buffer.data(), static_cast<std::size_t>(n))) {
            return Pass::Failed; // a file that grew past the cap, or one the Inbox refused
        }
    }
}

std::uint64_t hash_more(const ImportHooks& hooks, std::uint64_t hash, const char* data,
                        std::size_t n) {
    return hooks.hash ? hooks.hash(hash, data, n) : content_hash_extend(hash, data, n);
}

// What the Songs folder holds, by size: the files of a size a song on the stick has are read for
// their hash once, and every file a copy adds is noted.
class KnownSongs {
  public:
    KnownSongs(const fs::path& folder, const ImportHooks& hooks) : hooks_(hooks) {
        std::error_code ec;
        for (fs::recursive_directory_iterator
                 it(folder, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end && count_ < kMaxIndexed; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (!name.empty() && name.front() == '.') {
                if (it->is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (it->is_regular_file(ec) && !it->is_symlink(ec)) {
                const std::uint64_t size = static_cast<std::uint64_t>(it->file_size(ec));
                if (!ec) {
                    by_size_[size].unread.push_back(it->path());
                    ++count_;
                }
            }
        }
    }

    // The files of `size` whose hash is `hash`: candidates, to be compared byte for byte.
    std::vector<fs::path> matches(std::uint64_t size, std::uint64_t hash) {
        std::vector<fs::path> out;
        const auto found = by_size_.find(size);
        if (found == by_size_.end()) {
            return out;
        }
        Group& group = found->second;
        for (const fs::path& path : group.unread) {
            group.known.push_back({hash_of(path), path});
        }
        group.unread.clear();
        for (const Entry& e : group.known) {
            if (e.hash == hash) {
                out.push_back(e.path);
            }
        }
        return out;
    }

    void add(std::uint64_t size, std::uint64_t hash, const fs::path& path) {
        by_size_[size].known.push_back({hash, path});
    }

  private:
    struct Entry {
        std::uint64_t hash = 0;
        fs::path path;
    };
    struct Group {
        std::vector<fs::path> unread;
        std::vector<Entry> known;
    };

    std::uint64_t hash_of(const fs::path& path) const {
        std::uint64_t h = content_hash("", 0);
        const Fd in(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        std::string buffer(kImportChunk, '\0');
        while (in.fd >= 0) {
            const ssize_t n = ::read(in.fd, buffer.data(), buffer.size());
            if (n <= 0) {
                break;
            }
            h = hash_more(hooks_, h, buffer.data(), static_cast<std::size_t>(n));
        }
        return h;
    }

    const ImportHooks& hooks_;
    std::map<std::uint64_t, Group> by_size_;
    std::size_t count_ = 0;
};

// The size and hash of the song on the stick, read once.
struct Seen {
    Pass pass = Pass::Done;
    std::uint64_t size = 0;
    std::uint64_t hash = 0;
};

Seen hash_song(const ImportJob& job, const std::string& relative, const ImportHooks& hooks) {
    Seen seen;
    seen.hash = content_hash("", 0);
    seen.pass = read_file(job, relative, false, hooks, [&](const char* data, std::size_t n) {
        seen.hash = hash_more(hooks, seen.hash, data, n);
        seen.size += n;
        return true;
    });
    return seen;
}

// Whether the stick's file and `other` hold the same bytes (the hash is only 64 bits: a match on it
// and the size is checked here before a song is skipped). Gone when the stick went.
enum class Same : std::uint8_t { Yes, No, Gone };

Same same_bytes(const ImportJob& job, const std::string& relative, const fs::path& other,
                const ImportHooks& hooks) {
    const Fd theirs(::open(other.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (theirs.fd < 0) {
        return Same::No;
    }
    bool same = true;
    std::string buffer(std::max<std::size_t>(job.chunk, 1), '\0');
    const Pass pass = read_file(job, relative, false, hooks, [&](const char* data, std::size_t n) {
        std::size_t done = 0;
        while (done < n) {
            const ssize_t got = ::read(theirs.fd, buffer.data(), std::min(n - done, buffer.size()));
            if (got <= 0 ||
                std::memcmp(data + done, buffer.data(), static_cast<std::size_t>(got)) != 0) {
                same = false;
                return false;
            }
            done += static_cast<std::size_t>(got);
        }
        return true;
    });
    if (pass == Pass::Gone) {
        return Same::Gone;
    }
    char extra = 0;
    const bool theirs_ends = ::read(theirs.fd, &extra, 1) == 0;
    return pass == Pass::Done && same && theirs_ends ? Same::Yes : Same::No;
}

// Copies the song into the Inbox. Done when it is in (`saved` is where); Gone when the stick went;
// Failed when the Inbox refused it (a name, a type, a file that is not a song, a full disk).
Pass copy_song(const ImportJob& job, const std::string& relative, const ImportHooks& hooks,
               UploadService& service, fs::path* saved) {
    ServiceError error;
    const std::string name = fs::path(relative).filename().string();
    std::unique_ptr<UploadSink> sink = service.begin_upload(name, &error);
    if (!sink) {
        return Pass::Failed;
    }
    const Pass pass = read_file(job, relative, true, hooks, [&](const char* data, std::size_t n) {
        return sink->write(data, n);
    });
    if (pass != Pass::Done) {
        sink->abort();
        return pass;
    }
    if (!sink->finish()) {
        return Pass::Failed;
    }
    *saved = service.songs() / kInboxFolder / sink->saved_as();
    return Pass::Done;
}

struct Handled {
    Pass pass = Pass::Done;
    ui::SongMark mark = ui::SongMark::Failed;
};

// One song of the job: skipped when the Songs folder has it, else copied.
Handled handle_song(const ImportJob& job, const std::string& relative, const ImportHooks& hooks,
                    UploadService& service, KnownSongs& known) {
    Handled h;
    const Seen seen = hash_song(job, relative, hooks);
    h.pass = seen.pass;
    if (h.pass != Pass::Done) {
        return h;
    }
    for (const fs::path& other : known.matches(seen.size, seen.hash)) {
        const Same same = same_bytes(job, relative, other, hooks);
        if (same == Same::Gone) {
            h.pass = Pass::Gone;
            return h;
        }
        if (same == Same::Yes) {
            h.mark = ui::SongMark::Duplicate;
            return h;
        }
    }
    fs::path saved;
    h.pass = copy_song(job, relative, hooks, service, &saved);
    if (h.pass == Pass::Done) {
        h.mark = ui::SongMark::Copied;
        known.add(seen.size, seen.hash, saved);
    }
    return h;
}

} // namespace

ImportResult import_songs(const ImportJob& job, const ImportHooks& hooks) {
    ImportResult result;
    result.marks.assign(job.songs.size(), ui::SongMark::None);
    UploadService service(job.songs_folder, fs::path());
    KnownSongs known(job.songs_folder, hooks);
    for (std::size_t i = 0; i < job.songs.size(); ++i) {
        if (hooks.on_song) {
            hooks.on_song(i, ui::SongMark::None);
        }
        const Handled h = handle_song(job, job.songs[i], hooks, service, known);
        if (h.pass == Pass::Gone) {
            std::error_code ec;
            const bool cancelled = hooks.stop && hooks.stop() && fs::is_directory(job.stick, ec);
            result.end = cancelled ? ui::ImportEnd::None : ui::ImportEnd::Removed;
            return result;
        }
        result.marks[i] = h.mark;
        result.copied += h.mark == ui::SongMark::Copied ? 1 : 0;
        result.duplicates += h.mark == ui::SongMark::Duplicate ? 1 : 0;
        result.failed += h.mark == ui::SongMark::Failed ? 1 : 0;
        if (hooks.on_song) {
            hooks.on_song(i, h.mark);
        }
    }
    return result;
}

} // namespace etude::app
