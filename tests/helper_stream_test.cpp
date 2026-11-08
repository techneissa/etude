// SPDX-License-Identifier: GPL-3.0-or-later
// The root update helper's streaming of a download (helper/stream): the hash of a file read in
// pieces, the gzip unpacked into a file, that file mapped, and the memory all of it takes.
#include "../helper/safe_io.hpp"
#include "../helper/stream.hpp"
#include "upload_test_support.hpp"

#include "etude/tar_archive.hpp"

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

using namespace etude;
using namespace etude::helper;
using namespace upload_test;

namespace {

// A gzip member of stored blocks (no compressor needed) holding `size` bytes of `fill`, written to
// `path` a block at a time, so a large one never sits in memory here either.
void write_stored_gzip(const fs::path& path, std::uint64_t size, std::uint8_t fill) {
    const std::vector<std::uint8_t> block(65535, fill);
    std::uint32_t table[256];
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        table[i] = c;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    const Fd out(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600));
    REQUIRE(out.ok());
    const std::uint8_t header[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
    REQUIRE(::write(out.get(), header, sizeof header) == sizeof header);
    for (std::uint64_t done = 0; done < size;) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(block.size(), size - done));
        const std::uint8_t head[5] = {static_cast<std::uint8_t>(done + n == size ? 1 : 0),
                                      static_cast<std::uint8_t>(n & 0xff), static_cast<std::uint8_t>(n >> 8),
                                      static_cast<std::uint8_t>(~n & 0xff),
                                      static_cast<std::uint8_t>((~n >> 8) & 0xff)};
        REQUIRE(::write(out.get(), head, sizeof head) == sizeof head);
        REQUIRE(::write(out.get(), block.data(), n) == static_cast<ssize_t>(n));
        for (std::size_t i = 0; i < n; ++i) {
            crc = table[(crc ^ fill) & 0xff] ^ (crc >> 8);
        }
        done += n;
    }
    crc = ~crc;
    std::uint8_t trailer[8];
    for (int k = 0; k < 4; ++k) {
        trailer[k] = static_cast<std::uint8_t>(crc >> (8 * k));
        trailer[4 + k] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(size) >> (8 * k));
    }
    REQUIRE(::write(out.get(), trailer, sizeof trailer) == sizeof trailer);
}

Fd open_read(const fs::path& path) {
    Fd fd(::open(path.c_str(), O_RDONLY));
    REQUIRE(fd.ok());
    return fd;
}

} // namespace

