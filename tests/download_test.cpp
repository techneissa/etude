// SPDX-License-Identifier: GPL-3.0-or-later
// Downloads: HTTPS to listed hosts only, a song's file stays in its folder, a damaged or cut gzip is
// refused. A FolderFetcher stands in for the sites, so no test reaches one. Also the melody manifest's
// licence line and the launcher copy the player made.
#include "../app/desktop_entry.hpp"
#include "catalogue_service.hpp"
#include "upload_test_support.hpp"

#include "etude/gunzip_stream.hpp"
#include "etude/melody_manifest.hpp"
#include "etude/song_catalogue.hpp"
#include "etude/tar_archive.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

using Bytes = std::vector<std::uint8_t>;

// gzip -9 of "hello\n" and of "named file\n" (the name a.txt in its header), mtime 0.
const Bytes kHello{0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xcb, 0x48, 0xcd,
                   0xc9, 0xc9, 0xe7, 0x02, 0x00, 0x20, 0x30, 0x3a, 0x36, 0x06, 0x00, 0x00, 0x00};
const Bytes kNamed{0x1f, 0x8b, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x61, 0x2e,
                   0x74, 0x78, 0x74, 0x00, 0xcb, 0x4b, 0xcc, 0x4d, 0x4d, 0x51, 0x48, 0xcb,
                   0xcc, 0x49, 0xe5, 0x02, 0x00, 0xac, 0xb8, 0xbc, 0xf3, 0x0b, 0x00, 0x00, 0x00};

// What the whole-buffer and the streaming reader say about `gz`, handing the stream `piece` bytes at a time.
std::pair<GunzipError, GunzipError> both(const Bytes& gz, std::uint64_t limit, std::string* text = nullptr) {
    std::string out;
    std::size_t at = 0;
    const GunzipError streamed = gunzip_stream(
        [&](std::span<std::uint8_t> into) -> std::ptrdiff_t {
            const std::size_t n = std::min<std::size_t>({3, into.size(), gz.size() - at});
            std::copy_n(gz.begin() + static_cast<std::ptrdiff_t>(at), n, into.begin());
            at += n;
            return static_cast<std::ptrdiff_t>(n);
        },
        [&](std::span<const std::uint8_t> piece) {
            out.append(piece.begin(), piece.end());
            return true;
        },
        limit);
    const GunzipResult whole = gunzip(gz, limit);
    if (text && streamed == GunzipError::None) {
        *text = out == std::string(whole.data.begin(), whole.data.end()) ? out : "<differs>";
    }
    return {whole.error, streamed};
}

Bytes with(Bytes gz, std::size_t at, std::uint8_t value) {
    gz[at] = value;
    return gz;
}

} // namespace

TEST_CASE("gunzip: a good member is read, the size cap is on what comes out") {
    std::string text;
    CHECK(both(kHello, 6, &text) == std::pair(GunzipError::None, GunzipError::None));
    CHECK(text == "hello\n");
    CHECK(both(kNamed, 100, &text).first == GunzipError::None);
    CHECK(text == "named file\n");
    CHECK(both(kHello, 5) == std::pair(GunzipError::TooLarge, GunzipError::TooLarge));
}

TEST_CASE("gunzip: damaged or cut input is refused") {
    const Bytes cut_header(kHello.begin(), kHello.begin() + 5);
    const Bytes cut_data(kHello.begin(), kHello.end() - 9);
    const Bytes cut_trailer(kHello.begin(), kHello.end() - 3);
    Bytes two = kHello;
    two.insert(two.end(), kHello.begin(), kHello.end());
    Bytes endless_name = kNamed;
    std::fill(endless_name.begin() + 10, endless_name.begin() + 16, 'x');
    Bytes trailing = kHello;
    trailing.push_back(0);
    const struct {
        const char* what;
        Bytes gz;
    } bad[] = {
        {"empty", {}},
        {"not gzip", with(kHello, 0, 0x1e)},
        {"not deflate", with(kHello, 2, 7)},
        {"reserved flag", with(kHello, 3, 0x20)},
        {"cut in the header", cut_header},
        {"cut in the data", cut_data},
        {"cut in the trailer", cut_trailer},
        {"wrong CRC", with(kHello, kHello.size() - 8, kHello[kHello.size() - 8] ^ 0xff)},
        {"wrong size", with(kHello, kHello.size() - 4, 9)},
        {"flipped bit", with(kHello, 12, kHello[12] ^ 0x40)},
        {"endless name", endless_name},
        {"a byte after the member", trailing},
        {"a second member", two},
    };
    for (const auto& b : bad) {
        CAPTURE(b.what);
        const auto [whole, streamed] = both(b.gz, 100);
        CHECK(whole != GunzipError::None);
        CHECK(streamed != GunzipError::None);
    }
}

