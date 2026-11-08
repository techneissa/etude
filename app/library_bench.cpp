// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_bench.hpp"

#include "alloc_probe.hpp"
#include "catalogue_service.hpp"
#include "song_library.hpp"

#include "etude/hand_split.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include <unistd.h>

namespace etude::app {

namespace {

constexpr std::string_view kComposers[] = {"Ludwig van Beethoven",
                                           "Johann Sebastian Bach",
                                           "Wolfgang Amadeus Mozart",
                                           "Frédéric Chopin",
                                           "Erik Satie",
                                           "Claude Debussy",
                                           "Franz Schubert",
                                           "Robert Schumann",
                                           "Felix Mendelssohn",
                                           "Johannes Brahms",
                                           "Pyotr Ilyich Tchaikovsky",
                                           "Edvard Grieg",
                                           "Scott Joplin",
                                           "Muzio Clementi",
                                           "Carl Czerny",
                                           "Béla Bartók",
                                           "Maurice Ravel",
                                           "Sergei Rachmaninoff",
                                           "Franz Liszt",
                                           "Joseph Haydn",
                                           "",
                                           "Traditional",
                                           "George Gershwin",
                                           "Yann Tiersen"};
constexpr std::string_view kForms[] = {"Sonata",     "Prelude",   "Nocturne", "Waltz",
                                       "Étude",      "Mazurka",   "Minuet",   "Invention",
                                       "Gymnopédie", "Rag",       "Ballade",  "Impromptu",
                                       "Bagatelle",  "Arabesque", "Lullaby",  "Song Without Words",
                                       "Polonaise",  "Scherzo",   "Fugue",    "Toccata",
                                       "Rondo"};
constexpr std::string_view kNames[] = {"Moonlight",     "Für Elise",
                                       "Clair de Lune", "The Entertainer",
                                       "Greensleeves",  "Spring",
                                       "Raindrop",      "Minute",
                                       "Pathétique",    "Turkish March",
                                       "Träumerei",     "Reverie",
                                       "Autumn",        "Comptine d'un autre été",
                                       "Maple Leaf",    "Swan Lake",
                                       "Morning"};
constexpr std::string_view kKeys[] = {"C major", "A minor",      "G major", "E minor",
                                      "D major", "B-flat major", "F minor", "C-sharp minor"};
constexpr std::string_view kFolders[] = {"Classical",      "Classical/Baroque", "Romantic",
                                         "Folk",           "Ragtime",           "Film",
                                         "Lessons/Book 1", "Lessons/Book 2",    ""};

// The same sequence every run.
struct Lcg {
    std::uint64_t s;
    std::size_t next(std::size_t n) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::size_t>((s >> 33) % n);
    }
};

std::string make_title(Lcg& r, std::size_t i) {
    std::string t;
    switch (r.next(4)) {
    case 0:
        t = std::string(kForms[r.next(std::size(kForms))]) + " in " +
            std::string(kKeys[r.next(std::size(kKeys))]) + ", Op. " +
            std::to_string(1 + r.next(120)) + " No. " + std::to_string(1 + r.next(6));
        break;
    case 1:
        t = std::string(kNames[r.next(std::size(kNames))]) + " " +
            std::string(kForms[r.next(std::size(kForms))]);
        break;
    case 2:
        t = std::string(kForms[r.next(std::size(kForms))]) + " No. " +
            std::to_string(1 + r.next(24));
        break;
    default:
        t = std::string(kNames[r.next(std::size(kNames))]) + " (" + std::to_string(i) + ")";
        break;
    }
    return t;
}

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

} // namespace

