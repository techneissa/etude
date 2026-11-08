// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace etude {

// The song library's search: forgiving, ranked, and fast enough to run on every keystroke.
//
// Words. Every field is folded (fold_for_search: no case, no accents) and cut into words. A word is
// a run of letters and digits. Letters are a to z and every character beyond ASCII except the
// punctuation and symbols, which end a word as ASCII punctuation does: Latin-1's (U+0080 to U+00BF,
// "×" and "÷"), the general punctuation block (U+2000 to U+206F), CJK punctuation (U+3000 to
// U+303F) and the full-width forms' punctuation; the ordinals "º" and "ª" read as "o" and "a". An
// apostrophe inside a word (', U+2018, U+2019, U+02BC) is dropped ("Man's" is "mans"), any other
// character ends the word, and so does a change between letters and digits ("op27" is "op 27",
// "BWV847" is "bwv 847"). Number
// words are spelled one way: "opus" is "op"; "number", "nr" and "num" are "no". So "op 27" finds
// "Op. 27", "no 2" finds "No. 2" and "opus 27" finds both.
//
// Matching. Every word of the query must match a word of the song (its title, composer, tags,
// folder or file name). A query word matches a song word
//   - exactly;
//   - as its beginning ("moon" finds "Moonlight");
//   - with typos, in the title, the composer and the tags only: one edit for a query word of
//     `kOneTypoLetters` (4) to 7 bytes, two from `kTwoTypoLetters` (8), where an edit is a letter
//     put in, left out or changed, measured against the song word or any of its beginnings (so a
//     half-typed word with a typo still finds it). A word with a digit never matches with a typo.
// A query word of two or more letters also matches the title when it begins the title's initials
// ("ms" finds "Moonlight Sonata"; the initials are the first letters of its words of letters).
//
// Ranking. A song's rank is the weakest kind of match any query word needed:
//   ExactTitle    the title is the query, word for word;
//   TitlePrefix   the title begins with the query (its last word may be a beginning);
//   TitleWord     every query word matched a title word, or the initials;
//   Composer      a word needed the composer or the tags;
//   Typo          a word needed a typo;
//   FolderOrFile  a word matched only the folder or the file name.
// Inside a rank: the lower cost first (each query word costs 0 for an exact word, 1 for a beginning
// or the initials, 3 for one edit, 5 for two, summed), then the more recently played, then the
// favourites, then the order the songs were given in.
//
// Speed. The words of all songs are kept once each (a vocabulary), so a keystroke compares the
// query with each distinct word once and then reads every song's word list. Nothing is allocated
// by `search` or `closest` once the index is built: every buffer they use is sized by `set`.

inline constexpr std::size_t kOneTypoLetters = 4;
inline constexpr std::size_t kTwoTypoLetters = 8;
// The query's first words that count, and the bytes of a word that count (the rest is ignored).
inline constexpr std::size_t kMaxQueryWords = 8;
inline constexpr std::size_t kMaxSearchWordBytes = 32;
// The letters of the title's initials that are kept.
inline constexpr std::size_t kMaxInitials = 15;
// closest(): what a query word that matched nothing adds to a hit's cost (more than any edits).
inline constexpr std::uint16_t kClosestMissCost = 32;

// One song as the search sees it (UTF-8, any of them may be empty).
struct SearchDoc {
    std::string_view title;
    std::string_view composer;
    std::string_view tags;      // the genre and the player's tags
    std::string_view folder;    // the folders the file is in, '/' separated
    std::string_view file_name; // the file's own name
};

enum class MatchRank : std::uint8_t {
    ExactTitle,
    TitlePrefix,
    TitleWord,
    Composer,
    Typo,
    FolderOrFile,
};

struct SearchHit {
    std::uint32_t song = 0; // index into the docs
    MatchRank rank = MatchRank::TitleWord;
    std::uint16_t cost = 0;
};

// What breaks ties inside a rank, by song index; an empty span counts as none. `recency`: higher is
// played more recently, negative never. `favourite`: non-zero for a favourite.
struct SearchTies {
    std::span<const std::int32_t> recency;
    std::span<const std::uint8_t> favourite;
};

