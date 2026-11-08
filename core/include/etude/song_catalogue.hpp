// SPDX-License-Identifier: GPL-3.0-or-later
// The free song catalogues "Get songs" offers, and their indexes: what each one is, how its listing
// reads into entries, and the cache the app keeps of them. Plain values and text in and out: the
// app fetches the bytes and keeps the files.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// In the order they are offered.
enum class CatalogueId : std::uint8_t { Mutopia, PianoMidi, OpenScoreLieder };
inline constexpr std::size_t kCatalogueCount = 3;

struct CatalogueInfo {
    CatalogueId id = CatalogueId::Mutopia;
    std::string_view key;     // stored with a song and a catalogue group: "mutopia"
    std::string_view name;    // "Mutopia"
    std::string_view group;   // the group its downloads land in
    std::string_view site;    // its home page
    std::string_view licence; // in a few words, for the credits
    std::string_view about;   // one sentence
    // The credit every song from it shows; empty when the licence is per song.
    std::string_view credit;
    std::span<const std::string_view> hosts; // the only hosts the app reaches for it
    std::string_view files; // where every song file it offers lives (`catalogue_file_allowed`)
    // Off: never fetched, searched or listed; its parser, facts and credit stay for when it is
    // back (and for the songs already downloaded from it).
    bool enabled = true;
};

const CatalogueInfo& catalogue_info(CatalogueId id);
std::span<const CatalogueInfo, kCatalogueCount> catalogue_infos();
// The catalogue whose key is `key`; false when none is.
bool catalogue_from_key(std::string_view key, CatalogueId& out);

// One piece a catalogue offers.
struct CatalogueEntry {
    CatalogueId source = CatalogueId::Mutopia;
    std::string id;       // stable within the catalogue
    std::string title;
    std::string composer;
    std::string detail;   // a short line: opus, instruments, collection
    std::string licence;  // "Public domain", "CC BY 4.0", "CC BY-SA 3.0 DE", "CC0"
    std::string licence_url;
    std::string credit;   // who to credit, when the licence asks
    std::string url;      // the file to download (https)
    std::string page;     // the piece's page on the site

    friend bool operator==(const CatalogueEntry&, const CatalogueEntry&) = default;
};

// ---- Versions of one piece among the entries (#212)

// Entries that are one piece, found from what a listing says alone (no file has been fetched, so
// there is no melody to compare): same composer, same title and the same opus or catalogue number,
// all three present. An entry with no number, or whose composer is unknown, is on its own, however
// like another's its title: "Minuet" or "Prelude in C" names many pieces, and a wrong group would
// download the wrong song. A group holds two to kMaxVersions entries.
struct CatalogueVersions {
    static constexpr std::uint32_t kNone = ~std::uint32_t{0};
    std::vector<std::uint32_t> group_of;  // by entry: its group, kNone for none
    std::vector<std::uint32_t> member_of; // by entry: its place among the group's entries
    std::vector<std::uint32_t> offsets;   // by group, into `members` (one more than the groups)
    std::vector<std::uint32_t> members;   // the entries of each group: a labelled one first, then by title
    std::vector<std::string> names;       // by slot of `members`: the chip's word
    std::vector<std::string> pieces;      // by group: the piece's name

    std::size_t groups() const { return pieces.size(); }
    // The entries of group `g`, in chip order.
    std::span<const std::uint32_t> of(std::uint32_t g) const {
        return {members.data() + offsets[g], offsets[g + 1] - offsets[g]};
    }
    // The chip word of entry `i`; empty when it is on its own.
    std::string_view name_of(std::uint32_t i) const {
        return group_of[i] == kNone ? std::string_view()
                                    : std::string_view(names[offsets[group_of[i]] + member_of[i]]);
    }
};
CatalogueVersions group_catalogue_versions(std::span<const CatalogueEntry> entries);