BenchLibrary::BenchLibrary(std::size_t songs) {
    Lcg r{0x5eedULL};
    library_.items.reserve(songs);
    for (std::size_t i = 0; i < songs; ++i) {
        LibraryItem item;
        const std::string title = make_title(r, i);
        const std::string_view folder = kFolders[r.next(std::size(kFolders))];
        std::string file = title;
        std::replace(file.begin(), file.end(), ' ', '_');
        item.relative = folder.empty() ? file + ".mid" : std::string(folder) + "/" + file + ".mid";
        item.stamp = i + 1;
        item.state = EntryState::Loaded;
        item.entry.hash = hash_of(i);
        item.entry.title = title;
        item.entry.composer = std::string(kComposers[r.next(std::size(kComposers))]);
        item.entry.half_stars =
            kMinHalfStars + static_cast<int>(r.next(kMaxHalfStars - kMinHalfStars + 1));
        item.entry.difficulty = level_of_half_stars(item.entry.half_stars);
        item.entry.duration_s = 40.0 + static_cast<double>(r.next(420));
        item.entry.left_notes = r.next(4) == 0 ? 0 : 100;
        item.entry.right_notes = 200;
        if (r.next(9) == 0) {
            SongPrefs p;
            p.favourite = true;
            prefs_[item.entry.hash] = p;
        }
        library_.items.push_back(std::move(item));
    }
    // A few groups, as a player who keeps a lesson book, warm-ups and a recital list would have.
    constexpr std::string_view kGroupNames[kGroups] = {"Warm-ups", "Recital",  "Book 1",
                                                       "Sight reading", "Pieces to learn",
                                                       "Christmas"};
    for (int g = 0; g < kGroups; ++g) {
        GroupEdit e;
        e.group.id = static_cast<std::uint64_t>(g + 1);
        e.group.name = std::string(kGroupNames[g]);
        e.group.position = g;
        groups_.apply(e);
    }
    for (std::size_t i = 0; i < songs; ++i) {
        if (r.next(5) == 0) {
            GroupEdit e;
            e.kind = GroupEdit::Kind::Add;
            e.group.id = 1 + r.next(kGroups);
            e.hash = library_.items[i].entry.hash;
            groups_.apply(e);
        }
    }
    catalogue_.set_groups(groups_);
    // Versions of one piece: songs in runs of two to four, each run a piece, as a library of
    // arrangements has (about one song in three is one of them).
    {
        constexpr std::string_view kVersionNames[] = {"Easy", "Medium", "Hard", "Expert"};
        auto index = std::make_shared<VersionIndex>();
        for (std::size_t i = 0; i + 4 < songs; i += 9) {
            const std::size_t size = 2 + r.next(3);
            auto& group = index->groups.emplace_back();
            for (std::size_t k = 0; k < size; ++k) {
                const std::uint64_t hash = library_.items[i + k].entry.hash;
                group.push_back({hash, std::string(kVersionNames[k]), false});
                index->where[hash] = {static_cast<std::uint32_t>(index->groups.size() - 1),
                                      static_cast<std::uint32_t>(k)};
            }
        }
        catalogue_.set_versions(std::move(index));
    }
    catalogue_.library_changed();
    catalogue_.history_changed();
    catalogue_.prefs_changed();
    catalogue_.sync(library_, {}, prefs_);
    catalogue_.update_view(kToday, 0.0);
}

