// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_versions.hpp"

#include "song_versions_internal.hpp"

#include "etude/difficulty.hpp"
#include "etude/library.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace etude {

// ---- Grouping

namespace {

// What grouping works out once per song.
struct Prep {
    std::string composer;          // surname key; empty for none
    std::string key;               // the title's words
    std::vector<std::string> core; // sorted, for "one title holds the other's words"
    std::string catalogue;
    VersionLabel label = VersionLabel::None;
    bool usable = false;
    std::vector<int> pitch; // the melody as pitches from the first note, when usable
};

Prep prep_of(const VersionSong& s) {
    Prep p;
    TitleFacts f = facts_of_title(s.title);
    p.composer = composer_surname_key(s.composer);
    p.key = join(f.core);
    p.catalogue = std::move(f.catalogue);
    p.core = std::move(f.core);
    std::sort(p.core.begin(), p.core.end());
    p.label = f.label != VersionLabel::None ? f.label : version_label(s.title, s.file_name);
    p.usable = s.melody.usable();
    if (p.usable) {
        p.pitch = pitches_of(s.melody);
    }
    return p;
}

bool contains_all(const std::vector<std::string>& big, const std::vector<std::string>& small) {
    return std::includes(big.begin(), big.end(), small.begin(), small.end());
}

enum class Strength : std::uint8_t { Melody, Title, TitleAndComposer, Number };

struct Verdict {
    bool same = false;
    Strength strength = Strength::Melody;
};

// Two catalogue numbers that name different works: "op27/1" and "op27/2", not "op27" and
// "op27/1" (the work and one of its parts) and not one that is missing.
bool catalogues_differ(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty() || a == b) {
        return false;
    }
    const auto part_of = [](const std::string& whole, const std::string& part) {
        return part.size() > whole.size() && part.compare(0, whole.size(), whole) == 0 &&
               part[whole.size()] == '/';
    };
    return !part_of(a, b) && !part_of(b, a);
}

// `sim` is the melodies' similarity, or negative when one of them says too little to compare.
Verdict judge(const Prep& a, const Prep& b, double sim) {
    const bool both_composers = !a.composer.empty() && !b.composer.empty();
    if (both_composers && a.composer != b.composer) {
        return {};
    }
    // "Nocturne Op. 27 No. 1" and "No. 2" are two pieces whatever the title and melody say.
    if (catalogues_differ(a.catalogue, b.catalogue)) {
        return {};
    }
    const bool same_title = !a.key.empty() && a.key == b.key;
    const bool same_number = !a.catalogue.empty() && a.catalogue == b.catalogue;
    if (same_title && same_number && both_composers) {
        return {sim < 0.0 || sim >= kVersionVeto, Strength::Number};
    }
    if (same_number && both_composers &&
        (contains_all(a.core, b.core) || contains_all(b.core, a.core))) {
        return {sim >= kVersionConfirm, Strength::TitleAndComposer};
    }
    if (same_title && both_composers) {
        return {sim < 0.0 || sim >= kVersionConfirm, Strength::TitleAndComposer};
    }
    if (same_title) {
        // One composer unknown: the melody confirms it, or says nothing when a file is too short.
        return {sim < 0.0 || sim >= kVersionConfirm, Strength::Title};
    }
    return {sim >= kVersionMelody, Strength::Melody};
}

// Sorting key of a level for the order of versions: the file's own word, in the order of the words.
int label_rank(VersionLabel l) {
    switch (l) {
    case VersionLabel::Beginner:
        return 0;
    case VersionLabel::Easy:
        return 1;
    case VersionLabel::Medium:
        return 2;
    case VersionLabel::None:
        return 3;
    case VersionLabel::Hard:
        return 4;
    case VersionLabel::Expert:
        return 5;
    case VersionLabel::Original:
        return 6;
    }
    return 3;
}

struct Pair {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    Strength strength = Strength::Melody;
    double sim = 0.0;
};

// The 4-grams of a contour's steps, each once: songs sharing some are worth comparing. They are
// the same in any key, and an ornament spoils only the few that run through it.
std::vector<std::uint32_t> grams_of(const MelodyContour& c) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i + 3 < c.steps.size(); ++i) {
        out.push_back((static_cast<std::uint32_t>(c.steps[i] + 128) << 24) |
                      (static_cast<std::uint32_t>(c.steps[i + 1] + 128) << 16) |
                      (static_cast<std::uint32_t>(c.steps[i + 2] + 128) << 8) |
                      static_cast<std::uint32_t>(c.steps[i + 3] + 128));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

constexpr std::size_t kBucketMax = 24;  // songs sharing a title, and compared pair by pair
constexpr std::size_t kPostingMax = 64; // songs sharing a 4-gram: more than this says nothing
constexpr std::size_t kSharedGrams = 2; // 4-grams two melodies share, at least, to be compared
constexpr double kSharedShare = 0.2;    // and this share of the shorter one's

} // namespace