// The extension of the entry's file, lowercased with its dot (".mid", ".mscx"); empty when the
// URL has none.
std::string catalogue_file_extension(const CatalogueEntry& e);
// The name a downloaded file gets: "Composer - Title.ext" with characters no file system takes
// replaced, at most 120 bytes before the extension.
std::string catalogue_file_name(const CatalogueEntry& e);

// Where a downloaded song came from, kept with the song (by its content hash) so its licence and
// credit show wherever it is.
struct SongOrigin {
    std::string catalogue; // CatalogueInfo::key
    std::string id;
    std::string title;
    std::string composer;
    std::string licence;
    std::string licence_url;
    std::string credit;
    std::string url;  // the file it was downloaded from
    std::string page; // its page on the site
    std::int64_t downloaded_utc_s = 0;

    friend bool operator==(const SongOrigin&, const SongOrigin&) = default;
};
// The entry's file is under its catalogue's `files` and stays there: no "." or ".." segment
// (plain or %-encoded), no encoded slash, no query, fragment or backslash. Every parser and the
// cache reader leave out an entry that is not, and nothing else is ever fetched as a song.
bool catalogue_file_allowed(const CatalogueEntry& e);
SongOrigin song_origin_of(const CatalogueEntry& e, std::int64_t downloaded_utc_s);

// ---- Mutopia: its piano listing, a page of make-table.cgi at a time.

// A piece's licence as the listing words it, into the entry's licence, link and credit. Only
// "Public Domain" and Creative Commons Attribution or Attribution-ShareAlike with a version are
// taken; false for anything else (NonCommercial, no version, none), and the piece is left out.
bool mutopia_licence(std::string_view text, CatalogueEntry& e);
// Reads one page of the listing. Pieces without a single MIDI file (only a zip of several), or
// under a licence `mutopia_licence` does not take, are left out. `more`: the page links a next
// page.
void parse_mutopia_page(std::string_view html, std::vector<CatalogueEntry>& out, bool& more);
// The listing's page that starts at piece `start` (0, 10, 20...).
std::string mutopia_page_url(std::size_t start);
inline constexpr std::size_t kMutopiaPageSize = 10;

// ---- piano-midi.de: a page per composer, then that composer's pieces.

// The composers' pages the start page lists ("chopin.htm").
std::vector<std::string> parse_piano_midi_composers(std::string_view html);
// The pieces on a composer's page (`page`: its own URL, for the links); the credit and licence
// are the site's.
void parse_piano_midi_composer(std::string_view html, std::string_view page,
                               std::vector<CatalogueEntry>& out);
std::string_view piano_midi_start_url();
// The most composer pages read from the start page.
inline constexpr std::size_t kPianoMidiMaxComposers = 64;

// ---- OpenScore Lieder: one listing of the corpus.

void parse_openscore_lieder(std::string_view text, std::vector<CatalogueEntry>& out);
std::string_view openscore_lieder_index_url();

// ---- The app's cache of an index.

// The index as text the app saves: a header with the catalogue and when it was fetched, then one
// entry a line. Read back with read_catalogue_cache; false (and nothing changed) for text that is
// not one, a newer format or another catalogue's.
std::string write_catalogue_cache(CatalogueId id, std::int64_t fetched_utc_s,
                                  std::span<const CatalogueEntry> entries);
bool read_catalogue_cache(std::string_view text, CatalogueId id, std::int64_t& fetched_utc_s,
                          std::vector<CatalogueEntry>& out);

// An index older than this is fetched again (when the player has the catalogues online); a failed
// fetch waits `kCatalogueRetryS` before the next try.
inline constexpr std::int64_t kCatalogueRefreshS = 14 * 24 * 3600;
inline constexpr std::int64_t kCatalogueRetryS = 3600;
bool catalogue_stale(std::int64_t fetched_utc_s, std::int64_t now_utc_s);

// Text from HTML: entities decoded (the named ones a listing uses and numeric ones), tags removed,
// runs of white space made one space, trimmed.
std::string html_text(std::string_view html);

} // namespace etude