namespace {

// "Get songs" with a made-up catalogue of `entries` songs (cached, so nothing is fetched): each
// query typed a character at a time and deleted again. Times the frame thread's part of a
// keystroke (handing the text over and taking the hits) and counts its allocations; times the
// search on the catalogue's own thread and how long the hits take to come back (no debounce here,
// so every keystroke is searched). True when it is within its budget.
bool catalogue_bench(std::size_t entries, std::span<const std::string_view> queries) {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() / ("etude-bench-catalogue-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "indexes", ec);
    constexpr std::int64_t kNow = 1'760'000'000;
    Lcg r{11};
    std::array<std::vector<CatalogueEntry>, kCatalogueCount> lists;
    std::vector<CatalogueId> on;
    for (const CatalogueInfo& info : catalogue_infos()) {
        if (info.enabled) {
            on.push_back(info.id);
        }
    }
    for (std::size_t i = 0; i < entries; ++i) {
        CatalogueEntry e;
        e.source = on[i % on.size()];
        e.id = std::to_string(i);
        e.title = make_title(r, i);
        e.composer = std::string(kComposers[r.next(std::size(kComposers))]);
        e.detail = std::string(kFolders[r.next(std::size(kFolders))]);
        e.licence = "Public domain";
        e.url = "https://www.mutopiaproject.org/ftp/bench/" + e.id + ".mid";
        lists[static_cast<std::size_t>(e.source)].push_back(std::move(e));
    }
    for (const CatalogueInfo& info : catalogue_infos()) {
        std::ofstream out(dir / "indexes" / (std::string(info.key) + ".tsv"), std::ios::binary);
        out << write_catalogue_cache(info.id, kNow, lists[static_cast<std::size_t>(info.id)]);
    }
    CatalogueService::Options o;
    o.cache_dir = dir;
    o.songs_dir = dir / "songs";
    o.fetcher = std::make_unique<FolderFetcher>((dir / "no-sites").string());
    o.debounce = std::chrono::milliseconds(0);
    o.now_utc_s = [] { return kNow; };
    CatalogueService service(std::move(o));
    service.start();
    const auto load_t0 = Clock::now();
    service.search("a"); // the hits arrive with the first snapshot
    while ((service.snapshot() == nullptr || service.snapshot()->entries.size() < entries) &&
           ms_since(load_t0) < 10'000.0) {
        service.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const double load_ms = ms_since(load_t0);
    std::vector<double> frame_ms;
    std::vector<double> search_ms;
    std::vector<double> back_ms;
    std::uint64_t allocs = 0;
    std::size_t keys = 0;
    std::string text;
    text.reserve(CatalogueService::kMaxQueryBytes);
    for (int pass = 0; pass < 2; ++pass) {
        const bool timed = pass == 1;
        for (const std::string_view q : queries) {
            const auto key = [&](std::size_t n) {
                text.assign(q.substr(0, n));
                const std::uint64_t before = allocations_counted();
                count_allocations(timed);
                const auto t0 = Clock::now();
                service.search(text);
                service.poll();
                const double f = ms_since(t0);
                count_allocations(false);
                // The frames that follow, until the hits for this text are in.
                while (service.hits_query() != text && ms_since(t0) < 2000.0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    count_allocations(timed);
                    service.poll();
                    count_allocations(false);
                }
                if (timed) {
                    allocs += allocations_counted() - before;
                    frame_ms.push_back(f);
                    back_ms.push_back(ms_since(t0));
                    search_ms.push_back(static_cast<double>(service.last_search_ns()) / 1e6);
                    ++keys;
                }
            };
            for (std::size_t n = 1; n <= q.size(); ++n) {
                key(n);
            }
            for (std::size_t n = q.size(); n-- > 0;) {
                key(n);
            }
        }
    }
    fs::remove_all(dir, ec);
    const auto summary = [](std::vector<double>& v, const char* what) {
        std::sort(v.begin(), v.end());
        const auto at = [&](double share) {
            return v[std::min(v.size() - 1, static_cast<std::size_t>(share * static_cast<double>(v.size())))];
        };
        std::printf("  %s: min %.3f ms, median %.3f ms, p95 %.3f ms, max %.3f ms\n", what, v.front(),
                    at(0.5), at(0.95), v.back());
        return at(0.95);
    };
    std::printf("catalogue search bench: %zu catalogue songs (cache read in %.0f ms), %zu "
                "keystrokes\n",
                service.snapshot() ? service.snapshot()->entries.size() : 0, load_ms, keys);
    summary(frame_ms, "frame thread per keystroke (hand over, take hits)");
    const double search_p95 = summary(search_ms, "search on its own thread");
    summary(back_ms, "keystroke to hits on screen (no debounce)");
    std::printf("  frame-thread allocations after warm-up: %llu in %zu keystrokes\n",
                static_cast<unsigned long long>(allocs), keys);
    constexpr double kSearchBudgetMs = 5.0;
    constexpr double kFrameBudgetMs = 0.2;
    const bool ok = allocs == 0 && search_p95 < kSearchBudgetMs && frame_ms.back() < kFrameBudgetMs;
    std::printf("  %s (budget: the search under %.0f ms at p95 off the frame thread; the frame's "
                "part under %.1f ms, no allocation)\n",
                ok ? "PASS" : "FAIL", kSearchBudgetMs, kFrameBudgetMs);
    return ok;
}

} // namespace

int run_search_bench(std::size_t songs) {
    const auto build_t0 = Clock::now();
    BenchLibrary lib(songs);
    const double build_ms = ms_since(build_t0);
    LibraryCatalogue& cat = lib.catalogue();

    // Typed a character at a time, then deleted the same way: titles, composers, catalogue
    // numbers, typos, initials and things that match nothing.
    constexpr std::string_view kQueries[] = {
        "moonlight sonata", "beethoven", "op 27 no 2",      "fur elise",     "gymnopedie",
        "moonlite",         "bethoven",  "chopin nocturne", "clair de lune", "ms",
        "rachmaninov",      "étude",     "the entertainer", "book 2",        "zzqx"};
    double now_s = 1.0;
    std::vector<double> times;
    times.reserve(1024);
    std::uint64_t allocs = 0;
    std::size_t keys = 0;
    std::size_t found_last = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const bool timed = pass == 1; // the first pass warms the buffers up
        for (const std::string_view q : kQueries) {
            const auto key = [&](std::size_t n) {
                const std::uint64_t before = allocations_counted();
                if (timed) {
                    count_allocations(true);
                }
                const auto t0 = Clock::now();
                cat.set_query(q.substr(0, n));
                cat.update_view(BenchLibrary::kToday, now_s);
                const double ms = ms_since(t0);
                if (timed) {
                    count_allocations(false);
                    allocs += allocations_counted() - before;
                    times.push_back(ms);
                    ++keys;
                }
                now_s += 0.1;
            };
            for (std::size_t n = 1; n <= q.size(); ++n) {
                key(n);
            }
            if (timed) {
                found_last = cat.shown().size();
                std::printf("  %-18.*s %5zu found%s\n", static_cast<int>(q.size()), q.data(),
                            found_last, found_last == 0 ? " (nearest shown instead)" : "");
            }
            for (std::size_t n = q.size(); n-- > 0;) {
                key(n);
            }
        }
    }
    std::sort(times.begin(), times.end());
    const auto at = [&](double share) {
        return times[std::min(times.size() - 1,
                              static_cast<std::size_t>(share * static_cast<double>(times.size())))];
    };
    double sum = 0.0;
    for (const double t : times) {
        sum += t;
    }
    std::printf("search bench: %zu songs (catalogue built in %.1f ms), %zu keystrokes\n",
                cat.size(), build_ms, keys);
    std::printf("  per keystroke: min %.3f ms, median %.3f ms, mean %.3f ms, p95 %.3f ms, max %.3f "
                "ms\n",
                times.front(), at(0.5), sum / static_cast<double>(times.size()), at(0.95),
                times.back());
    std::printf("  allocations after warm-up: %llu in %zu keystrokes\n",
                static_cast<unsigned long long>(allocs), keys);
    // A change of the order (each grouping, each sort) works the shown list and the shelves out
    // again in one frame: timed each once, after a warm-up pass.
    cat.set_query("");
    double order_worst = 0.0;
    const char* order_worst_name = "";
    for (int pass = 0; pass < 2; ++pass) {
        for (int g = 0; g < kBrowseGroupCount; ++g) {
            for (int so = 0; so < kBrowseSortCount; ++so) {
                BrowseOrder o;
                o.group = static_cast<BrowseGroup>(g);
                o.sort = static_cast<BrowseSort>(so);
                const auto t0 = Clock::now();
                cat.set_order(o);
                cat.update_view(BenchLibrary::kToday, now_s);
                const double ms = ms_since(t0);
                now_s += 0.1;
                if (pass == 1 && ms > order_worst) {
                    order_worst = ms;
                    static const char* const kGroupNames[] = {"no grouping", "composer", "level",
                                                              "progress", "genre", "my groups"};
                    order_worst_name = kGroupNames[g];
                }
            }
        }
    }
    std::printf("  a change of order (each grouping and sort): worst %.3f ms (%s)\n", order_worst,
                order_worst_name);
    constexpr double kBudgetMs = 5.0;
    constexpr double kOrderBudgetMs = 12.0;
    const bool ok = allocs == 0 && times.back() < kBudgetMs && order_worst < kOrderBudgetMs;
    std::printf("  %s (budget: under %.0f ms a keystroke, no allocation; under %.0f ms a change of "
                "order)\n",
                ok ? "PASS" : "FAIL", kBudgetMs, kOrderBudgetMs);
    constexpr std::size_t kCatalogueSongs = 5000;
    const bool get_ok = catalogue_bench(kCatalogueSongs, kQueries);
    return ok && get_ok ? 0 : 1;
}

int run_scan_bench(const std::string& dir) {
    SongLibrary library({std::filesystem::path(dir)});
    const std::uint64_t splits_before = etude::hand_split_runs();
    const auto scan_t0 = Clock::now();
    const std::size_t listed = library.scan();
    library.start_loading();
    library.wait_until_settled();
    const double scan_ms = ms_since(scan_t0);
    const std::uint64_t scan_splits = etude::hand_split_runs() - splits_before;
    std::printf("scan: %zu files listed, %zu songs read, %zu failed, %.1f ms (%.2f ms a song), "
                "%llu hand splits\n",
                listed, library.state().loaded(), library.state().failed(), scan_ms,
                library.size() > 0 ? scan_ms / static_cast<double>(library.size()) : 0.0,
                static_cast<unsigned long long>(scan_splits));
    if (library.size() == 0) {
        std::fprintf(stderr, "no song files under %s\n", dir.c_str());
        return 1;
    }

    // Opening each song reads and parses it again, as the loader thread does for a PageDown.
    const std::uint64_t open_before = etude::hand_split_runs();
    int failed = 0;
    int split_songs = 0;
    const auto open_t0 = Clock::now();
    for (std::size_t i = 0; i < library.size(); ++i) {
        SongLoadOptions options;
        options.tune = library.tune_at(i);
        options.known = library.abc_known(library.path_at(i), library.tune_at(i));
        const SongFileLoad load = load_song_file(library.path_at(i), options);
        failed += load.ok ? 0 : 1;
        split_songs += load.ok && load.song.hands_split ? 1 : 0;
    }
    const double open_ms = ms_since(open_t0);
    std::printf("open each: %zu songs, %d failed, %.1f ms (%.2f ms a song), %llu hand splits "
                "(%d songs needed one)\n",
                library.size(), failed, open_ms, open_ms / static_cast<double>(library.size()),
                static_cast<unsigned long long>(etude::hand_split_runs() - open_before),
                split_songs);
    std::printf("scan and open together: %.1f ms, %llu hand splits\n", scan_ms + open_ms,
                static_cast<unsigned long long>(scan_splits + etude::hand_split_runs() -
                                                open_before));
    return failed > 0 ? 1 : 0;
}

} // namespace etude::app
