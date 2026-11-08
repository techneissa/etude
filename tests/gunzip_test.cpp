// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/tar_archive.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace etude;

namespace {

// gzip -9 of "hello\n", "" and "named file\n" (the last with the name a.txt in its header), made
// with mtime 0.
const std::vector<std::uint8_t> kHello{0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
                                       0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0xe7, 0x02, 0x00, 0x20, 0x30,
                                       0x3a, 0x36, 0x06, 0x00, 0x00, 0x00};
const std::vector<std::uint8_t> kEmpty{0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
                                       0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const std::vector<std::uint8_t> kNamed{0x1f, 0x8b, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
                                       0x61, 0x2e, 0x74, 0x78, 0x74, 0x00, 0xcb, 0x4b, 0xcc, 0x4d,
                                       0x4d, 0x51, 0x48, 0xcb, 0xcc, 0x49, 0xe5, 0x02, 0x00, 0xac,
                                       0xb8, 0xbc, 0xf3, 0x0b, 0x00, 0x00, 0x00};

std::string text(const GunzipResult& r) { return std::string(r.data.begin(), r.data.end()); }

} // namespace

TEST_CASE("gunzip: a member is read, with or without a name in its header, and an empty one") {
    CHECK(text(gunzip(kHello, 100)) == "hello\n");
    CHECK(text(gunzip(kNamed, 100)) == "named file\n");
    const GunzipResult empty = gunzip(kEmpty, 100);
    CHECK(empty.ok());
    CHECK(empty.data.empty());
}

TEST_CASE("gunzip: the size limit is on what comes out") {
    CHECK(gunzip(kHello, 6).ok());
    CHECK(gunzip(kHello, 5).error == GunzipError::TooLarge);
    // A trailer that claims more than the limit is refused before anything is allocated.
    std::vector<std::uint8_t> big = kHello;
    big[big.size() - 1] = 0x7f;
    CHECK(gunzip(big, 1000).error == GunzipError::TooLarge);
}

TEST_CASE("gunzip: damaged input is refused") {
    CHECK(gunzip({}, 100).error == GunzipError::Truncated);
    std::vector<std::uint8_t> bad = kHello;
    bad[0] = 0x1e;
    CHECK(gunzip(bad, 100).error == GunzipError::NotGzip);
    bad = kHello;
    bad[2] = 7; // a method other than deflate
    CHECK(gunzip(bad, 100).error == GunzipError::NotGzip);
    bad = kHello;
    bad[3] = 0x20; // a reserved flag
    CHECK(gunzip(bad, 100).error == GunzipError::NotGzip);
    // Cut off in the header, in the data and in the trailer.
    CHECK(gunzip(std::vector<std::uint8_t>(kHello.begin(), kHello.begin() + 5), 100).error ==
          GunzipError::Truncated);
    CHECK_FALSE(gunzip(std::vector<std::uint8_t>(kHello.begin(), kHello.end() - 9), 100).ok());
    CHECK_FALSE(gunzip(std::vector<std::uint8_t>(kHello.begin(), kHello.end() - 3), 100).ok());
    // A wrong CRC, a wrong size, a flipped bit in the data.
    bad = kHello;
    bad[bad.size() - 8] ^= 0xff;
    CHECK(gunzip(bad, 100).error == GunzipError::Corrupt);
    bad = kHello;
    bad[bad.size() - 4] = 9; // says 9 bytes, holds 6
    CHECK(gunzip(bad, 100).error == GunzipError::Corrupt);
    bad = kHello;
    bad[bad.size() - 4] = 3; // says 3 bytes, holds 6
    CHECK(gunzip(bad, 100).error == GunzipError::Corrupt);
    bad = kHello;
    bad[12] ^= 0x40;
    CHECK_FALSE(gunzip(bad, 100).ok());
    // A header whose name never ends.
    bad = kNamed;
    for (std::size_t i = 10; i < 16; ++i) {
        bad[i] = 'x';
    }
    CHECK_FALSE(gunzip(bad, 100).ok());
}