TEST_CASE("fetch: only https, only the catalogue's hosts, redirects stay on https") {
    const std::array<std::string_view, 1> hosts{"www.mutopiaproject.org"};
    CHECK(https_host("https://www.MutopiaProject.org/ftp/x.mid") == "www.mutopiaproject.org");
    CHECK(https_host("http://www.mutopiaproject.org/x").empty());
    CHECK(https_host("https://user@www.mutopiaproject.org/x").empty());
    CHECK(https_host("https://www.mutopiaproject.org:8443/x").empty());
    CHECK(fetch_allowed("https://www.mutopiaproject.org/ftp/x.mid", hosts));
    CHECK_FALSE(fetch_allowed("http://www.mutopiaproject.org/ftp/x.mid", hosts));
    CHECK_FALSE(fetch_allowed("https://evil.example/www.mutopiaproject.org", hosts));
    CHECK_FALSE(fetch_allowed("https://www.mutopiaproject.org.evil.example/x", hosts));
    CHECK(redirect_target("https://a.org/x/y", "/z") == "https://a.org/z");
    CHECK(redirect_target("https://a.org/x/y", "//b.org/q").empty());
    CurlFetcher curl; // refuses before it reaches out
    CHECK(curl.fetch("http://www.mutopiaproject.org/", hosts, 1000, {}).reason == "Only secure links are used");
    CHECK(curl.fetch("https://example.org/", hosts, 1000, {}).reason == "That site is not one of the catalogues");
}

TEST_CASE("catalogue: a song file comes only from under its catalogue, and is named inside its folder") {
    CatalogueEntry e;
    e.source = CatalogueId::OpenScoreLieder;
    const std::string base = "https://raw.githubusercontent.com/OpenScore/Lieder/main/scores/";
    e.url = base + "Abbott,_Jane_Bingham/_/Just_for_Today/lc6583477.mscx";
    CHECK(catalogue_file_allowed(e));
    for (const std::string& bad : {base + "../../../evil/main/x.mscx", base + "a/%2e%2e/lc1.mscx",
                                   base + "a%2fb/lc1.mscx", base + "a/lc1.mscx?x=1", base + "a\\..\\lc1.mscx",
                                   std::string("https://raw.githubusercontent.com/Evil/Lieder/main/scores/a/lc1.mscx"),
                                   std::string("http://raw.githubusercontent.com/OpenScore/Lieder/main/scores/a/lc1.mscx")}) {
        CAPTURE(bad);
        e.url = bad;
        CHECK_FALSE(catalogue_file_allowed(e));
    }
    e = CatalogueEntry{};
    e.composer = "Chopin";
    e.title = "Etudes, Opus 10: No. 1";
    e.url = "https://www.piano-midi.de/midis/chopin/chpn_op10_e01.MID?x=1";
    CHECK(catalogue_file_name(e) == "Chopin - Etudes, Opus 10_ No. 1.mid");
    e.composer.clear();
    e.title = "../a/b?";
    CHECK(catalogue_file_name(e) == "_a_b_.mid");
}