TEST_CASE("stream: a file is hashed in pieces, the same as all at once") {
    TempDir t;
    std::string text;
    for (int i = 0; i < 200000; ++i) {
        text += static_cast<char>('a' + i % 23);
    }
    write_file(t.path / "f", text);
    Digest512 streamed{};
    std::string error;
    REQUIRE(digest_file(open_read(t.path / "f").get(), streamed, error));
    FileDigest whole;
    whole.update({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    CHECK(streamed == whole.finish());
    // Read again from the same descriptor, which sits at the end now: the same hash.
    Digest512 again{};
    REQUIRE(digest_file(open_read(t.path / "f").get(), again, error));
    const Fd f = open_read(t.path / "f");
    REQUIRE(digest_file(f.get(), again, error));
    REQUIRE(digest_file(f.get(), again, error));
    CHECK(again == streamed);
    // A different byte in the middle is a different hash.
    text[text.size() / 2] ^= 1;
    write_file(t.path / "g", text);
    Digest512 other{};
    REQUIRE(digest_file(open_read(t.path / "g").get(), other, error));
    CHECK(other != streamed);
    write_file(t.path / "empty", "");
    REQUIRE(digest_file(open_read(t.path / "empty").get(), other, error));
}

TEST_CASE("stream: a gzip is unpacked into a new file, and refused when it is bad or too large") {
    TempDir t;
    write_stored_gzip(t.path / "a.gz", 300000, 'q');
    const Fd dir = open_read(t.path);
    std::string error;
    REQUIRE(gunzip_to_file(open_read(t.path / "a.gz").get(), dir.get(), "a.tar", 1 << 20, error));
    CHECK(fs::file_size(t.path / "a.tar") == 300000);
    CHECK(read_file(t.path / "a.tar") == std::string(300000, 'q'));
    // Never over an existing file.
    CHECK_FALSE(gunzip_to_file(open_read(t.path / "a.gz").get(), dir.get(), "a.tar", 1 << 20, error));
    CHECK(read_file(t.path / "a.tar") == std::string(300000, 'q'));
    // Too large: refused, and no half file is left.
    CHECK_FALSE(gunzip_to_file(open_read(t.path / "a.gz").get(), dir.get(), "b.tar", 299999, error));
    CHECK(error.find("more than it may") != std::string::npos);
    CHECK_FALSE(fs::exists(t.path / "b.tar"));
    // Damaged: the same.
    std::string bad = read_file(t.path / "a.gz");
    bad[bad.size() / 2] ^= 0x10;
    write_file(t.path / "bad.gz", bad);
    CHECK_FALSE(gunzip_to_file(open_read(t.path / "bad.gz").get(), dir.get(), "c.tar", 1 << 20, error));
    CHECK_FALSE(fs::exists(t.path / "c.tar"));
    write_file(t.path / "text.gz", "this is not gzip at all");
    CHECK_FALSE(gunzip_to_file(open_read(t.path / "text.gz").get(), dir.get(), "d.tar", 1 << 20, error));
    CHECK_FALSE(fs::exists(t.path / "d.tar"));
    // A name that is not a plain one.
    CHECK_FALSE(gunzip_to_file(open_read(t.path / "a.gz").get(), dir.get(), "../x", 1 << 20, error));
}

TEST_CASE("stream: a mapped file reads like memory, and an empty one is an empty span") {
    TempDir t;
    write_file(t.path / "f", "hello mapped world");
    write_file(t.path / "empty", "");
    std::string error;
    MappedFile map;
    REQUIRE(map.map(open_read(t.path / "f").get(), error));
    CHECK(std::string(map.bytes().begin(), map.bytes().end()) == "hello mapped world");
    MappedFile none;
    REQUIRE(none.map(open_read(t.path / "empty").get(), error));
    CHECK(none.bytes().empty());
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ETUDE_TEST_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define ETUDE_TEST_ASAN 1
#endif

#if defined(__linux__) && !defined(ETUDE_TEST_ASAN)
namespace {

// Runs `job` in a child process whose writable memory may grow by `extra_bytes` and no more (Linux
// only: a sanitizer's shadow memory would break the limit). Returns the job's verdict.
bool within_memory(std::uint64_t extra_bytes, const std::function<bool()>& job) {
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        struct rusage usage {};
        ::getrusage(RUSAGE_SELF, &usage);
        std::uint64_t data_now = 0;
        if (FILE* statm = std::fopen("/proc/self/statm", "r")) {
            unsigned long pages = 0, resident = 0, shared = 0, text = 0, lib = 0, data = 0;
            if (std::fscanf(statm, "%lu %lu %lu %lu %lu %lu", &pages, &resident, &shared, &text, &lib, &data) == 6) {
                data_now = data * static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
            }
            std::fclose(statm);
        }
        rlimit limit{};
        limit.rlim_cur = limit.rlim_max = data_now + extra_bytes;
        // Writable private memory (the heap, big allocations) counts; a read-only mapping of a file
        // does not.
        if (::setrlimit(RLIMIT_DATA, &limit) != 0) {
            ::_exit(2);
        }
        bool ok = false;
        try {
            ok = job();
        } catch (...) {
            ok = false;
        }
        ::_exit(ok ? 0 : 1);
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) != 2);
    return WEXITSTATUS(status) == 0;
}

} // namespace

TEST_CASE("stream: a download far larger than the memory it may use is hashed and unpacked") {
    TempDir t;
    constexpr std::uint64_t kSize = 128ULL * 1024 * 1024; // the writable memory allowed below is 32 MiB
    write_stored_gzip(t.path / "big.gz", kSize, 0x41);
    // Everything that allocates (the file helpers, the temporary folder) is set up before the limit.
    const Fd dir = open_read(t.path);
    const Fd gz = open_read(t.path / "big.gz");
    CHECK(within_memory(32ULL * 1024 * 1024, [&] {
        std::string error;
        Digest512 digest{};
        if (!digest_file(gz.get(), digest, error)) {
            return false;
        }
        if (!gunzip_to_file(gz.get(), dir.get(), "big.tar", kSize, error)) {
            return false;
        }
        const Fd tar(::openat(dir.get(), "big.tar", O_RDONLY));
        MappedFile map;
        return tar.ok() && map.map(tar.get(), error) && map.bytes().size() == kSize &&
               map.bytes().front() == 0x41 && map.bytes().back() == 0x41;
    }));
    // The same limit stops a program that holds the file whole: the check can fail.
    CHECK_FALSE(within_memory(32ULL * 1024 * 1024, [&] {
        std::vector<std::uint8_t> all(kSize, 1);
        return all[all.size() / 2] == 1;
    }));
}
#endif
