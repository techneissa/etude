// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_search.hpp"

#include "etude/library.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace etude {

namespace {

// What one query word is to one vocabulary word.
enum Class : std::uint8_t { None = 0, Exact, Prefix, Typo1, Typo2 };

constexpr std::uint32_t kDigitBit = 1u << 26;
constexpr std::uint32_t kOtherBit = 1u << 27;
// A song's words beyond this many are not indexed (a title is never that long).
constexpr std::size_t kMaxSongRefs = 255;
// The bytes the query is folded into without allocating (a typed query is far shorter).
constexpr std::size_t kQueryReserve = 512;
// The longest word the distance functions handle on the stack; longer ones are cut there.
constexpr std::size_t kMaxDistanceBytes = 64;

std::uint32_t byte_mask(unsigned char c) {
    if (c >= 'a' && c <= 'z') {
        return 1u << (c - 'a');
    }
    if (c >= '0' && c <= '9') {
        return kDigitBit;
    }
    return kOtherBit;
}

// Decodes one code point of valid UTF-8 at `i`; returns its length (1 for a stray byte).
std::size_t next_code_point(std::string_view s, std::size_t i, char32_t& cp) {
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char b0 = b(i);
    std::size_t n = b0 < 0x80 ? 1 : b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : b0 >= 0xC0 ? 2 : 1;
    if (i + n > s.size()) {
        n = 1;
    }
    if (n == 1) {
        cp = b0;
        return 1;
    }
    cp = b0 & (n == 2 ? 0x1Fu : n == 3 ? 0x0Fu : 0x07u);
    for (std::size_t k = 1; k < n; ++k) {
        cp = (cp << 6) | (b(i + k) & 0x3Fu);
    }
    return n;
}

enum class CharKind : std::uint8_t { Letter, Digit, Apostrophe, Separator };

// How a folded character takes part in a word; `as` is a letter it stands for (º is "o").
CharKind char_kind(char32_t cp, char& as) {
    as = 0;
    if ((cp >= 'a' && cp <= 'z')) {
        return CharKind::Letter;
    }
    if (cp >= '0' && cp <= '9') {
        return CharKind::Digit;
    }
    if (cp == '\'' || cp == 0x2018 || cp == 0x2019 || cp == 0x02BC) {
        return CharKind::Apostrophe;
    }
    if (cp == 0xBA) {
        as = 'o'; // the ordinal "nº"
        return CharKind::Letter;
    }
    if (cp == 0xAA) {
        as = 'a';
        return CharKind::Letter;
    }
    if (cp < 0xC0 || (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F) ||
        (cp >= 0xFF00 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) ||
        (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65) || cp == 0xD7 ||
        cp == 0xF7) {
        return CharKind::Separator;
    }
    return CharKind::Letter;
}

// One word as it is cut out of folded text, in a fixed buffer.
struct WordBuf {
    std::array<char, kMaxSearchWordBytes> text{};
    std::size_t length = 0;
    bool digits = false;
    bool ascii_start = false;

