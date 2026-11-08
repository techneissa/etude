// SPDX-License-Identifier: GPL-3.0-or-later
// "Get songs" off the network: a FolderFetcher stands in for the sites, so no test reaches one.
#include "catalogue_service.hpp"

#include "alloc_count.hpp"

#include "etude/content_hash.hpp"

#include <doctest/doctest.h>

#include <array>
#include <fstream>
#include <iterator>
#include <thread>

using namespace etude;
using namespace etude::app;
namespace fs = std::filesystem;

namespace {

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void put(const fs::path& p, std::string_view bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string fixture(const char* name) {
    return read_all(fs::path(ETUDE_TEST_CATALOGUE_DIR) / name);
}

// The sites as folders: Mutopia's listing (its last page alone, so one request), its Rondo, the
// Lieder listing; piano-midi.de's start page is there too, but that catalogue is off.
struct Sites {
    fs::path root;
    fs::path sites;
    fs::path cache;
    fs::path songs;
    std::string rondo;

    explicit Sites(const char* name) {
        root = fs::temp_directory_path() / name;
        fs::remove_all(root);
        sites = root / "sites";
        cache = root / "cache";
        songs = root / "songs";
        put(sites / "www.mutopiaproject.org/cgibin/make-table.cgi", fixture("mutopia-solo-last.html"));
        rondo = read_all(fs::path(ETUDE_TEST_DATA_DIR) / "satie-gymnopedie-1.mid");
        put(sites / "www.mutopiaproject.org/ftp/WanhalJ/O42/wanhal-rondo/wanhal-rondo.mid", rondo);
        put(sites / "www.mutopiaproject.org/ftp/Yaniewicz/leslanciers/leslanciers.mid",
            "<html>not a song</html>");
        put(sites / "www.piano-midi.de/midi_files.htm", fixture("piano-midi-files.htm"));
        put(sites / "raw.githubusercontent.com/OpenScore/Lieder/main/data/scores.yaml",
            fixture("lieder-scores.yaml"));
    }
    ~Sites() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    CatalogueService::Options options(std::uint64_t bytes_per_s = 0, bool fail_first = false,
                                      FolderFetcher** fetcher = nullptr) const {
        CatalogueService::Options o;
        o.cache_dir = cache;
        o.songs_dir = songs;
        auto f = std::make_unique<FolderFetcher>(sites.string(), bytes_per_s, fail_first);
        if (fetcher) {
            *fetcher = f.get();
        }
        o.fetcher = std::move(f);
        o.debounce = std::chrono::milliseconds(20);
        o.now_utc_s = [] { return std::int64_t{1'760'000'000}; };
        return o;
    }
};

// Polls until `done`. The deadline only bounds a hang; a loaded machine never reaches it.
template <typename F>
bool until(CatalogueService& s, F done) {
    for (int i = 0; i < 3000; ++i) {
        s.poll();
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::optional<std::uint32_t> entry_titled(const CatalogueService& s, std::string_view title) {
    const CatalogueSnapshot* snap = s.snapshot();
    if (!snap) {
        return std::nullopt;
    }
    for (std::uint32_t i = 0; i < snap->entries.size(); ++i) {
        if (snap->entries[i].title == title) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("fetch: only https, only the catalogue's hosts") {
    const std::array<std::string_view, 1> hosts{"www.mutopiaproject.org"};
    CHECK(https_host("https://www.MutopiaProject.org/ftp/x.mid") == "www.mutopiaproject.org");
    CHECK(https_host("https://www.mutopiaproject.org:443/x") == "www.mutopiaproject.org");
    CHECK(https_host("https://www.mutopiaproject.org:8443/x").empty());
    CHECK(https_host("http://www.mutopiaproject.org/x").empty());
    CHECK(https_host("https://user@www.mutopiaproject.org/x").empty());
    CHECK(https_host("https://").empty());
    CHECK(fetch_allowed("https://www.mutopiaproject.org/ftp/x.mid", hosts));
    CHECK_FALSE(fetch_allowed("http://www.mutopiaproject.org/ftp/x.mid", hosts));
    CHECK_FALSE(fetch_allowed("https://evil.example/www.mutopiaproject.org", hosts));
    CHECK_FALSE(fetch_allowed("https://www.mutopiaproject.org.evil.example/x", hosts));
    CHECK(redirect_target("https://a.org/x/y", "/z") == "https://a.org/z");
    CHECK(redirect_target("https://a.org/x/y", "https://b.org/q") == "https://b.org/q");
    CHECK(redirect_target("https://a.org/x/y", "//b.org/q").empty());
    CHECK(redirect_target("https://a.org/x/y", "z").empty());
    CHECK(http_status_reason(404) == "Not found on the site");
    CHECK(http_status_reason(418) == "The site refused the download");
    CHECK(fetch_user_agent().rfind("etude/", 0) == 0);
}

TEST_CASE("fetch: the curl fetcher refuses plain http and other hosts before reaching out") {
    CurlFetcher curl;
    const std::array<std::string_view, 1> hosts{"www.mutopiaproject.org"};
    FetchResult r = curl.fetch("http://www.mutopiaproject.org/", hosts, 1000, {});
    CHECK_FALSE(r.ok);
    CHECK(r.reason == "Only secure links are used");
    r = curl.fetch("https://example.org/", hosts, 1000, {});
    CHECK_FALSE(r.ok);
    CHECK(r.reason == "That site is not one of the catalogues");
}

TEST_CASE("catalogue service: offline it reaches nothing; online it fetches and caches") {
    Sites sites("etude_catalogue_online");
    FolderFetcher* fetcher = nullptr;
    {
        CatalogueService s(sites.options(0, false, &fetcher));
        s.start();
        REQUIRE(s.wait_idle(std::chrono::seconds(60)));
        s.poll();
        CHECK(fetcher->fetches() == 0);
        CHECK(s.index_state(CatalogueId::Mutopia).kind == CatalogueIndexState::Kind::None);

        s.set_online(true);
        REQUIRE(until(s, [&] {
            return s.index_state(CatalogueId::OpenScoreLieder).kind ==
                       CatalogueIndexState::Kind::Ready &&
                   s.index_state(CatalogueId::Mutopia).kind == CatalogueIndexState::Kind::Ready;
        }));
        CHECK(s.index_state(CatalogueId::Mutopia).songs == 6);
        CHECK(s.index_state(CatalogueId::OpenScoreLieder).songs == 46);
        CHECK(s.index_state(CatalogueId::Mutopia).fetched_utc_s == 1'760'000'000);
        // piano-midi.de is off: never fetched, and none of its songs are searched.
        REQUIRE(s.wait_idle(std::chrono::seconds(60)));
        s.poll();
        CHECK(s.index_state(CatalogueId::PianoMidi).kind == CatalogueIndexState::Kind::None);
        CHECK(fetcher->fetches() == 2);
        for (const CatalogueEntry& e : s.snapshot()->entries) {
            CHECK(e.source != CatalogueId::PianoMidi);
        }

        s.search("rondo");
        REQUIRE(until(s, [&] { return s.hits_query() == "rondo" && !s.searching(); }));
        REQUIRE_FALSE(s.hits().empty());
        CHECK(s.snapshot()->entries[s.hits()[0].entry].title == "Rondo");
        s.search("");
        REQUIRE(until(s, [&] { return s.hits_query().empty() && !s.searching(); }));
        CHECK(s.hits().empty());
    }
    CHECK(fs::exists(sites.cache / "indexes" / "mutopia.tsv"));
    CHECK(fs::exists(sites.cache / "indexes" / "openscore-lieder.tsv"));
    CHECK_FALSE(fs::exists(sites.cache / "indexes" / "piano-midi.tsv"));

    // A new start reads the caches and searches them offline.
    CatalogueService again(sites.options(0, false, &fetcher));
    again.start();
    again.search("gute nacht");
    REQUIRE(until(again, [&] { return again.hits_query() == "gute nacht" && !again.searching() &&
                                      !again.hits().empty(); }));
    CHECK(again.snapshot()->entries[again.hits()[0].entry].title == "Gute Nacht");
    CHECK(again.index_state(CatalogueId::OpenScoreLieder).kind == CatalogueIndexState::Kind::Ready);
    CHECK(fetcher->fetches() == 0);
}

TEST_CASE("catalogue service: a preview's file, then Download keeps it in the catalogue's folder") {
    Sites sites("etude_catalogue_download");
    CatalogueService s(sites.options());
    s.start();
    s.set_online(true);
    REQUIRE(until(s, [&] { return entry_titled(s, "Rondo").has_value(); }));
    const std::uint32_t rondo = *entry_titled(s, "Rondo");

    s.preview(rondo);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::Ready; }));
    const fs::path cached = s.file_path(rondo);
    CHECK(read_all(cached) == sites.rondo);
    CHECK_FALSE(fs::exists(sites.songs / "Mutopia"));

    s.download(rondo);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::Kept; }));
    const fs::path kept = sites.songs / "Mutopia" / "J. Wanhal - Rondo.mid";
    CHECK(fs::exists(kept));
    CHECK(read_all(kept) == sites.rondo);
    std::optional<CatalogueDownload> d = s.take_download();
    REQUIRE(d);
    CHECK(d->path == kept);
    CHECK(d->entry.title == "Rondo");
    CHECK(d->content_hash == content_hash(sites.rondo));
    CHECK_FALSE(s.take_download());
    // Downloading it again does nothing.
    s.download(rondo);
    CHECK(s.wait_idle(std::chrono::seconds(60)));
    CHECK_FALSE(s.take_download());
    // Deleted from the library, it can be downloaded again (under the next free name).
    s.forget(catalogue_entry_key(CatalogueId::Mutopia, "192"));
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::None; }));
    s.download(rondo);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::Kept; }));
    std::optional<CatalogueDownload> again = s.take_download();
    REQUIRE(again);
    CHECK(again->path == sites.songs / "Mutopia" / "J. Wanhal - Rondo (2).mid");
}