// The edit distance (Levenshtein) of `a` and `b`, or `max + 1` when it is more than `max`.
int bounded_edit_distance(std::string_view a, std::string_view b, int max);
// The least edit distance of `query` and `word` or any beginning of `word`, or `max + 1` when it is
// more than `max`. "moonlite" is 2 from "moonligh", a beginning of "moonlight".
int prefix_edit_distance(std::string_view query, std::string_view word, int max);
// The typos a query word of `bytes` bytes may have: 0 below kOneTypoLetters, 1, or 2 from
// kTwoTypoLetters.
int typos_allowed(std::size_t bytes);

class SearchIndex {
  public:
    // Replaces the index with `docs` (allocates).
    void build(std::span<const SearchDoc> docs);
    // Adds or replaces song `song` (allocates); a song past the end grows the index, the songs
    // between empty.
    void set(std::size_t song, const SearchDoc& doc);
    // Keeps the first `n` songs.
    void resize(std::size_t n);
    std::size_t size() const { return songs_.size(); }
    std::size_t vocabulary_size() const { return words_.size(); }

    // The songs matching `query`, best first (see above). `include`, when not empty, leaves out a
    // song whose byte is 0. A query with no word gives no hits. The list is the index's own and
    // stays valid until the next call.
    const std::vector<SearchHit>& search(std::string_view query, const SearchTies& ties = {},
                                         std::span<const std::uint8_t> include = {});

    // The words `query` has (at most kMaxQueryWords): 0 for one of punctuation alone ("-", "'"),
    // which a search treats as no query. Allocates nothing once warm.
    std::size_t query_words(std::string_view query) { return read_query(query); }

    // For a query nothing matches: up to `limit` songs nearest to it, where each query word may be
    // a third of its length wrong (at least 1, at most 3 edits; a word with a digit must match as
    // in `search`) and a song needs only one of the query's words. Every hit has the rank Typo and
    // the cost `kClosestMissCost` per query word it did not match plus its edits, lowest first,
    // then as in `search`.
    const std::vector<SearchHit>& closest(std::string_view query, std::size_t limit,
                                          const SearchTies& ties = {},
                                          std::span<const std::uint8_t> include = {});

  private:
    enum Field : std::uint8_t { Title, Composer, Tags, Folder, File };
    struct Word {
        std::uint32_t offset = 0;
        std::uint8_t length = 0;
        bool digits = false;    // has a digit: never matched with a typo
        std::uint32_t mask = 0; // which of a..z, digits and other bytes it has
    };
    struct Ref {
        std::uint32_t word = 0;
        Field field = Title;
    };
    struct Song {
        std::uint32_t begin = 0; // its refs: the title's words first, in order
        std::uint16_t count = 0;
        std::uint8_t title_words = 0;
        std::uint8_t initials_length = 0;
        std::array<char, kMaxInitials> initials{};
    };
    struct Token {
        std::array<char, kMaxSearchWordBytes> text{};
        std::uint8_t length = 0;
        bool digits = false;
        std::uint32_t mask = 0;
        std::string_view view() const { return {text.data(), length}; }
    };

    std::uint32_t intern(std::string_view word);
    std::string_view word_text(std::uint32_t id) const {
        return {chars_.data() + words_[id].offset, words_[id].length};
    }
    void add_field(std::string_view text, Field field, Song& song, bool title);
    std::size_t read_query(std::string_view query);
    void sort_hits(const SearchTies& ties);
    void compact();

    std::string chars_;
    std::vector<Word> words_;
    std::unordered_map<std::string, std::uint32_t> ids_;
    std::vector<Ref> refs_;
    std::size_t garbage_ = 0; // refs no song points at any more
    std::vector<Song> songs_;
    // Per keystroke, sized by set().
    std::string folded_;
    std::array<Token, kMaxQueryWords> tokens_{};
    std::vector<std::uint8_t> classes_; // query word x vocabulary word
    std::vector<SearchHit> hits_;
    std::string scratch_; // set(): one field folded
};

} // namespace etude