    std::string_view view() const { return {text.data(), length}; }
    void push(const char* bytes, std::size_t n) {
        if (length + n <= text.size()) {
            std::memcpy(text.data() + length, bytes, n);
            length += n;
        }
    }
};

// The one spelling of a number word.
std::string_view canonical(std::string_view w) {
    if (w == "opus") {
        return "op";
    }
    if (w == "number" || w == "nr" || w == "num") {
        return "no";
    }
    return w;
}

// Calls `emit(WordBuf&)` for each word of folded text.
template <class Emit> void for_each_word(std::string_view folded, Emit&& emit) {
    WordBuf word;
    bool open = false;
    CharKind open_kind = CharKind::Letter;
    const auto close = [&] {
        if (open && word.length > 0) {
            const std::string_view c = canonical(word.view());
            if (c.size() != word.length) {
                std::memcpy(word.text.data(), c.data(), c.size());
                word.length = c.size();
            }
            emit(word);
        }
        open = false;
        word = WordBuf{};
    };
    for (std::size_t i = 0; i < folded.size();) {
        char32_t cp = 0;
        const std::size_t n = next_code_point(folded, i, cp);
        char as = 0;
        const CharKind kind = char_kind(cp, as);
        if (kind == CharKind::Separator) {
            close();
        } else if (kind == CharKind::Apostrophe) {
            // Dropped inside a word, a separator elsewhere.
            if (!open) {
                close();
            }
        } else {
            if (open && kind != open_kind) {
                close();
            }
            if (!open) {
                open = true;
                open_kind = kind;
                word.ascii_start = cp < 0x80 || as != 0;
            }
            word.digits = word.digits || kind == CharKind::Digit;
            if (as != 0) {
                word.push(&as, 1);
            } else {
                word.push(folded.data() + i, n);
            }
        }
        i += n;
    }
    close();
}

// The distance table's work for both distance functions: the least distance of `a` to `b` whole
// (`prefix` false) or to any beginning of `b` (true), or max + 1.
int banded_distance(std::string_view a, std::string_view b, int max, bool prefix) {
    if (max < 0) {
        return max + 1;
    }
    a = a.substr(0, kMaxDistanceBytes);
    b = b.substr(0, kMaxDistanceBytes);
    const int m = static_cast<int>(a.size());
    const int n = static_cast<int>(b.size());
    const int inf = max + 1;
    if (!prefix && std::abs(m - n) > max) {
        return inf;
    }
    if (m == 0) {
        return prefix ? 0 : std::min(n, inf);
    }
    std::array<int, kMaxDistanceBytes + 1> prev{};
    std::array<int, kMaxDistanceBytes + 1> cur{};
    for (int j = 0; j <= n; ++j) {
        prev[static_cast<std::size_t>(j)] = std::min(j, inf);
    }
    for (int i = 1; i <= m; ++i) {
        const int lo = std::max(0, i - max);
        const int hi = std::min(n, i + max);
        std::fill(cur.begin(), cur.begin() + n + 1, inf);
        int row_min = inf;
        if (lo == 0) {
            cur[0] = std::min(i, inf);
            row_min = cur[0];
        }
        for (int j = std::max(1, lo); j <= hi; ++j) {
            const auto ju = static_cast<std::size_t>(j);
            const int sub =
                prev[ju - 1] + (a[static_cast<std::size_t>(i - 1)] != b[ju - 1] ? 1 : 0);
            const int del = prev[ju] + 1;
            const int ins = cur[ju - 1] + 1;
            const int v = std::min({sub, del, ins, inf});
            cur[ju] = v;
            row_min = std::min(row_min, v);
        }
        if (row_min >= inf) {
            return inf;
        }
        std::swap(prev, cur);
    }
    if (!prefix) {
        return prev[static_cast<std::size_t>(n)];
    }
    int best = inf;
    for (int j = std::max(0, m - max); j <= std::min(n, m + max); ++j) {
        best = std::min(best, prev[static_cast<std::size_t>(j)]);
    }
    return best;
}

std::uint32_t mask_of(std::string_view word) {
    std::uint32_t mask = 0;
    for (const char c : word) {
        mask |= byte_mask(static_cast<unsigned char>(c));
    }
    return mask;
}

// What query word `t` (allowed `typos`) is to vocabulary word `w`.
Class classify(std::string_view t, std::uint32_t t_mask, std::string_view w, std::uint32_t w_mask,
               bool w_digits, int typos) {
    if (w.size() >= t.size() && w.compare(0, t.size(), t) == 0) {
        return w.size() == t.size() ? Exact : Prefix;
    }
    if (typos == 0 || w_digits || w.size() + static_cast<std::size_t>(typos) < t.size()) {
        return None;
    }
    // A byte of the query the word lacks altogether needs an edit of its own.
    if (std::popcount(t_mask & ~w_mask) > typos) {
        return None;
    }
    const int d = banded_distance(t, w, typos, true);
    if (d > typos) {
        return None;
    }
    return d == 1 ? Typo1 : Typo2;
}

} // namespace