TEST_CASE("catalogue service: the previews' files are kept to the cap, the oldest used go") {
    Sites sites("etude_catalogue_cap");
    const fs::path files = sites.cache / "files" / "mutopia";
    const auto now = fs::file_time_type::clock::now();
    const std::string kilobyte(1000, 'x');
    for (int i = 0; i < 4; ++i) {
        const fs::path p = files / ("f" + std::to_string(i) + ".mid");
        put(p, kilobyte);
        fs::last_write_time(p, now - std::chrono::hours(4 - i)); // f0 the oldest
    }
    CatalogueService::Options o = sites.options();
    o.cache_cap_bytes = 2500;
    CatalogueService s(std::move(o));
    s.start();
    REQUIRE(s.wait_idle(std::chrono::seconds(60)));
    CHECK_FALSE(fs::exists(files / "f0.mid"));
    CHECK_FALSE(fs::exists(files / "f1.mid"));
    CHECK(fs::exists(files / "f2.mid"));
    CHECK(fs::exists(files / "f3.mid"));
}

TEST_CASE("catalogue service: a file that is not a song fails with a reason, and a retry works") {
    Sites sites("etude_catalogue_fail");
    CatalogueService s(sites.options(0, true));
    s.start();
    s.set_online(true);
    // The listings fail once ("No connection") and are tried again an hour later. No refresh()
    // before this wait: it would retry at once, and Failed might pass between two polls.
    REQUIRE(until(s, [&] {
        return s.index_state(CatalogueId::Mutopia).kind == CatalogueIndexState::Kind::Failed;
    }));
    CHECK(s.index_state(CatalogueId::Mutopia).reason == "No connection");
    s.refresh();
    REQUIRE(until(s, [&] { return entry_titled(s, "Rondo").has_value(); }));
    const std::uint32_t rondo = *entry_titled(s, "Rondo");
    s.download(rondo);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::Failed; }));
    CHECK(s.file_reason(rondo) == "No connection");
    s.download(rondo); // Retry
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::Kept; }));

    const std::uint32_t lanciers = *entry_titled(s, "Les Lanciers");
    s.preview(lanciers);
    REQUIRE(until(s, [&] { return s.file(lanciers).state == CatalogueFile::State::Failed &&
                                  s.file_reason(lanciers) == "No connection"; }));
    s.preview(lanciers);
    REQUIRE(until(s, [&] { return s.file(lanciers).state == CatalogueFile::State::Failed &&
                                  s.file_reason(lanciers) == "The file is not a song"; }));
}