bool same_piece(const VersionSong& a, const VersionSong& b) {
    const Prep pa = prep_of(a);
    const Prep pb = prep_of(b);
    Scratch scratch;
    const double sim = pa.usable && pb.usable ? similarity_of(pa.pitch, pb.pitch, scratch) : -1.0;
    return judge(pa, pb, sim).same;
}

bool VersionOverrides::split(std::uint64_t hash) const {
    return std::binary_search(splits.begin(), splits.end(), hash);
}

bool VersionOverrides::joined(std::uint64_t hash) const {
    return std::any_of(joins.begin(), joins.end(),
                       [&](const auto& j) { return j.first == hash || j.second == hash; });
}

void apply_version_edit(VersionOverrides& o, const VersionEdit& edit,
                        std::vector<VersionRow>* rows) {
    const auto note = [&](std::uint64_t a, std::uint64_t b, bool add) {
        if (rows != nullptr) {
            rows->push_back({a, b, add});
        }
    };
    const auto drop_split = [&](std::uint64_t h) {
        const auto it = std::lower_bound(o.splits.begin(), o.splits.end(), h);
        if (it != o.splits.end() && *it == h) {
            o.splits.erase(it);
            note(h, 0, false);
        }
    };
    const auto drop_joins = [&](std::uint64_t h) {
        for (auto it = o.joins.begin(); it != o.joins.end();) {
            if (it->first == h || it->second == h) {
                note(it->first, it->second, false);
                it = o.joins.erase(it);
            } else {
                ++it;
            }
        }
    };
    if (edit.a == 0) {
        return;
    }
    switch (edit.kind) {
    case VersionEdit::Kind::Split: {
        drop_joins(edit.a);
        const auto it = std::lower_bound(o.splits.begin(), o.splits.end(), edit.a);
        if (it == o.splits.end() || *it != edit.a) {
            o.splits.insert(it, edit.a);
            note(edit.a, 0, true);
        }
        break;
    }
    case VersionEdit::Kind::Join: {
        if (edit.b == 0 || edit.a == edit.b) {
            return;
        }
        drop_split(edit.a);
        drop_split(edit.b);
        const std::pair<std::uint64_t, std::uint64_t> pair{std::min(edit.a, edit.b),
                                                           std::max(edit.a, edit.b)};
        const auto it = std::lower_bound(o.joins.begin(), o.joins.end(), pair);
        if (it == o.joins.end() || *it != pair) {
            o.joins.insert(it, pair);
            note(pair.first, pair.second, true);
        }
        break;
    }
    case VersionEdit::Kind::Clear:
        drop_split(edit.a);
        drop_joins(edit.a);
        break;
    }
}

namespace {

// Whether a folded word names a note: A to G, then b or # ("bb", "eb", "f#", "c"); the written
// sign ♭ and ♯ folds to those.
bool is_note_word(const std::string& w) {
    return !w.empty() && w.size() <= 2 && w[0] >= 'a' && w[0] <= 'g' &&
           (w.size() == 1 || w[1] == 'b' || w[1] == '#');
}

bool is_mode_word(const std::string& w) {
    return w == "major" || w == "minor" || w == "maj" || w == "min";
}

// The words of `title` that no other version's title has, as written ("D" in "Lantern Waltz in D"
// among "Lantern Waltz"); empty when there are none or more than two. A key's note and the mode
// word after it ("Bb major") are one word: both stay or both go, so the name never reads "major"
// alone.
std::string own_words(std::string_view title, const std::unordered_set<std::string>& shared) {
    static constexpr std::string_view kJoiners[] = {"in",  "of",   "by", "for",
                                                    "no",  "op",   "opus", "arr"};
    const std::vector<TitleToken> t = tokens_of(title);
    const std::vector<char> gone = level_tokens(t);
    std::string out;
    std::size_t count = 0;
    const auto add = [&](std::size_t from, std::size_t to) {
        out += out.empty() ? "" : " ";
        out.append(title.substr(t[from].begin, t[to].end - t[from].begin));
    };
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (gone[i] != 0) {
            continue;
        }
        if (i + 1 < t.size() && gone[i + 1] == 0 && is_note_word(t[i].word) &&
            is_mode_word(t[i + 1].word)) {
            if (shared.count(t[i].word) != 0 && shared.count(t[i + 1].word) != 0) {
                ++i;
                continue;
            }
            if (++count > 2) {
                return {};
            }
            add(i, i + 1);
            ++i;
            continue;
        }
        if (shared.count(t[i].word) != 0 || in_list(t[i].word, kStopWords) ||
            in_list(t[i].word, kJoiners) || all_digits(t[i].word)) {
            continue;
        }
        if (++count > 2) {
            return {};
        }
        add(i, i);
    }
    return out;
}

