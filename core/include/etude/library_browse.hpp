// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/library_search.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// How the song library is shown: one sort, one grouping and one quick filter, the same in every
// view. Pure functions over a few numbers per song (BrowseSong) the app works out once per change;
// the texts (names, composers, genres) stay with the app, which hands their order over as ranks.

enum class BrowseSort : std::uint8_t {
    Name,
    Composer,
    Difficulty,
    Progress,
    Length,
    Recent,
    Added
};
inline constexpr int kBrowseSortCount = 7;
// MyGroups: the player's own groups (library_groups.hpp), where a song in several shows in each.
enum class BrowseGroup : std::uint8_t { None, Composer, Difficulty, Progress, Genre, MyGroups };
inline constexpr int kBrowseGroupCount = 6;
enum class BrowseFilter : std::uint8_t { All, Favourites, Recent, InProgress };
inline constexpr int kBrowseFilterCount = 4;
// The hands a song is for: any, both, or only the right or only the left.
enum class BrowseHands : std::uint8_t { Any, Both, RightOnly, LeftOnly };
inline constexpr int kBrowseHandsCount = 4;
// The three ways the library is shown; each shares the search, the filters and the order.
enum class BrowseView : std::uint8_t { Grid, List, Shelves };
inline constexpr int kBrowseViewCount = 3;
// The levels a filter keeps: bit (level - 1) for levels 1 to 5; none set keeps every level.
inline constexpr int kAllLevels = 0;
inline constexpr int kLevelBits = 0x1F;

struct BrowseOrder {
    BrowseSort sort = BrowseSort::Name;
    bool reversed = false; // against the sort's natural direction (see browse())
    BrowseGroup group = BrowseGroup::None;
    BrowseFilter filter = BrowseFilter::All;
    int levels = kAllLevels; // the levels shown; a song not read yet passes only "all"
    BrowseHands hands = BrowseHands::Any;

    friend bool operator==(const BrowseOrder&, const BrowseOrder&) = default;
};

// The level and hands filters that are on (0, 1 or 2), for the Filters chip's count.
int facet_count(const BrowseOrder& order);

// A tap on a sort: the sort that is on already turns round, another starts in its natural
// direction.
BrowseOrder with_sort(BrowseOrder order, BrowseSort sort);

// The text a composer sorts by: the surname, then the whole name, folded (fold_for_search). The
// surname is the last word, leaving out a parenthetical ("(after Handel)") and a trailing "Jr",
// "Sr" or Roman numeral: "Ludwig van Beethoven" sorts as "beethoven ludwig van beethoven".
std::string composer_sort_key(std::string_view composer);

// A rank for a song with no such value (no composer, no genre).
inline constexpr std::uint32_t kNoRank = std::numeric_limits<std::uint32_t>::max();
// A best accuracy of this many thousandths or more is mastered.
inline constexpr int kMasteredPermille = 900;
// "Recent" is a song played in the last this many days, today included.
inline constexpr int kRecentDays = 30;

// One song's facts, by the song's index in the library.
struct BrowseSong {
    std::uint32_t name_rank = 0;           // its place when the names are in order
    std::uint32_t composer_rank = kNoRank; // its composer's place (by surname); kNoRank for none
    std::uint32_t genre_rank = kNoRank;    // its genre's place; kNoRank for none ("Other")
    std::uint8_t difficulty = 0;           // 1 to 5; 0 while the song is not read yet
    std::int16_t best_permille = -1;       // the best accuracy, 0..1000; -1 for none
    std::int32_t recency = -1;             // higher is played more recently; -1 never played
    std::int32_t last_day = 0;             // the local day (days since 1970) it was last played
    double duration_s = 0.0;               // 0 while not known
    std::int64_t added_s = 0;              // when the library first listed it (UTC); 0 not known
    bool favourite = false;
    bool left = false;  // it has notes for the left hand
    bool right = false; // and for the right
};

enum class Progress : std::uint8_t { NotStarted, InProgress, Mastered };
// Not started: never played. Mastered: a best of kMasteredPermille or more. In progress: the rest.
Progress progress_of(const BrowseSong& song);

// Whether `song` is in `filter`: All, the favourites, those played in the last kRecentDays days up
// to `today` (a local day), or those in progress.
bool passes_filter(const BrowseSong& song, BrowseFilter filter, std::int32_t today);
// Whether `song` passes the order's level and hands filters.
bool passes_facets(const BrowseSong& song, const BrowseOrder& order);