int typos_allowed(std::size_t bytes) {
    return bytes >= kTwoTypoLetters ? 2 : bytes >= kOneTypoLetters ? 1 : 0;
}

// ---- Building

std::uint32_t SearchIndex::intern(std::string_view word) {
    std::string key(word);
    if (const auto it = ids_.find(key); it != ids_.end()) {
        return it->second;
    }
    const auto id = static_cast<std::uint32_t>(words_.size());
    Word w;
    w.offset = static_cast<std::uint32_t>(chars_.size());
    w.length = static_cast<std::uint8_t>(word.size());
    w.mask = mask_of(word);
    w.digits = (w.mask & kDigitBit) != 0;
    chars_.append(word);
    words_.push_back(w);
    ids_.emplace(std::move(key), id);
    return id;
}

void SearchIndex::add_field(std::string_view text, Field field, Song& song, bool title) {
    fold_for_search_into(text, scratch_);
    for_each_word(scratch_, [&](const WordBuf& w) {
        if (song.count >= kMaxSongRefs) {
            return;
        }
        refs_.push_back({intern(w.view()), field});
        ++song.count;
        if (title) {
            ++song.title_words;
            if (!w.digits && w.ascii_start && song.initials_length < kMaxInitials) {
                song.initials[song.initials_length++] = w.text[0];
            }
        }
    });
}

void SearchIndex::set(std::size_t song, const SearchDoc& doc) {
    if (song >= songs_.size()) {
        songs_.resize(song + 1);
    }
    Song& s = songs_[song];
    garbage_ += s.count;
    s = Song{};
    s.begin = static_cast<std::uint32_t>(refs_.size());
    // The title first: its words are read in order for the exact and prefix ranks.
    add_field(doc.title, Title, s, true);
    add_field(doc.composer, Composer, s, false);
    add_field(doc.tags, Tags, s, false);
    add_field(doc.folder, Folder, s, false);
    add_field(doc.file_name, File, s, false);
    if (garbage_ > refs_.size() / 2) {
        compact();
    }
    // Every buffer a keystroke needs, sized now.
    classes_.resize(kMaxQueryWords * words_.size());
    hits_.reserve(songs_.size());
    folded_.reserve(kQueryReserve);
}

void SearchIndex::resize(std::size_t n) {
    for (std::size_t i = n; i < songs_.size(); ++i) {
        garbage_ += songs_[i].count;
    }
    songs_.resize(n);
}

void SearchIndex::build(std::span<const SearchDoc> docs) {
    chars_.clear();
    words_.clear();
    ids_.clear();
    refs_.clear();
    garbage_ = 0;
    songs_.clear();
    songs_.reserve(docs.size());
    for (std::size_t i = 0; i < docs.size(); ++i) {
        set(i, docs[i]);
    }
}

void SearchIndex::compact() {
    std::vector<Ref> packed;
    packed.reserve(refs_.size() - garbage_);
    for (Song& s : songs_) {
        const std::uint32_t begin = static_cast<std::uint32_t>(packed.size());
        packed.insert(packed.end(), refs_.begin() + s.begin, refs_.begin() + s.begin + s.count);
        s.begin = begin;
    }
    refs_ = std::move(packed);
    garbage_ = 0;
}

// ---- Searching

std::size_t SearchIndex::read_query(std::string_view query) {
    fold_for_search_into(query, folded_);
    std::size_t n = 0;
    for_each_word(folded_, [&](const WordBuf& w) {
        if (n >= kMaxQueryWords) {
            return;
        }
        Token& t = tokens_[n++];
        std::memcpy(t.text.data(), w.text.data(), w.length);
        t.length = static_cast<std::uint8_t>(w.length);
        t.digits = w.digits;
        t.mask = mask_of(t.view());
    });
    return n;
}