// The kind of file a song came from, as a chip says it; empty when its name has no extension.
std::string file_type_of(std::string_view file_name) {
    // "tunes.abc#X=3": the tune's number is not part of the name.
    file_name = file_name.substr(0, file_name.find('#'));
    const std::size_t dot = file_name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= file_name.size()) {
        return {};
    }
    std::string ext(file_name.substr(dot + 1));
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == "mid" || ext == "midi" || ext == "kar") {
        return "MIDI";
    }
    if (ext == "mxl" || ext == "musicxml" || ext == "xml") {
        return "MusicXML";
    }
    if (ext == "abc") {
        return "ABC";
    }
    if (ext == "mscz" || ext == "mscx") {
        return "MuseScore";
    }
    for (char& c : ext) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return ext;
}

// The folder a song is in, its own name only ("Songs/Lessons/Easy" is "Easy").
std::string last_folder(std::string_view folder) {
    const std::size_t slash = folder.find_last_of('/');
    return std::string(slash == std::string_view::npos ? folder : folder.substr(slash + 1));
}

// Gives members that came out with one name a name each. A name two share is told apart by, in
// order: the word one title has that the others' do not ("Medium D"), the kind of file when that
// differs ("Medium MusicXML"), the folder ("Medium Lessons"); those still alike are numbered, the
// number kept apart from any number the name already ends in.
void tell_apart(VersionGroup& g, std::span<const VersionSong> songs) {
    std::unordered_map<std::string, std::vector<std::size_t>> by_name;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        by_name[g.members[i].name].push_back(i);
    }
    const auto song_of = [&](std::size_t i) -> const VersionSong& {
        return songs[g.members[i].song];
    };
    for (auto& [base, at] : by_name) {
        std::vector<std::size_t> open = at;
        for (int tier = 0; tier < 3 && open.size() >= 2; ++tier) {
            std::vector<std::string> label(open.size());
            if (tier == 0) {
                // The words every one of them has are not what sets one apart.
                std::unordered_set<std::string> shared;
                for (std::size_t k = 0; k < open.size(); ++k) {
                    std::unordered_set<std::string> mine;
                    for (const TitleToken& t : tokens_of(song_of(open[k]).title)) {
                        mine.insert(t.word);
                    }
                    if (k == 0) {
                        shared = std::move(mine);
                    } else {
                        std::erase_if(shared, [&](const std::string& w) { return mine.count(w) == 0; });
                    }
                }
                for (std::size_t k = 0; k < open.size(); ++k) {
                    label[k] = own_words(song_of(open[k]).title, shared);
                }
            } else {
                for (std::size_t k = 0; k < open.size(); ++k) {
                    label[k] = tier == 1 ? file_type_of(song_of(open[k]).file_name)
                                         : last_folder(song_of(open[k]).folder);
                }
            }
            // A kind of label all of them have alike tells nothing apart.
            if (std::all_of(label.begin(), label.end(),
                            [&](const std::string& l) { return l == label[0]; })) {
                continue;
            }
            for (std::size_t k = 0; k < open.size(); ++k) {
                if (!label[k].empty()) {
                    g.members[open[k]].name += " " + label[k];
                }
            }
            // Those whose name is still another's go on to the next kind of label.
            std::unordered_map<std::string, int> count;
            for (const std::size_t i : open) {
                ++count[g.members[i].name];
            }
            std::erase_if(open, [&](std::size_t i) { return count[g.members[i].name] < 2; });
        }
        // The last resort: a number, the first of them keeping the bare name.
        std::unordered_map<std::string, int> used;
        for (const std::size_t i : open) {
            std::string& name = g.members[i].name;
            const int times = ++used[name];
            if (times > 1) {
                const bool ends_in_number =
                    !name.empty() && std::isdigit(static_cast<unsigned char>(name.back()));
                name += ends_in_number ? " (" + std::to_string(times) + ")"
                                       : " " + std::to_string(times);
            }
        }
    }
}

} // namespace

