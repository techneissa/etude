// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_library.hpp"

#include "song_library_internal.hpp"

#include "hand_split_cache.hpp"

#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <utility>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

// Loads a song. `listed` is the ABC tune to load when the caller has it already (the file's
// `listed_count` tunes are not listed again); null lists the bytes.
SongFileLoad load_song_impl(std::span<const std::uint8_t> bytes, std::string_view file_name,
                            const SongLoadOptions& options, const AbcTune* listed,
                            std::size_t listed_count) {
    SongFileLoad out;
    const SongFileFormat format =
        listed != nullptr ? SongFileFormat::Abc : sniff_song_format(bytes);
    out.format = format;
    const bool score = format == SongFileFormat::Xml || format == SongFileFormat::Zip;
    std::string score_title;
    std::string composer;
    std::uint64_t hash =
        format == SongFileFormat::Abc ? 0 : content_hash(bytes.data(), bytes.size());
    AbcTune tune; // ABC: the tune that was opened
    bool in_collection = false;
    Song song;
    if (format == SongFileFormat::Abc) {
        std::vector<AbcTune> tunes;
        const AbcTune* found = listed;
        if (found == nullptr) {
            tunes = list_abc_tunes(bytes);
            for (const AbcTune& t : tunes) {
                if (options.tune.empty() || t.address == options.tune) {
                    found = &t;
                    break;
                }
            }
        }
        if (found == nullptr) {
            out.error = options.tune.empty() ? "no ABC tune in the file"
                                             : "the file has no tune " + options.tune;
            return out;
        }
        AbcOptions abc;
        abc.chords_as_left_hand = options.abc_chords;
        abc.listed = found;
        ScoreLoadResult r = load_abc(bytes, abc);
        if (!r.ok) {
            out.error = std::string(to_string(r.error));
            if (!r.message.empty()) {
                out.error += ": " + r.message;
            }
            return out;
        }
        tune = *found;
        tune.title = r.title; // the tune's own T: line, wherever it is
        composer = std::move(r.composer);
        in_collection = (listed != nullptr ? listed_count : tunes.size()) > 1;
        hash = tune.hash;
        out.warnings = std::move(r.warnings);
        song = std::move(r.song);
    } else if (score) {
        ScoreLoadResult r = load_score(bytes);
        if (!r.ok) {
            out.error = std::string(to_string(r.error));
            if (!r.message.empty()) {
                out.error += ": " + r.message;
            }
            return out;
        }
        score_title = std::move(r.title);
        composer = std::move(r.composer);
        out.warnings = std::move(r.warnings);
        song = std::move(r.song);
    } else {
        MidiLoadResult r = load_midi(bytes);
        if (!r.ok) {
            if (r.error == MidiFileError::NotMidi) {
                out.error = "not a song file";
            } else if (r.error == MidiFileError::NoNotes || r.error == MidiFileError::TooLarge) {
                out.error = r.message; // names the problem: no notes, or more than the cap
            } else {
                out.error = to_string(r.error);
            }
            return out;
        }
        song = std::move(r.song);
    }
    // The library entry (hand counts, difficulty) is made from the split song. A song whose hands
    // the split decides keeps the hands the file gave, to split again by the player's choice
    // (hand_split_for, on a copy of the song that gets them back).
    if (needs_hand_split(song)) {
        if (hand_split_decides(song)) {
            out.song.file_hands = std::make_shared<const HandSplit>(capture_hands(song));
            out.song.hands_from_staves = etude::hands_from_staves(song);
        }
        split_hands_cached(song, hash, format == SongFileFormat::Abc && options.abc_chords);
        out.song.hands_split = true;
    }
    // Rated once here, on the loader's thread: the entry takes the song's stars and the open song
    // keeps the bars' ratings for the band and the trouble-bar picker.
    auto rated = std::make_shared<const SongDifficulty>(rate_song(song));
    if (format == SongFileFormat::Abc) {
        out.entry = make_abc_entry(song, hash, tune.title, file_name, tune.address, in_collection,
                                   composer, rated.get());
    } else {
        out.entry =
            score ? make_song_entry(song, bytes, file_name, score_title, composer, rated.get())
                  : make_song_entry(song, bytes, file_name, rated.get());
    }
    out.song.difficulty = std::move(rated);
    out.song.song = std::move(song);
    out.song.title = out.entry.title;
    out.song.hash = hash;
    out.song.display_path = std::string(file_name);
    out.song.format = format;
    out.song.tune = format == SongFileFormat::Abc ? tune.address : std::string();
    out.song.abc_chords = format == SongFileFormat::Abc && options.abc_chords;
    out.ok = true;
    return out;
}
// Appends bytes [begin, end) of the stream to `out`.
bool append_range(std::ifstream& in, std::size_t begin, std::size_t end,
                  std::vector<std::uint8_t>& out) {
    if (end < begin) {
        return false;
    }
    const std::size_t n = end - begin;
    const std::size_t at = out.size();
    out.resize(at + n);
    in.clear();
    in.seekg(static_cast<std::streamoff>(begin));
    in.read(reinterpret_cast<char*>(out.data() + at), static_cast<std::streamsize>(n));
    return in.gcount() == static_cast<std::streamsize>(n);
}
// The tune the library listed, read from the file without reading the rest of it: the file header
// and the tune's text, laid end to end. False when the file changed since it was listed or cannot
// be read. The size and time can stay the same under an edit, so the bytes read are hashed and
// compared with the hash of the listing: a tune whose text moved or changed is not taken for the
// one that was listed.
bool read_known_abc(const fs::path& path, const AbcKnown& known, std::vector<std::uint8_t>& out,
                    AbcTune& tune) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec || size != known.file_size) {
        return false;
    }
    const auto time = fs::last_write_time(path, ec);
    if (ec || static_cast<std::int64_t>(time.time_since_epoch().count()) != known.file_mtime) {
        return false;
    }
    const AbcTune& t = known.tune;
    if (t.header_end > t.begin || t.begin > t.end || t.end > size) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in || !append_range(in, 0, t.header_end, out) || !append_range(in, t.begin, t.end, out)) {
        return false;
    }
    if (content_hash(out.data(), out.size()) != t.hash) {
        return false;
    }
    tune = t;
    tune.header_end = t.header_end;
    tune.begin = t.header_end;
    tune.end = t.header_end + (t.end - t.begin);
    return true;
}

} // namespace

