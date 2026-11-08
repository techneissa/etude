// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_job.hpp"

#include "song_library.hpp"

#include "etude/abc.hpp"
#include "etude/content_hash.hpp"

#include <SDL3/SDL.h>

#include <filesystem>

namespace etude::app {

SheetSource make_sheet_source(const LoadedSong& song) {
    SheetSource out;
    if (song.format == SongFileFormat::Midi) {
        return sheet_source_for_midi(song.song);
    }
    if (song.absolute_path.empty()) {
        out.reason = "the song's file is not known";
        return out;
    }
    const SongPath where = split_song_path(song.absolute_path);
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_song_bytes(where.path, bytes, &error)) {
        out.reason = "the song's file could not be read again: " + error;
        return out;
    }
    AbcOptions abc;
    if (song.format == SongFileFormat::Abc) {
        abc.tune = song.tune.empty() ? where.tune : song.tune;
        abc.chords_as_left_hand = song.abc_chords;
        bool same = false;
        for (const AbcTune& t : list_abc_tunes(bytes)) {
            if ((abc.tune.empty() || t.address == abc.tune) && t.hash == song.hash) {
                same = true;
                break;
            }
            if (abc.tune.empty()) {
                break;
            }
        }
        if (!same) {
            out.reason = "the song's file changed since it was opened";
            return out;
        }
    } else if (content_hash(bytes.data(), bytes.size()) != song.hash) {
        out.reason = "the song's file changed since it was opened";
        return out;
    }
    return sheet_source_for_score(bytes, abc);
}

std::string sheet_resource_dir() {
    // Beside the app in a build, in <prefix>/share/etude where the Pi setup installs it.
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        return "verovio-data";
    }
    const std::filesystem::path dir(base);
    const std::filesystem::path beside = dir / "verovio-data";
    std::error_code ec;
    if (std::filesystem::is_directory(beside, ec)) {
        return beside.string();
    }
    return (dir / ".." / "share" / "etude" / "verovio-data").string();
}

} // namespace etude::app