TEST_CASE("catalogue service: a download lands in the catalogue's folder and never replaces a file") {
    TempDir temp;
    const fs::path root = temp.path;
    write_file(root / "sites/www.mutopiaproject.org/cgibin/make-table.cgi",
        read_file(fs::path(ETUDE_TEST_CATALOGUE_DIR) / "mutopia-solo-last.html"));
    const std::string rondo = read_file(fs::path(ETUDE_TEST_DATA_DIR) / "satie-gymnopedie-1.mid");
    write_file(root / "sites/www.mutopiaproject.org/ftp/WanhalJ/O42/wanhal-rondo/wanhal-rondo.mid", rondo);
    write_file(root / "sites/www.mutopiaproject.org/ftp/Yaniewicz/leslanciers/leslanciers.mid", "<html>not a song</html>");
    CatalogueService::Options o;
    o.cache_dir = root / "cache";
    o.songs_dir = root / "songs";
    o.fetcher = std::make_unique<FolderFetcher>((root / "sites").string());
    o.debounce = std::chrono::milliseconds(20);
    o.now_utc_s = [] { return std::int64_t{1'760'000'000}; };
    CatalogueService s(std::move(o));
    const auto until = [&](auto done) {
        for (int i = 0; i < 3000; ++i) {
            s.poll();
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    const auto entry = [&](std::string_view title) -> std::uint32_t {
        const CatalogueSnapshot* snap = s.snapshot();
        for (std::uint32_t i = 0; snap && i < snap->entries.size(); ++i) {
            if (snap->entries[i].title == title) {
                return i;
            }
        }
        return ~0u;
    };
    s.start();
    s.set_online(true);
    REQUIRE(until([&] { return entry("Rondo") != ~0u && entry("Les Lanciers") != ~0u; }));
    const std::uint32_t song = entry("Rondo");
    s.download(song);
    REQUIRE(until([&] { return s.file(song).state == CatalogueFile::State::Kept; }));
    const fs::path kept = root / "songs/Mutopia/J. Wanhal - Rondo.mid";
    CHECK(read_file(kept) == rondo);
    REQUIRE(s.take_download());
    s.forget(catalogue_entry_key(CatalogueId::Mutopia, "192")); // deleted from the library: can come again
    REQUIRE(until([&] { return s.file(song).state == CatalogueFile::State::None; }));
    s.download(song);
    REQUIRE(until([&] { return s.file(song).state == CatalogueFile::State::Kept; }));
    const auto again = s.take_download();
    REQUIRE(again);
    CHECK(again->path == root / "songs/Mutopia/J. Wanhal - Rondo (2).mid");
    CHECK(read_file(kept) == rondo);
    // A file that is not a song fails with a reason and is not kept.
    const std::uint32_t fake = entry("Les Lanciers");
    s.download(fake);
    REQUIRE(until([&] { return s.file(fake).state == CatalogueFile::State::Failed; }));
    CHECK(s.file_reason(fake) == "The file is not a song");
}

TEST_CASE("melody manifest: a file with no licence line is refused") {
    const std::string tail = "path | one | Path one | sprout\ngoal | one | a.mid | First goal\n";
    CHECK(parse_melody_manifest("file | a.mid | Piece A | Someone | 1 | Own transcription | CC0-1.0\n" + tail).ok());
    CHECK_FALSE(parse_melody_manifest("file | a.mid | Piece A | Someone | 1 | Own transcription |\n" + tail).ok());
    CHECK_FALSE(parse_melody_manifest("file | a.mid | Piece A | Someone | 1 |\n" + tail).ok());
}

TEST_CASE("launcher: a copy the player made is never touched") {
    TempDir t;
    const fs::path root = t.path;
    write_file(root / "system/applications/etude.desktop", "[Desktop Entry]\nExec=etude\nIcon=etude\n");
    const std::string theirs = "[Desktop Entry]\nName=My etude\nExec=/home/me/etude\nIcon=mine\n";
    const fs::path mine = root / "home/applications/etude.desktop";
    write_file(mine, theirs);
    DesktopFolders folders;
    folders.data_home = root / "home";
    folders.data_dirs = {root / "system"};
    const unsigned char png[] = {0x89, 'P', 'N', 'G'};
    CHECK(apply_launcher_colour(folders, etude::IconColour::Teal, png) == DesktopResult::Unchanged);
    CHECK(apply_launcher_colour(folders, etude::IconColour::Wine, png) == DesktopResult::Unchanged);
    CHECK(read_file(mine) == theirs);
}