void SearchIndex::sort_hits(const SearchTies& ties) {
    const auto recency = [&](std::uint32_t s) {
        return s < ties.recency.size() ? ties.recency[s] : std::int32_t{-1};
    };
    const auto favourite = [&](std::uint32_t s) {
        return s < ties.favourite.size() && ties.favourite[s] != 0;
    };
    std::sort(hits_.begin(), hits_.end(), [&](const SearchHit& a, const SearchHit& b) {
        if (a.rank != b.rank) {
            return a.rank < b.rank;
        }
        if (a.cost != b.cost) {
            return a.cost < b.cost;
        }
        const std::int32_t ra = recency(a.song);
        const std::int32_t rb = recency(b.song);
        if (ra != rb) {
            return ra > rb;
        }
        const bool fa = favourite(a.song);
        const bool fb = favourite(b.song);
        if (fa != fb) {
            return fa;
        }
        return a.song < b.song;
    });
}

const std::vector<SearchHit>& SearchIndex::search(std::string_view query, const SearchTies& ties,
                                                  std::span<const std::uint8_t> include) {
    hits_.clear();
    const std::size_t nt = read_query(query);
    if (nt == 0) {
        return hits_;
    }
    const std::size_t vocab = words_.size();
    for (std::size_t t = 0; t < nt; ++t) {
        const Token& tok = tokens_[t];
        const int typos = tok.digits ? 0 : typos_allowed(tok.length);
        std::uint8_t* row = classes_.data() + t * vocab;
        for (std::size_t w = 0; w < vocab; ++w) {
            const Word& word = words_[w];
            row[w] = classify(tok.view(), tok.mask, {chars_.data() + word.offset, word.length},
                              word.mask, word.digits, typos);
        }
    }
    constexpr auto kNone = static_cast<std::uint8_t>(MatchRank::FolderOrFile) + 1;
    std::array<std::uint8_t, kMaxQueryWords> kind{};
    std::array<std::uint8_t, kMaxQueryWords> cost{};
    for (std::size_t si = 0; si < songs_.size(); ++si) {
        if (!include.empty() && (si >= include.size() || include[si] == 0)) {
            continue;
        }
        const Song& s = songs_[si];
        kind.fill(kNone);
        cost.fill(0);
        const Ref* refs = refs_.data() + s.begin;
        for (std::size_t r = 0; r < s.count; ++r) {
            const Ref ref = refs[r];
            const bool place = ref.field == Folder || ref.field == File;
            for (std::size_t t = 0; t < nt; ++t) {
                const std::uint8_t c = classes_[t * vocab + ref.word];
                if (c == None) {
                    continue;
                }
                std::uint8_t k = 0;
                std::uint8_t price = 0;
                if (c == Exact || c == Prefix) {
                    k = static_cast<std::uint8_t>(place                ? MatchRank::FolderOrFile
                                                  : ref.field == Title ? MatchRank::TitleWord
                                                                       : MatchRank::Composer);
                    price = c == Exact ? 0 : 1;
                } else if (!place) {
                    k = static_cast<std::uint8_t>(MatchRank::Typo);
                    price = c == Typo1 ? 3 : 5;
                } else {
                    continue;
                }
                if (k < kind[t] || (k == kind[t] && price < cost[t])) {
                    kind[t] = k;
                    cost[t] = price;
                }
            }
        }
        // The initials: a word of letters that begins them.
        bool all = true;
        std::uint8_t rank = 0;
        std::uint16_t total = 0;
        for (std::size_t t = 0; t < nt; ++t) {
            const Token& tok = tokens_[t];
            const auto title_word = static_cast<std::uint8_t>(MatchRank::TitleWord);
            if (kind[t] > title_word && !tok.digits && tok.length >= 2 &&
                tok.length <= s.initials_length &&
                std::memcmp(tok.text.data(), s.initials.data(), tok.length) == 0) {
                kind[t] = title_word;
                cost[t] = 1;
            }
            if (kind[t] == kNone) {
                all = false;
                break;
            }
            rank = std::max(rank, kind[t]);
            total = static_cast<std::uint16_t>(total + cost[t]);
        }
        if (!all) {
            continue;
        }
        SearchHit hit;
        hit.song = static_cast<std::uint32_t>(si);
        hit.rank = static_cast<MatchRank>(rank);
        hit.cost = total;
        if (hit.rank == MatchRank::TitleWord && nt <= s.title_words) {
            // The title word for word, or its beginning.
            bool lead = true;
            std::uint8_t last = None;
            for (std::size_t t = 0; t < nt && lead; ++t) {
                last = classes_[t * vocab + refs[t].word];
                lead = last == Exact || (t + 1 == nt && last == Prefix);
            }
            if (lead) {
                hit.rank = nt == s.title_words && last == Exact ? MatchRank::ExactTitle
                                                                : MatchRank::TitlePrefix;
            }
        }
        hits_.push_back(hit);
    }
    sort_hits(ties);
    return hits_;
}

