// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/qr_code.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace etude::app;

namespace {

// A QR code's three finder patterns: a dark 7 by 7 ring, a light ring inside it, a dark 3 by 3
// core.
bool finder_at(const QrGrid& g, int x0, int y0) {
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const bool ring = x == 0 || y == 0 || x == 6 || y == 6;
            const bool core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
            if (g.at(x0 + x, y0 + y) != (ring || core)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("qr code: an address of the upload page fits the smallest versions") {
    const QrGrid g = make_qr("http://192.168.1.20:8080/");
    REQUIRE_FALSE(g.empty());
    CHECK(g.size == 25); // version 2
    CHECK(finder_at(g, 0, 0));
    CHECK(finder_at(g, g.size - 7, 0));
    CHECK(finder_at(g, 0, g.size - 7));
    CHECK(g.dark.size() == 25u * 25u);
    // The same text gives the same code.
    const QrGrid again = make_qr("http://192.168.1.20:8080/");
    CHECK(again.dark == g.dark);
    CHECK(make_qr("http://etude.local:8080/").size >= 25);
}

TEST_CASE("qr code: outside the grid is light, and no text or too much text is no code") {
    const QrGrid g = make_qr("http://10.0.0.2:8080/");
    CHECK_FALSE(g.at(-1, 0));
    CHECK_FALSE(g.at(0, -1));
    CHECK_FALSE(g.at(g.size, 0));
    CHECK_FALSE(g.at(0, g.size));
    CHECK(make_qr("").empty());
    CHECK(make_qr(std::string(4000, 'x')).empty());
    CHECK_FALSE(make_qr(std::string(1000, 'x')).empty());
}

namespace {

// "http://192.168.1.20:8080/" as the encoder makes it. Checked once with an independent decoder
// (macOS's CIDetector read this very grid, drawn with a quiet zone, back to the address), so a
// change in the code that moves a module fails here and must be checked with a phone's camera.
constexpr const char* kKnown[25] = {
    "#######....#..#...#######", "#.....#...##.#.#..#.....#", "#.###.#.##.#....#.#.###.#",
    "#.###.#.####.#....#.###.#", "#.###.#.#.##..#...#.###.#", "#.....#.##.#...##.#.....#",
    "#######.#.#.#.#.#.#######", "........##.##..#.........", "#.#####..##.##.##.#####..",
    "####.#.#...#.##..#.....#.", "#.#.#.#.....##.#..##.#.##", ".#..##...#.##...#...#...#",
    "###########.##....###.###", "###..#.#.#...##......#.#.", "#.....#....##..#.#.#.#.##",
    "#.##.#....##...#.....#..#", "#..####..#####..#####.#..", "........###.#.#.#...###..",
    "#######..#......#.#.#####", "#.....#.#.##...##...##.##", "#.###.#.#.####.########..",
    "#.###.#.#.#######.###.###", "#.###.#.##..#....#....#.#", "#.....#..##.#...#.####..#",
    "#######.#.#..#.#.########"};

// The 15 format bits (the error correction level and the mask, under a BCH code) as the first copy
// holds them, round the top left finder pattern, and as the second copy, split between the other
// two.
int format_first(const QrGrid& g) {
    int bits = 0;
    for (int i = 0; i <= 5; ++i) {
        bits |= (g.at(8, i) ? 1 : 0) << i;
    }
    bits |= (g.at(8, 7) ? 1 : 0) << 6;
    bits |= (g.at(8, 8) ? 1 : 0) << 7;
    bits |= (g.at(7, 8) ? 1 : 0) << 8;
    for (int i = 9; i < 15; ++i) {
        bits |= (g.at(14 - i, 8) ? 1 : 0) << i;
    }
    return bits;
}

int format_second(const QrGrid& g) {
    int bits = 0;
    for (int i = 0; i < 8; ++i) {
        bits |= (g.at(g.size - 1 - i, 8) ? 1 : 0) << i;
    }
    for (int i = 8; i < 15; ++i) {
        bits |= (g.at(8, g.size - 15 + i) ? 1 : 0) << i;
    }
    return bits;
}

// True when `bits` is a codeword of the format code (5 data bits, 10 of BCH remainder, xor 0x5412).
bool valid_format(int bits) {
    const int word = bits ^ 0x5412;
    const int data = word >> 10;
    int rem = data;
    for (int i = 0; i < 10; ++i) {
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    }
    return ((data << 10) | (rem & 0x3FF)) == word;
}

} // namespace

TEST_CASE("qr code: the grid is the one a decoder was checked against") {
    const QrGrid g = make_qr("http://192.168.1.20:8080/");
    REQUIRE(g.size == 25);
    for (int y = 0; y < 25; ++y) {
        for (int x = 0; x < 25; ++x) {
            INFO("module " << x << "," << y);
            CHECK(g.at(x, y) == (kKnown[y][x] == '#'));
        }
    }
}

TEST_CASE("qr code: the parts a reader looks for are in place") {
    for (const char* text :
         {"http://192.168.1.20:8080/", "http://10.0.0.2:8080/", "http://etude.local:8080/"}) {
        INFO(text);
        const QrGrid g = make_qr(text);
        REQUIRE_FALSE(g.empty());
        // The timing patterns: dark and light in turn between the finders.
        for (int i = 8; i < g.size - 8; ++i) {
            CHECK(g.at(i, 6) == (i % 2 == 0));
            CHECK(g.at(6, i) == (i % 2 == 0));
        }
        CHECK(g.at(8, g.size - 8)); // the one module that is always dark
        // The format information: a valid code word, twice alike, at error correction level M.
        const int first = format_first(g);
        CHECK(valid_format(first));
        CHECK(first == format_second(g));
        CHECK(((first ^ 0x5412) >> 13) == 0); // level M is bits 00
    }
}