TEST_CASE("catalogue service: a download in progress can be cancelled; offline cancels too") {
    Sites sites("etude_catalogue_cancel");
    // The fixture holds the file half in until the test lets go, so Fetching (and a progress
    // above zero) is there to be seen however the polls fall; a wait on timing could miss it.
    FolderFetcher* fetcher = nullptr;
    CatalogueService s(sites.options(0, false, &fetcher));
    fetcher->hold(true);
    s.start();
    s.set_online(true);
    REQUIRE(until(s, [&] { return entry_titled(s, "Rondo").has_value(); }));
    const std::uint32_t rondo = *entry_titled(s, "Rondo");
    s.download(rondo);
    REQUIRE(until(s, [&] {
        const CatalogueFile f = s.file(rondo);
        return f.state == CatalogueFile::State::Fetching && f.progress > 0.0f;
    }));
    CHECK(s.file(rondo).keep);
    s.cancel(rondo);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::None; }));
    CHECK_FALSE(fs::exists(sites.songs / "Mutopia"));

    s.preview(rondo);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::Fetching; }));
    s.set_online(false);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::None; }));
    // Offline, a file not fetched before cannot be.
    s.preview(rondo);
    REQUIRE(until(s, [&] { return s.file(rondo).state == CatalogueFile::State::Failed; }));
    CHECK(s.file_reason(rondo) == "Offline");
}