// A run of songs under one header. `key` is what they share for the grouping: the composer's or
// the genre's rank (kNoRank last: "Unknown composer", "Other"), the level (1 to 5, then 0 for the
// songs not read yet), the Progress value; 0 with no grouping or for search results.
struct BrowseSection {
    std::uint32_t key = 0;
    std::uint32_t begin = 0; // into LibraryBrowser::order()
    std::uint32_t end = 0;
};

// The songs to show, in order, split into sections. Its lists are its own: they are reused, so a
// browse allocates only when the library has grown past what it held before.
class LibraryBrowser {
  public:
    // The songs that pass the order's filter (and `include`, when it is not empty and the song's
    // byte is 0: left out), grouped and then sorted inside each group. Groups come in their fixed
    // order (composers and genres by rank, levels easy to hard, not started to mastered), the
    // missing value last. Natural sort directions: names and composers A to Z, easy first, the
    // best score first, short first, the latest played first, the newest first; `reversed` turns
    // the direction round, but a song with no value for the sort (never played, no score, no
    // composer, not read yet) stays last both ways. Ties go by name, then by index.
    // `groups` holds the player's groups for BrowseGroup::MyGroups, in their order: group g's
    // songs (indices) are songs[offsets[g] .. offsets[g + 1]). Each group is a section (key g, in
    // the groups' order, empty ones left out) holding its songs in the sort's order, a song in
    // several groups in each; the songs in none come last (key kNoRank, "Not in a group").
    struct GroupLists {
        std::span<const std::uint32_t> offsets;
        std::span<const std::uint32_t> songs;
    };
    void browse(std::span<const BrowseSong> songs, const BrowseOrder& order, std::int32_t today,
                std::span<const std::uint8_t> include = {}, GroupLists groups = {});
    // Search results as they are ranked, one section (key 0).
    void ranked(std::span<const SearchHit> hits);

    const std::vector<std::uint32_t>& order() const { return order_; }
    const std::vector<BrowseSection>& sections() const { return sections_; }

  private:
    std::vector<std::uint32_t> order_;
    std::vector<BrowseSection> sections_;
    std::vector<std::uint32_t> place_;  // MyGroups: a passing song's place in the sort, by song
    std::vector<std::uint8_t> grouped_; // and whether it is in any group
    std::vector<std::uint32_t> sorted_;
};

// The grid of cards: as many columns as fit cards at least `min_card_w` wide, `gap` apart, inside
// `pad` on each side of `width` (at least one column, which may be narrower when the width is).
struct GridColumns {
    int columns = 1;
    float card_w = 0.0f;
    float x0 = 0.0f; // the first column's left edge
    float gap = 0.0f;
    float x(int column) const; // a column's left edge
};
GridColumns grid_columns(float width, float min_card_w, float gap, float pad);

// Where the cards and the section headers are, top to bottom, in the scroll's own units: every
// section a header `header_h` tall (none when `headers` is false) then rows of `card_h`, `gap`
// between rows and before each header after the first. It keeps one number per section, so the
// questions below never walk the cards.
class GridRows {
  public:
    void build(std::span<const BrowseSection> sections, int columns, float card_h, float gap,
               float header_h, bool headers);

    float content_height() const { return height_; }
    // The top of card `k` (its index in the order) and its column.
    struct Place {
        float y = 0.0f;
        int column = 0;
    };
    Place place(std::uint32_t k) const;
    // The top of section `s`'s header (its first row when there are no headers).
    float section_top(std::size_t s) const;
    // The cards any part of which lies between `top` and `bottom`: [first, last) in the order.
    struct Range {
        std::uint32_t first = 0;
        std::uint32_t last = 0;
    };
    Range visible(float top, float bottom) const;
    // The header that sticks to the top of a list scrolled by `scroll`: the section whose header
    // went past the top, drawn at `y` (scroll, or less where the next header pushes it up).
    // `section` is -1 when no header has gone past the top, or there are no headers.
    struct Sticky {
        int section = -1;
        float y = 0.0f;
    };
    Sticky sticky(float scroll) const;

  private:
    struct Section {
        float top = 0.0f;       // the header's top
        float cards_top = 0.0f; // the first row's top
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
    };
    std::size_t section_of(std::uint32_t k) const;
    std::vector<Section> sections_;
    int columns_ = 1;
    float card_h_ = 0.0f;
    float gap_ = 0.0f;
    float header_h_ = 0.0f;
    float height_ = 0.0f;
};

} // namespace etude