std::vector<VersionGroup> group_versions(std::span<const VersionSong> songs,
                                         const VersionLinks& links) {
    const std::size_t n = songs.size();
    std::vector<char> is_split(n, 0);
    for (const std::uint32_t i : links.splits) {
        if (i < n) {
            is_split[i] = 1;
        }
    }
    std::vector<Prep> prep;
    prep.reserve(n);
    for (const VersionSong& s : songs) {
        prep.push_back(prep_of(s));
    }
    // The pairs worth judging: songs of one title, or one number, and songs sharing melody.
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates;
    const auto add = [&](std::uint32_t a, std::uint32_t b) {
        if (a > b) {
            std::swap(a, b);
        }
        if (a != b && is_split[a] == 0 && is_split[b] == 0 &&
            seen.insert((static_cast<std::uint64_t>(a) << 32) | b).second) {
            candidates.emplace_back(a, b);
        }
    };
    // Songs that share a title or a number are judged pair by pair, unless more than kBucketMax
    // share it: a title that many songs carry ("Etude", a series) names no piece, so a file that
    // is short of melody cannot join one on the title alone. Their melodies are still compared
    // through the shared 4-grams below, and the same tune in two of them groups.
    const auto add_all = [&](const std::vector<std::uint32_t>& ids) {
        if (ids.size() > kBucketMax) {
            return;
        }
        for (std::size_t i = 0; i < ids.size(); ++i) {
            for (std::size_t j = i + 1; j < ids.size(); ++j) {
                add(ids[i], ids[j]);
            }
        }
    };
    {
        std::unordered_map<std::string, std::vector<std::uint32_t>> by_title;
        std::unordered_map<std::string, std::vector<std::uint32_t>> by_number;
        for (std::uint32_t i = 0; i < n; ++i) {
            if (!prep[i].key.empty()) {
                by_title[prep[i].key].push_back(i);
            }
            if (!prep[i].catalogue.empty() && !prep[i].composer.empty()) {
                by_number[prep[i].composer + '|' + prep[i].catalogue].push_back(i);
            }
        }
        for (const auto& [key, ids] : by_title) {
            add_all(ids);
        }
        for (const auto& [key, ids] : by_number) {
            add_all(ids);
        }
    }
    {
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> postings;
        std::vector<std::vector<std::uint32_t>> grams(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            if (prep[i].usable) {
                grams[i] = grams_of(songs[i].melody);
            }
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            for (const std::uint32_t g : grams[i]) {
                postings[g].push_back(i);
            }
        }
        std::unordered_map<std::uint32_t, std::uint32_t> shared;
        for (std::uint32_t i = 0; i < n; ++i) {
            shared.clear();
            for (const std::uint32_t g : grams[i]) {
                const std::vector<std::uint32_t>& list = postings[g];
                if (list.size() > kPostingMax) {
                    continue;
                }
                for (const std::uint32_t j : list) {
                    if (j > i) {
                        ++shared[j];
                    }
                }
            }
            for (const auto& [j, count] : shared) {
                // Some of the shorter melody's 4-grams, at least a couple.
                const std::size_t least = std::max<std::size_t>(
                    kSharedGrams, static_cast<std::size_t>(kSharedShare *
                                                           static_cast<double>(std::min(
                                                               grams[i].size(), grams[j].size()))));
                if (count >= least) {
                    add(i, j);
                }
            }
        }
    }
    // Judge them, strongest first, and join the songs; a group never takes two composers or more
    // than kMaxVersions songs.
    std::vector<Pair> pairs;
    Scratch scratch;
    for (const auto& [a, b] : candidates) {
        const bool comparable = prep[a].usable && prep[b].usable;
        const double sim = comparable ? similarity_of(prep[a].pitch, prep[b].pitch, scratch) : -1.0;
        const Verdict v = judge(prep[a], prep[b], sim);
        if (v.same) {
            pairs.push_back({a, b, v.strength, sim});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) {
        if (x.strength != y.strength) {
            return x.strength > y.strength;
        }
        if (x.sim != y.sim) {
            return x.sim > y.sim;
        }
        return x.a != y.a ? x.a < y.a : x.b < y.b;
    });
    std::vector<std::uint32_t> root(n);
    std::iota(root.begin(), root.end(), 0u);
    std::vector<std::vector<std::uint32_t>> held(n); // by root: its songs
    for (std::uint32_t i = 0; i < n; ++i) {
        held[i].push_back(i);
    }
    std::vector<std::string> composer(n);
    for (std::size_t i = 0; i < n; ++i) {
        composer[i] = prep[i].composer;
    }
    const auto find = [&](std::uint32_t x) {
        while (root[x] != x) {
            root[x] = root[root[x]];
            x = root[x];
        }
        return x;
    };
    // The player's joins first: a joined pair is one group whatever the files say (a split song is
    // out of it, and a group never passes kMaxVersions).
    for (const auto& [x, y] : links.joins) {
        if (x >= n || y >= n || x == y || is_split[x] != 0 || is_split[y] != 0) {
            continue;
        }
        const std::uint32_t rx = find(x);
        const std::uint32_t ry = find(y);
        if (rx == ry || held[rx].size() + held[ry].size() > kMaxVersions) {
            continue;
        }
        root[ry] = rx;
        held[rx].insert(held[rx].end(), held[ry].begin(), held[ry].end());
        held[ry].clear();
        if (composer[rx].empty()) {
            composer[rx] = composer[ry];
        }
    }
    std::vector<std::uint32_t> joined_root(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        joined_root[i] = find(i);
    }
    // Two songs of a group are the same piece, not only linked through a third: "A is B" and "B is
    // C" do not make "A is C" (an easy arrangement of a piece and a hard one of another that
    // share a middle one). The pairs judged above are remembered; any other is judged when asked.
    std::unordered_map<std::uint64_t, bool> asked;
    for (const auto& [a, b] : candidates) {
        asked.emplace((static_cast<std::uint64_t>(a) << 32) | b, false);
    }
    for (const Pair& p : pairs) {
        asked[(static_cast<std::uint64_t>(p.a) << 32) | p.b] = true;
    }
    const auto same = [&](std::uint32_t a, std::uint32_t b) {
        if (joined_root[a] == joined_root[b]) {
            return true; // joined by the player
        }
        if (a > b) {
            std::swap(a, b);
        }
        const auto [it, fresh] = asked.emplace((static_cast<std::uint64_t>(a) << 32) | b, false);
        if (fresh) {
            const bool comparable = prep[a].usable && prep[b].usable;
            const double sim =
                comparable ? similarity_of(prep[a].pitch, prep[b].pitch, scratch) : -1.0;
            it->second = judge(prep[a], prep[b], sim).same;
        }
        return it->second;
    };
    for (const Pair& p : pairs) {
        const std::uint32_t ra = find(p.a);
        const std::uint32_t rb = find(p.b);
        if (ra == rb || held[ra].size() + held[rb].size() > kMaxVersions) {
            continue;
        }
        if (!composer[ra].empty() && !composer[rb].empty() && composer[ra] != composer[rb]) {
            continue;
        }
        const bool all_same = std::all_of(held[ra].begin(), held[ra].end(), [&](std::uint32_t x) {
            return std::all_of(held[rb].begin(), held[rb].end(),
                               [&](std::uint32_t y) { return same(x, y); });
        });
        if (!all_same) {
            continue;
        }
        root[rb] = ra;
        held[ra].insert(held[ra].end(), held[rb].begin(), held[rb].end());
        held[rb].clear();
        if (composer[ra].empty()) {
            composer[ra] = composer[rb];
        }
    }
    // The groups, in the order of their first song, each ordered and named.
    std::unordered_map<std::uint32_t, std::size_t> slot;
    std::vector<std::vector<std::uint32_t>> members;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t r = find(i);
        if (held[r].size() < 2) {
            continue;
        }
        const auto [it, fresh] = slot.emplace(r, members.size());
        if (fresh) {
            members.emplace_back();
        }
        members[it->second].push_back(i);
    }
    std::vector<VersionGroup> out;
    out.reserve(members.size());
    for (std::vector<std::uint32_t>& ids : members) {
        std::stable_sort(ids.begin(), ids.end(), [&](std::uint32_t x, std::uint32_t y) {
            const int hx = songs[x].half_stars > 0 ? songs[x].half_stars : 99;
            const int hy = songs[y].half_stars > 0 ? songs[y].half_stars : 99;
            if (hx != hy) {
                return hx < hy;
            }
            const int lx = label_rank(prep[x].label);
            const int ly = label_rank(prep[y].label);
            if (lx != ly) {
                return lx < ly;
            }
            return songs[x].title < songs[y].title;
        });
        VersionGroup g;
        for (const std::uint32_t id : ids) {
            VersionMember m;
            m.song = id;
            m.labelled = prep[id].label != VersionLabel::None;
            if (m.labelled) {
                m.name = std::string(version_label_name(prep[id].label));
            } else if (songs[id].half_stars > 0) {
                m.name = std::string(level_name(level_of_half_stars(songs[id].half_stars)));
            } else {
                m.name = "Version";
            }
            g.members.push_back(std::move(m));
        }
        tell_apart(g, songs);
        out.push_back(std::move(g));
    }
    return out;
}

} // namespace etude