TEST_CASE("catalogue service: typing and polling allocate nothing once warm") {
    Sites sites("etude_catalogue_alloc");
    CatalogueService s(sites.options());
    s.start();
    s.set_online(true);
    REQUIRE(until(s, [&] { return s.index_state(CatalogueId::OpenScoreLieder).songs > 0; }));
    const char* words[] = {"s", "sc", "sch", "schu", "schub", "schube", "schuber", "schubert"};
    // Warm: the texts and hits at their largest.
    for (const char* w : words) {
        s.search(w);
        until(s, [&] { return !s.searching(); });
    }
    s.search(std::string(CatalogueService::kMaxQueryBytes, 'x'));
    until(s, [&] { return !s.searching(); });
    for (const char* w : words) {
        etude::test::start_counting_allocations();
        s.search(w);
        s.poll();
        const int n = etude::test::stop_counting_allocations();
        CHECK(n == 0);
        until(s, [&] { return !s.searching(); });
        etude::test::start_counting_allocations();
        s.poll();
        s.poll();
        CHECK(etude::test::stop_counting_allocations() == 0);
    }
}

TEST_CASE("versions: the snapshot comes with the entries that are one piece, worked out off the frame thread") {
    Sites sites("etude-catalogue-versions");
    // A cached index with two versions of one piece (a number and a composer both say so).
    std::vector<CatalogueEntry> entries(3);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        entries[i].source = CatalogueId::Mutopia;
        entries[i].id = std::to_string(i + 1);
        entries[i].composer = "J. S. Bach";
        entries[i].detail = "BWV 846";
        entries[i].licence = "Public domain";
        entries[i].url = "https://www.mutopiaproject.org/ftp/BachJS/" + std::to_string(i) + ".mid";
    }
    entries[0].title = "Prelude in C major";
    entries[1].title = "Prelude in C major (Easy)";
    entries[2].title = "Fugue in C major";
    put(sites.cache / "indexes" / "mutopia.tsv",
        write_catalogue_cache(CatalogueId::Mutopia, 1'760'000'000, entries));
    CatalogueService s(sites.options());
    s.start();
    REQUIRE(until(s, [&] {
        const CatalogueSnapshot* snap = s.snapshot();
        return snap != nullptr && snap->entries.size() >= 3;
    }));
    const CatalogueSnapshot* snap = s.snapshot();
    REQUIRE(snap->versions.group_of.size() == snap->entries.size());
    const auto prelude = entry_titled(s, "Prelude in C major");
    const auto easy = entry_titled(s, "Prelude in C major (Easy)");
    const auto fugue = entry_titled(s, "Fugue in C major");
    REQUIRE((prelude && easy && fugue));
    REQUIRE(snap->versions.group_of[*prelude] != CatalogueVersions::kNone);
    CHECK(snap->versions.group_of[*prelude] == snap->versions.group_of[*easy]);
    CHECK(snap->versions.group_of[*fugue] == CatalogueVersions::kNone);
    CHECK(snap->versions.name_of(*easy) == "Easy");
    CHECK(snap->versions.name_of(*prelude) == "Original");
    CHECK(snap->versions.pieces[snap->versions.group_of[*prelude]] == "Prelude in C major");
}