std::string absolute_path_text(const fs::path& path) {
    std::error_code ec;
    const fs::path abs = fs::absolute(path, ec);
    return ec ? std::string() : generic(abs.lexically_normal());
}

bool is_song_file_name(const fs::path& path) {
    static constexpr std::string_view kExtensions[] = {".mid",  ".midi",     ".kar", ".smf",
                                                       ".rmi",  ".musicxml", ".xml", ".mxl",
                                                       ".mscz", ".mscx",     ".abc"};
    const std::string ext = lower(path.extension().string());
    return std::find(std::begin(kExtensions), std::end(kExtensions), ext) != std::end(kExtensions);
}

bool read_song_bytes(const fs::path& path, std::vector<std::uint8_t>& out, std::string* error) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        if (error != nullptr) {
            *error = fs::exists(path, ec) ? "not a file" : "file not found";
        }
        return false;
    }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec || size > kMaxSongFileBytes) {
        if (error != nullptr) {
            *error = ec ? "unreadable" : "too large for a song";
        }
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) {
            *error = "unreadable";
        }
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        if (error != nullptr) {
            *error = "unreadable";
        }
        return false;
    }
    return true;
}


std::shared_ptr<const HandSplit> hand_split_for(const Song& unsplit, std::uint64_t hash,
                                                bool abc_chords, const HandSplitOptions& options) {
    const HandSplitCache::Key key = HandSplitCache::key_of(hash, abc_chords, options);
    const std::uint64_t fingerprint = song_fingerprint(unsplit);
    if (const auto known = hand_split_cache().find(key);
        known != nullptr && known->fingerprint == fingerprint &&
        known->note_hands.size() == unsplit.notes.size()) {
        return known;
    }
    auto split = std::make_shared<const HandSplit>(compute_hand_split(unsplit, options));
    hand_split_cache().put(key, split);
    return split;
}

SongFileLoad load_song_bytes(std::span<const std::uint8_t> bytes, std::string_view file_name,
                             const SongLoadOptions& options) {
    return load_song_impl(bytes, file_name, options, nullptr, 0);
}

SongPath split_song_path(std::string_view text) {
    SongPath out;
    out.path = fs::path(std::string(text));
    const AbcLocation where = split_abc_location(text);
    if (where.tune.empty()) {
        return out;
    }
    std::error_code ec;
    if (fs::is_regular_file(out.path, ec)) {
        return out; // a file really named so
    }
    out.path = fs::path(std::string(where.file));
    out.tune = std::string(where.tune);
    return out;
}

SongFileLoad load_song_file(const fs::path& path, const SongLoadOptions& options) {
    SongPath named{path, options.tune};
    if (options.tune.empty()) {
        named = split_song_path(path.generic_string());
    }
    const std::string name = named.path.filename().string();
    SongLoadOptions use = options;
    use.tune = named.tune;
    std::vector<std::uint8_t> bytes;
    if (options.known && (named.tune.empty() ? options.known->first : named.tune == options.known->tune.address)) {
        AbcTune tune;
        if (read_known_abc(named.path, *options.known, bytes, tune)) {
            SongFileLoad out = load_song_impl(bytes, name, use, &tune, options.known->tune_count);
            if (!out.ok) {
                out.error = "Can't open " + name + ": " + out.error;
            }
            return out;
        }
        bytes.clear();
    }
    std::string error;
    if (!read_song_bytes(named.path, bytes, &error)) {
        SongFileLoad out;
        out.error = "Can't open " + name + ": " + error;
        return out;
    }
    SongFileLoad out = load_song_impl(bytes, name, use, nullptr, 0);
    if (!out.ok) {
        out.error = "Can't open " + name + ": " + out.error;
    }
    return out;
}

SongLibrary::SongLibrary(std::vector<fs::path> roots) : roots_(std::move(roots)) {
    for (const fs::path& root : roots_) {
        std::error_code ec;
        fs::path key = fs::weakly_canonical(root, ec);
        root_keys_.push_back(ec ? root.lexically_normal() : key);
    }
}

SongLibrary::~SongLibrary() {
    stop_worker();
    stop_lister();
}

} // namespace etude::app