const std::vector<SearchHit>& SearchIndex::closest(std::string_view query, std::size_t limit,
                                                   const SearchTies& ties,
                                                   std::span<const std::uint8_t> include) {
    hits_.clear();
    const std::size_t nt = read_query(query);
    if (nt == 0 || limit == 0) {
        return hits_;
    }
    // classes_ holds the edits plus one here (0: no match), each word allowed a third of its
    // length.
    const std::size_t vocab = words_.size();
    for (std::size_t t = 0; t < nt; ++t) {
        const Token& tok = tokens_[t];
        const int typos = tok.digits ? 0 : std::clamp(static_cast<int>(tok.length) / 3, 1, 3);
        std::uint8_t* row = classes_.data() + t * vocab;
        for (std::size_t w = 0; w < vocab; ++w) {
            const Word& word = words_[w];
            const std::string_view text{chars_.data() + word.offset, word.length};
            int d = typos + 1;
            if (text.size() >= tok.length && text.compare(0, tok.length, tok.view()) == 0) {
                d = 0;
            } else if (typos > 0 && !word.digits && std::popcount(tok.mask & ~word.mask) <= typos) {
                d = banded_distance(tok.view(), text, typos, true);
            }
            row[w] = d <= typos ? static_cast<std::uint8_t>(d + 1) : std::uint8_t{0};
        }
    }
    std::array<std::uint8_t, kMaxQueryWords> best{};
    for (std::size_t si = 0; si < songs_.size(); ++si) {
        if (!include.empty() && (si >= include.size() || include[si] == 0)) {
            continue;
        }
        const Song& s = songs_[si];
        best.fill(0);
        const Ref* refs = refs_.data() + s.begin;
        for (std::size_t r = 0; r < s.count; ++r) {
            if (refs[r].field == Folder || refs[r].field == File) {
                continue;
            }
            for (std::size_t t = 0; t < nt; ++t) {
                const std::uint8_t c = classes_[t * vocab + refs[r].word];
                if (c != 0 && (best[t] == 0 || c < best[t])) {
                    best[t] = c;
                }
            }
        }
        std::uint16_t cost = 0;
        std::size_t matched = 0;
        for (std::size_t t = 0; t < nt; ++t) {
            if (best[t] == 0) {
                cost = static_cast<std::uint16_t>(cost + kClosestMissCost);
            } else {
                ++matched;
                cost = static_cast<std::uint16_t>(cost + best[t] - 1);
            }
        }
        if (matched > 0) {
            hits_.push_back({static_cast<std::uint32_t>(si), MatchRank::Typo, cost});
        }
    }
    sort_hits(ties);
    if (hits_.size() > limit) {
        hits_.resize(limit);
    }
    return hits_;
}

} // namespace etude
