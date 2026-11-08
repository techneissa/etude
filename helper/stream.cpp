// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream.hpp"

#include "interrupt.hpp"
#include "safe_io.hpp"

#include "etude/gunzip_stream.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

namespace etude::helper {

namespace {

// One read of `fd` into `into`: the count, 0 at the end, -1 on an error (`error` says which). A signal
// to the helper counts as an error.
std::ptrdiff_t read_piece(int fd, std::span<std::uint8_t> into, std::string& error) {
    for (;;) {
        if (interrupt_in_wait()) {
            error = "interrupted";
            return -1;
        }
        const ssize_t n = ::read(fd, into.data(), into.size());
        if (n >= 0) {
            return n;
        }
        if (errno != EINTR) {
            error = errno_text(errno);
            return -1;
        }
    }
}

bool write_piece(int fd, std::span<const std::uint8_t> piece, std::string& error) {
    std::size_t done = 0;
    while (done < piece.size()) {
        const ssize_t n = ::write(fd, piece.data() + done, piece.size() - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = errno_text(errno);
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

const char* gunzip_words(GunzipError error) {
    switch (error) {
    case GunzipError::TooLarge:
        return "it unpacks to more than it may";
    case GunzipError::NotGzip:
        return "it is not a gzip file";
    default:
        return "it is damaged";
    }
}

} // namespace

bool digest_file(int fd, Digest512& digest, std::string& error) {
    if (::lseek(fd, 0, SEEK_SET) != 0) {
        error = errno_text(errno);
        return false;
    }
    FileDigest hash;
    std::vector<std::uint8_t> chunk(kStreamChunk);
    for (;;) {
        const std::ptrdiff_t n = read_piece(fd, chunk, error);
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            digest = hash.finish();
            return true;
        }
        hash.update({chunk.data(), static_cast<std::size_t>(n)});
    }
}

bool gunzip_to_file(int src, int dir_fd, const std::string& name, std::uint64_t max_bytes,
                    std::string& error) {
    if (!plain_name(name)) {
        error = "not a plain file name";
        return false;
    }
    const int raw = ::openat(dir_fd, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (raw < 0) {
        error = name + ": " + errno_text(errno);
        return false;
    }
    Fd out(raw);
    if (::lseek(src, 0, SEEK_SET) != 0) {
        error = errno_text(errno);
        ::unlinkat(dir_fd, name.c_str(), 0);
        return false;
    }
    std::string io_error; // what the read or the write said, when that is what stopped it
    const GunzipError result = gunzip_stream(
        [&](std::span<std::uint8_t> into) { return read_piece(src, into, io_error); },
        [&](std::span<const std::uint8_t> piece) { return write_piece(out.get(), piece, io_error); },
        max_bytes);
    if (result != GunzipError::None) {
        error = io_error.empty() ? std::string(gunzip_words(result)) : io_error;
    }
    if (!error.empty()) {
        out.reset();
        ::unlinkat(dir_fd, name.c_str(), 0);
        return false;
    }
    return true;
}

MappedFile::~MappedFile() {
    if (at_ != nullptr) {
        ::munmap(at_, size_);
    }
}

bool MappedFile::map(int fd, std::string& error) {
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        error = errno_text(errno);
        return false;
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ == 0) {
        return true;
    }
    void* at = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    if (at == MAP_FAILED) {
        error = errno_text(errno);
        size_ = 0;
        return false;
    }
    at_ = at;
    return true;
}

} // namespace etude::helper
