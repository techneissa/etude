// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_catalogue.hpp"
#include "etude/song_versions.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;

namespace {

std::string fixture(const char* name) {
    std::ifstream in(std::string(ETUDE_TEST_CATALOGUE_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

const CatalogueEntry* find_title(const std::vector<CatalogueEntry>& list, std::string_view title) {
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const CatalogueEntry& e) { return e.title == title; });
    return it == list.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("catalogues: three, in the order offered, each with its hosts") {
    const auto infos = catalogue_infos();
    REQUIRE(infos.size() == 3);
    CHECK(infos[0].id == CatalogueId::Mutopia);
    CHECK(infos[1].id == CatalogueId::PianoMidi);
    CHECK(infos[2].id == CatalogueId::OpenScoreLieder);
    // piano-midi.de is off (no valid HTTPS); its facts and credit stay.
    CHECK(infos[0].enabled);
    CHECK_FALSE(infos[1].enabled);
    CHECK(infos[2].enabled);
    // Lieder's files and listing come from raw.githubusercontent.com alone.
    REQUIRE(infos[2].hosts.size() == 1);
    CHECK(infos[2].hosts[0] == "raw.githubusercontent.com");
    for (const CatalogueInfo& info : infos) {
        CHECK_FALSE(info.hosts.empty());
        CHECK(info.files.rfind("https://" + std::string(info.hosts[0]) + "/", 0) == 0);
        CatalogueId back{};
        CHECK(catalogue_from_key(info.key, back));
        CHECK(back == info.id);
    }
    // piano-midi.de's licence asks for its credit on every song.
    CHECK(catalogue_info(CatalogueId::PianoMidi).credit ==
          "Bernd Krueger, http://www.piano-midi.de");
    CatalogueId none{};
    CHECK_FALSE(catalogue_from_key("thesession", none));
}

TEST_CASE("catalogues: html_text decodes entities and drops tags") {
    CHECK(html_text("Vocalise &#8470; 1") == "Vocalise \xE2\x84\x96 1");
    CHECK(html_text("<a href=\"x\">Public Domain</a>") == "Public Domain");
    CHECK(html_text("&nbsp;") == "");
    CHECK(html_text("  Fr&eacute;d&eacute;ric\n Chopin ") == "Fr\xC3\xA9" "d\xC3\xA9ric Chopin");
    CHECK(html_text("Etudes, Opus 10\n(1832)\n") == "Etudes, Opus 10 (1832)");
    CHECK(html_text("a &amp; b &lt;c&gt;") == "a & b <c>");
    CHECK(html_text("&unknown; x") == "&unknown; x");
    // A no-break space written out in UTF-8 is a space; letters whose UTF-8 holds the byte A0
    // stay whole.
    CHECK(html_text("a\xC2\xA0" "b") == "a b");
    CHECK(html_text("voil\xC3\xA0") == "voil\xC3\xA0");
    CHECK(html_text("\xD0\xA0" "ondo") == "\xD0\xA0" "ondo");
    // An ampersand far from any semicolon is kept as it is.
    CHECK(html_text("a & b; c") == "a & b; c");
}

TEST_CASE("catalogues: piano-midi.de's start page with odd links reads without a fault") {
    const std::string page = "<a href=\".htm\">x</a><a href=\"a.htm\">a</a><a href=\"b_d.htm\">b</a>"
                             "<a href=\"bach.htm\">Bach</a>";
    const std::vector<std::string> composers = parse_piano_midi_composers(page);
    CHECK(composers == std::vector<std::string>{"https://www.piano-midi.de/a.htm",
                                                "https://www.piano-midi.de/bach.htm"});
    std::string many;
    for (int i = 0; i < 200; ++i) {
        many += "<a href=\"c" + std::to_string(i) + ".htm\">c</a>";
    }
    CHECK(parse_piano_midi_composers(many).size() == kPianoMidiMaxComposers);
}

TEST_CASE("catalogues: Mutopia's listing, a page at a time") {
    std::vector<CatalogueEntry> out;
    bool more = false;
    parse_mutopia_page(fixture("mutopia-piano-630.html"), out, more);
    CHECK(more);
    REQUIRE(out.size() == 10);
    for (const CatalogueEntry& e : out) {
        CHECK(e.source == CatalogueId::Mutopia);
        CHECK_FALSE(e.id.empty());
        CHECK_FALSE(e.title.empty());
        CHECK(e.url.rfind("https://www.mutopiaproject.org/ftp/", 0) == 0);
        CHECK(e.url.substr(e.url.size() - 4) == ".mid");
        CHECK(e.page == "https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=" + e.id);
        CHECK_FALSE(e.licence.empty());
        CHECK(e.composer.find("by ") != 0);
        CHECK(e.composer.find('(') == std::string::npos);
    }
    // Creative Commons pieces credit the project; public-domain ones need no credit.
    const bool any_cc = std::any_of(out.begin(), out.end(), [](const CatalogueEntry& e) {
        return e.licence.rfind("CC BY", 0) == 0 && !e.credit.empty() &&
               e.licence_url.rfind("https://creativecommons.org/licenses/by", 0) == 0;
    });
    CHECK(any_cc);
    const bool any_sa = std::any_of(out.begin(), out.end(), [](const CatalogueEntry& e) {
        return e.licence.rfind("CC BY-SA ", 0) == 0 &&
               e.licence_url.rfind("https://creativecommons.org/licenses/by-sa/", 0) == 0;
    });
    CHECK(any_sa);
    for (const CatalogueEntry& e : out) {
        if (e.licence == "Public domain") {
            CHECK(e.credit.empty());
        }
    }
}

TEST_CASE("catalogues: Mutopia's licences, only those the app may hand on") {
    CatalogueEntry e;
    CHECK(mutopia_licence("Public Domain", e));
    CHECK(e.licence == "Public domain");
    CHECK(e.credit.empty());
    CHECK(mutopia_licence("Creative Commons Attribution 2.5", e));
    CHECK(e.licence == "CC BY 2.5");
    CHECK(e.licence_url == "https://creativecommons.org/licenses/by/2.5/");
    CHECK_FALSE(e.credit.empty());
    CHECK(mutopia_licence("Creative Commons Attribution-ShareAlike 4.0", e));
    CHECK(e.licence == "CC BY-SA 4.0");
    CHECK(e.licence_url == "https://creativecommons.org/licenses/by-sa/4.0/");
    for (const std::string_view other :
         {"", " ", "Creative Commons Attribution", "Creative Commons Attribution-ShareAlike",
          "Creative Commons Attribution-NonCommercial 3.0",
          "Creative Commons Attribution-NonCommercial-ShareAlike 3.0",
          "Creative Commons Attribution-NoDerivs 3.0", "Creative Commons Attribution 3.0 beta",
          "Creative Commons Attribution ../x", "GPL", "Public Domain Mark?"}) {
        CAPTURE(other);
        CHECK_FALSE(mutopia_licence(other, e));
    }
}

TEST_CASE("catalogues: Mutopia leaves out a piece under a licence it does not take") {
    std::string page = fixture("mutopia-solo-last.html");
    std::vector<CatalogueEntry> all;
    bool more = false;
    parse_mutopia_page(page, all, more);
    REQUIRE(all.size() == 6);
    for (const std::string_view other :
         {std::string_view("Creative Commons Attribution-NonCommercial 3.0"), std::string_view()}) {
        std::string changed = page;
        const std::size_t at = changed.find("Public Domain");
        REQUIRE(at != std::string::npos);
        changed.replace(at, 13, other);
        std::vector<CatalogueEntry> out;
        parse_mutopia_page(changed, out, more);
        CHECK(out.size() == 5);
    }
}

TEST_CASE("catalogues: Mutopia leaves out a piece that comes only as a zip of MIDI files") {
    std::vector<CatalogueEntry> out;
    bool more = false;
    const std::string page = fixture("mutopia-piano-500.html");
    parse_mutopia_page(page, out, more);
    CHECK(more);
    CHECK(out.size() < 10);
    CHECK_FALSE(out.empty());
    for (const CatalogueEntry& e : out) {
        CHECK(e.url.find(".zip") == std::string::npos);
    }
}

TEST_CASE("catalogues: Mutopia's last page has no next one") {
    std::vector<CatalogueEntry> out;
    bool more = true;
    parse_mutopia_page(fixture("mutopia-solo-last.html"), out, more);
    CHECK_FALSE(more);
    REQUIRE(out.size() == 6);
    const CatalogueEntry* rondo = find_title(out, "Rondo");
    REQUIRE(rondo);
    CHECK(rondo->composer == "J. Wanhal");
    CHECK(rondo->id == "192");
    CHECK(rondo->licence == "Public domain");
    CHECK(rondo->url == "https://www.mutopiaproject.org/ftp/WanhalJ/O42/wanhal-rondo/wanhal-rondo.mid");
    CHECK(mutopia_page_url(620).find("startat=620") != std::string::npos);
    CHECK(mutopia_page_url(0).rfind("https://www.mutopiaproject.org/cgibin/make-table.cgi?", 0) == 0);
    CHECK(mutopia_page_url(0).find("Instrument=Piano") != std::string::npos);
    CHECK(mutopia_page_url(0).find("solo=1") != std::string::npos);
}

TEST_CASE("catalogues: piano-midi.de's composers and their pieces") {
    const std::vector<std::string> composers = parse_piano_midi_composers(fixture("piano-midi-files.htm"));
    CHECK(composers.size() >= 20);
    CHECK(std::find(composers.begin(), composers.end(), "https://www.piano-midi.de/chopin.htm") !=
          composers.end());
    for (const std::string& c : composers) {
        CHECK(c.rfind("https://www.piano-midi.de/", 0) == 0);
        CHECK(c.find("midi_files") == std::string::npos);
        CHECK(c.find("copy.htm") == std::string::npos);
    }

    std::vector<CatalogueEntry> out;
    parse_piano_midi_composer(fixture("piano-midi-chopin.htm"), "https://www.piano-midi.de/chopin.htm",
                              out);
    CHECK(out.size() >= 20);
    const CatalogueEntry* etude = find_title(out, "Etudes, Opus 10: No. 1");
    REQUIRE(etude);
    CHECK(etude->composer == "Chopin");
    CHECK(etude->url == "https://www.piano-midi.de/midis/chopin/chpn_op10_e01.mid");
    CHECK(etude->detail == "Allegro, 1832");
    CHECK(etude->licence == "CC BY-SA 3.0 DE");
    CHECK(etude->credit == "Bernd Krueger, http://www.piano-midi.de");
    for (const CatalogueEntry& e : out) {
        CHECK(e.url.find("format0") == std::string::npos);
        CHECK(e.credit == "Bernd Krueger, http://www.piano-midi.de");
    }
}

TEST_CASE("catalogues: OpenScore Lieder's listing") {
    std::vector<CatalogueEntry> out;
    parse_openscore_lieder(fixture("lieder-scores.yaml"), out);
    CHECK(out.size() == 46);
    const CatalogueEntry* first = find_title(out, "Just for Today");
    REQUIRE(first);
    CHECK(first->id == "6583477");
    CHECK(first->composer == "Jane Bingham Abbott");
    CHECK(first->detail.empty());
    CHECK(first->licence == "CC0");
    CHECK(first->url ==
          "https://raw.githubusercontent.com/OpenScore/Lieder/main/scores/"
          "Abbott,_Jane_Bingham/_/Just_for_Today/lc6583477.mscx");
    // A song of a set: the set is the detail and its number goes from the title.
    const CatalogueEntry* gute = find_title(out, "Gute Nacht");
    REQUIRE(gute);
    CHECK(gute->composer == "Franz Schubert");
    CHECK(gute->detail == "Winterreise, D.911");
    // A song's own name wins over its path.
    CHECK(find_title(out, "Is my team ploughing?"));
    // Characters beyond ASCII go into the URL as %XX.
    const CatalogueEntry* orphan = find_title(out, "The Orphan\xE2\x80\x99s Prayer");
    REQUIRE(orphan);
    CHECK(orphan->url.find("Orphan%E2%80%99s_Prayer") != std::string::npos);
    CHECK(openscore_lieder_index_url().rfind("https://raw.githubusercontent.com/", 0) == 0);
}

TEST_CASE("catalogues: a song file only from under its catalogue's files") {
    CatalogueEntry e;
    e.source = CatalogueId::OpenScoreLieder;
    const std::string base = "https://raw.githubusercontent.com/OpenScore/Lieder/main/scores/";
    e.url = base + "Abbott,_Jane_Bingham/_/Just_for_Today/lc6583477.mscx";
    CHECK(catalogue_file_allowed(e));
    for (const std::string& bad :
         {base, base + "../../../evil/main/x.mscx", base + "a/../b/lc1.mscx", base + "a/./lc1.mscx",
          base + "a/%2e%2e/lc1.mscx", base + "a/%2E./lc1.mscx", base + "a%2fb/lc1.mscx",
          base + "a//lc1.mscx", base + "a/lc1.mscx?x=1", base + "a/lc1.mscx#x",
          base + "a\\..\\lc1.mscx",
          std::string("https://raw.githubusercontent.com/Evil/Lieder/main/scores/a/lc1.mscx"),
          std::string("https://github.com/OpenScore/Lieder/raw/main/scores/a/lc1.mscx"),
          "http://raw.githubusercontent.com/OpenScore/Lieder/main/scores/a/" + std::string("lc1.mscx")}) {
        CAPTURE(bad);
        e.url = bad;
        CHECK_FALSE(catalogue_file_allowed(e));
    }
    // The listing leaves out a score whose path climbs out of the scores.
    std::vector<CatalogueEntry> out;
    parse_openscore_lieder("1:\n  path: ../../x/y/z\n2:\n  path: A/_/B\n", out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].id == "2");
    e.source = CatalogueId::Mutopia;
    e.url = "https://www.mutopiaproject.org/ftp/BachJS/x.mid";
    CHECK(catalogue_file_allowed(e));
    e.url = "https://www.mutopiaproject.org/cgibin/x.mid";
    CHECK_FALSE(catalogue_file_allowed(e));
}

TEST_CASE("catalogues: the cache reads back what it wrote") {
    std::vector<CatalogueEntry> in;
    parse_openscore_lieder(fixture("lieder-scores.yaml"), in);
    in[0].title = "Tabs\tand\nnew lines \\ kept";
    const std::string text = write_catalogue_cache(CatalogueId::OpenScoreLieder, 1'760'000'000, in);
    std::vector<CatalogueEntry> back;
    std::int64_t when = 0;
    REQUIRE(read_catalogue_cache(text, CatalogueId::OpenScoreLieder, when, back));
    CHECK(when == 1'760'000'000);
    CHECK(back == in);
    // Another catalogue's cache, a newer format or something else is not read.
    CHECK_FALSE(read_catalogue_cache(text, CatalogueId::Mutopia, when, back));
    std::string newer = text;
    newer.replace(newer.find("\t1\t"), 3, "\t2\t");
    CHECK_FALSE(read_catalogue_cache(newer, CatalogueId::OpenScoreLieder, when, back));
    CHECK_FALSE(read_catalogue_cache("garbage", CatalogueId::OpenScoreLieder, when, back));
    std::string torn = text.substr(0, text.size() / 2);
    torn.resize(torn.rfind('\t'));
    CHECK_FALSE(read_catalogue_cache(torn, CatalogueId::OpenScoreLieder, when, back));
}

TEST_CASE("catalogues: an index goes stale after the refresh age") {
    CHECK(catalogue_stale(0, 100));
    CHECK_FALSE(catalogue_stale(1000, 1000 + kCatalogueRefreshS - 1));
    CHECK(catalogue_stale(1000, 1000 + kCatalogueRefreshS));
    CHECK(catalogue_stale(1000, 999)); // a clock set back
}

TEST_CASE("catalogues: a download's file name") {
    CatalogueEntry e;
    e.title = "Etudes, Opus 10: No. 1";
    e.composer = "Chopin";
    e.url = "https://www.piano-midi.de/midis/chopin/chpn_op10_e01.MID?x=1";
    CHECK(catalogue_file_extension(e) == ".mid");
    CHECK(catalogue_file_name(e) == "Chopin - Etudes, Opus 10_ No. 1.mid");
    e.composer.clear();
    e.title = "../a/b?";
    CHECK(catalogue_file_name(e) == "_a_b_.mid");
    e.title = std::string(200, 'x');
    CHECK(catalogue_file_name(e).size() == 124);
    e.title.clear();
    e.id = "42";
    CHECK(catalogue_file_name(e) == "42.mid");
    e.url = "https://example.org/noext/";
    CHECK(catalogue_file_extension(e).empty());
}

// ---- Versions of one piece among the entries (#212)

namespace {

CatalogueEntry entry_of(CatalogueId source, const char* id, const char* title, const char* composer,
                        const char* detail = "") {
    CatalogueEntry e;
    e.source = source;
    e.id = id;
    e.title = title;
    e.composer = composer;
    e.detail = detail;
    return e;
}

std::vector<std::uint32_t> group_members(const CatalogueVersions& v, std::uint32_t entry) {
    std::vector<std::uint32_t> out;
    if (v.group_of[entry] != CatalogueVersions::kNone) {
        for (const std::uint32_t m : v.of(v.group_of[entry])) {
            out.push_back(m);
        }
    }
    return out;
}

} // namespace

TEST_CASE("catalogue versions: the same composer, title and number are one piece") {
    const std::vector<CatalogueEntry> entries = {
        entry_of(CatalogueId::Mutopia, "1", "Prelude in C major, BWV 846", "J. S. Bach", "BWV 846"),
        entry_of(CatalogueId::Mutopia, "2", "Gymnopedie No. 1", "E. Satie"),
        entry_of(CatalogueId::Mutopia, "3", "Prelude in C major (Easy)", "Johann Sebastian Bach",
                 "BWV 846, Baroque"),
        entry_of(CatalogueId::OpenScoreLieder, "4", "Prelude in C major", "Bach, Johann Sebastian",
                 "BWV 846"),
    };
    const CatalogueVersions v = group_catalogue_versions(entries);
    REQUIRE(v.groups() == 1);
    // The file's own label first, then the others by title.
    CHECK(group_members(v, 0) == std::vector<std::uint32_t>{2, 3, 0});
    CHECK(v.group_of[1] == CatalogueVersions::kNone);
    CHECK(v.name_of(2) == "Easy");
    // Two with no level word: each is the original of its catalogue.
    CHECK(v.name_of(0) == "Original \xC2\xB7 Mutopia");
    CHECK(v.name_of(3) == "Original \xC2\xB7 OpenScore");
    CHECK(v.name_of(1).empty());
    CHECK(v.pieces[0] == "Prelude in C major");
}

TEST_CASE("catalogue versions: no number, no composer or another number is not one piece") {
    const std::vector<CatalogueEntry> entries = {
        // The same title and composer with no number: "Minuet" names many pieces.
        entry_of(CatalogueId::Mutopia, "1", "Minuet in G", "C. Petzold"),
        entry_of(CatalogueId::Mutopia, "2", "Minuet in G", "C. Petzold"),
        // Two parts of one opus are two pieces.
        entry_of(CatalogueId::Mutopia, "3", "Nocturne", "F. Chopin", "Op. 27, No. 1"),
        entry_of(CatalogueId::Mutopia, "4", "Nocturne", "F. Chopin", "Op. 27, No. 2"),
        // The same number and composer, other titles: the songs of one set.
        entry_of(CatalogueId::OpenScoreLieder, "5", "Gute Nacht", "Franz Schubert", "Op. 89"),
        entry_of(CatalogueId::OpenScoreLieder, "6", "Die Post", "Franz Schubert", "Op. 89"),
        // The same title and number, another composer.
        entry_of(CatalogueId::Mutopia, "7", "Sonata, Op. 5", "A. Keller"),
        entry_of(CatalogueId::Mutopia, "8", "Sonata, Op. 5", "B. Quill"),
        // No composer known.
        entry_of(CatalogueId::Mutopia, "9", "Sonata, Op. 9", ""),
        entry_of(CatalogueId::Mutopia, "10", "Sonata, Op. 9", ""),
    };
    const CatalogueVersions v = group_catalogue_versions(entries);
    CHECK(v.groups() == 0);
    for (const std::uint32_t g : v.group_of) {
        CHECK(g == CatalogueVersions::kNone);
    }
}

TEST_CASE("catalogue versions: the number may be in the title or in the detail line") {
    const std::vector<CatalogueEntry> entries = {
        entry_of(CatalogueId::Mutopia, "1", "Nocturne in E flat", "F. Chopin", "Op. 9, No. 2"),
        entry_of(CatalogueId::Mutopia, "2", "Nocturne in E flat, Op. 9 No. 2 (Beginner)",
                 "Frédéric Chopin"),
        // A part of the opus is not the opus's other part.
        entry_of(CatalogueId::Mutopia, "3", "Nocturne in E flat", "F. Chopin", "Op. 9, No. 1"),
    };
    const CatalogueVersions v = group_catalogue_versions(entries);
    REQUIRE(v.groups() == 1);
    CHECK(group_members(v, 0) == std::vector<std::uint32_t>{1, 0});
    CHECK(v.name_of(1) == "Beginner");
    CHECK(v.name_of(0) == "Original"); // the only one with no level word
    CHECK(v.group_of[2] == CatalogueVersions::kNone);
}

TEST_CASE("catalogue versions: a group never holds more than kMaxVersions entries") {
    std::vector<CatalogueEntry> entries;
    for (int i = 0; i < 12; ++i) {
        entries.push_back(entry_of(CatalogueId::Mutopia, std::to_string(i).c_str(),
                                   "Prelude in C major, BWV 846", "J. S. Bach"));
    }
    const CatalogueVersions v = group_catalogue_versions(entries);
    std::size_t in_groups = 0;
    for (std::uint32_t g = 0; g < v.groups(); ++g) {
        CHECK(v.of(g).size() <= kMaxVersions);
        CHECK(v.of(g).size() >= 2);
        in_groups += v.of(g).size();
    }
    CHECK(in_groups >= 8);
    // Two from one catalogue are told apart by a number.
    std::vector<std::string> names;
    for (const std::uint32_t m : v.of(0)) {
        names.emplace_back(v.name_of(m));
    }
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}
