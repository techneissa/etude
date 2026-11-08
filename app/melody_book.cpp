// SPDX-License-Identifier: GPL-3.0-or-later
#include "melody_book.hpp"

#include "song_library.hpp"

#include <utility>

namespace etude::app {

MelodyFileFn embedded_melody_files() {
    return [](std::string_view name) {
        const std::span<const unsigned char> bytes = embedded_melody_file(name);
        return std::span<const std::uint8_t>(bytes.data(), bytes.size());
    };
}

void MelodyBook::set_best(const std::unordered_map<std::uint64_t, SongPlays>& plays) {
    for (MelodyPiece& p : pieces) {
        const auto it = plays.find(p.hash);
        p.best_permille = it == plays.end() ? -1 : it->second.best_permille;
    }
}

MelodyBook load_melody_book(std::string_view manifest_text, const MelodyFileFn& files) {
    MelodyBook book;
    const MelodyManifest manifest = parse_melody_manifest(manifest_text);
    book.problems = manifest.errors;
    if (!manifest.ok()) {
        return book;
    }
    // The pieces of each path in order, each file read once; a file that does not load is left out.
    for (const MelodyPathEntry& entry : manifest.paths) {
        MelodyPath path;
        path.id = entry.id;
        path.title = entry.title;
        path.icon = entry.icon;
        for (std::size_t i = 0; i < entry.files.size(); ++i) {
            const MelodyFileEntry& file = manifest.files[entry.files[i]];
            const std::span<const std::uint8_t> bytes = files(file.file);
            const SongFileLoad load = load_song_bytes(bytes, file.file);
            if (bytes.empty() || !load.ok) {
                book.problems.push_back(file.file + " does not load");
                continue;
            }
            MelodyPiece p;
            p.file = file.file;
            p.title = file.title;
            p.composer = file.composer;
            p.tag = file.version;
            p.goal = entry.goals[i];
            p.step = file.step;
            p.hash = load.song.hash;
            p.half_stars = load.entry.half_stars;
            p.shape = melody_shape(load.song.song);
            path.pieces.push_back(book.pieces.size());
            book.pieces.push_back(std::move(p));
        }
        if (!path.pieces.empty()) {
            book.paths.push_back(std::move(path));
        }
    }
    return book;
}

MelodyBook load_melody_book() {
    const std::span<const unsigned char> text = embedded_melody_manifest();
    return load_melody_book({reinterpret_cast<const char*>(text.data()), text.size()},
                            embedded_melody_files());
}

std::optional<OpenedMelody> open_melody(const MelodyPiece& piece, const MelodyFileFn& files) {
    const std::span<const std::uint8_t> bytes = files(piece.file);
    SongFileLoad load = load_song_bytes(bytes, piece.file);
    if (bytes.empty() || !load.ok) {
        return std::nullopt;
    }
    // A tagged piece opens under its tag as well, so the player's title says which version is playing.
    const std::string title = piece.tag.empty() ? piece.title : piece.title + " (" + piece.tag + ")";
    load.song.title = title;
    load.song.display_path = title;
    return OpenedMelody{std::move(load.song), piece.composer};
}

} // namespace etude::app
