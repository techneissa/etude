// SPDX-License-Identifier: GPL-3.0-or-later
// What the library screen needs of the song library: the catalogue it shows (the songs, the search,
// the order) and the actions on a song. The frame loop binds it (app/library_binding.*), so the
// screen never sees the files, the worker or the history database.
#pragma once

#include "../catalogue_service.hpp"
#include "../library_catalogue.hpp"
#include "../song_library.hpp"

#include "etude/library_groups.hpp"
#include "etude/profile.hpp"
#include "etude/progress.hpp"
#include "etude/progress_view.hpp"
#include "etude/song_opening.hpp"
#include "etude/song_practice.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app::ui {

class LibraryPort {
  public:
    virtual ~LibraryPort() = default;

    // The songs, the search text and the order the screen shows.
    virtual LibraryCatalogue& catalogue() = 0;
    // The song that is open, as a catalogue index; none when no song is open or it is not one of
    // the library's.
    virtual std::optional<std::size_t> current() const = 0;
    // Today, as a local day (days since 1970), for the Recent filter and "played today".
    virtual std::int32_t today() const = 0;

    // Opens song `i` (the frame loop opens it after this frame and closes the library).
    virtual void open(std::size_t i) = 0;
    // Opens song `i` with its loop on one of its sections, `start_s` to `end_s` (`section_id` the
    // section selected), and the playhead at the loop's start.
    virtual void open_at_section(std::size_t i, double start_s, double end_s, int section_id) = 0;
    // What the history holds of the player's practice choices for song `i` (its hands, speed,
    // count-in, ...); the defaults for a song never practised or not read yet. The reference
    // holds until the next history change.
    virtual const SongPractice& practice(std::size_t i) const = 0;
    // The player chose song `i` as the version of its piece to open (a version chip): kept by its
    // content hash, so the card of the piece opens it from now on. Opening a version does the same.
    virtual void choose_version(std::size_t i) = 0;
    // The player's corrections of the grouping (#213), kept by content hash. `split_version`: song
    // `i` is not a version of the piece it was grouped with, and stays out of it; `join_versions`:
    // `i` is the same piece as `with` (and the other versions of `with`'s piece); `reset_versions`:
    // the files decide again. `version_said` is what the player has said about `i`.
    // A join stores nothing for a song not read yet (NotRead, either of the two) and none for a
    // piece that already holds the most versions (Full).
    enum class VersionSaid : std::uint8_t { Auto, Split, Joined };
    enum class JoinOutcome : std::uint8_t { Joined, Full, NotRead };
    virtual void split_version(std::size_t i) = 0;
    virtual JoinOutcome join_versions(std::size_t i, std::size_t with) = 0;
    virtual void reset_versions(std::size_t i) = 0;
    virtual VersionSaid version_said(std::size_t i) const = 0;
    // The player's settings for song `i`, kept by its content hash. A song not read yet has none
    // and these do nothing.
    virtual void set_favourite(std::size_t i, bool on) = 0;
    // An empty name or composer goes back to the file's own.
    virtual void rename(std::size_t i, std::string_view title) = 0;
    virtual void set_composer(std::size_t i, std::string_view composer) = 0;
    // The best run of each way song `i` was played (mode, hands, speed), best first, at most
    // `max`. Worked out when asked (once, as the details open).
    virtual std::vector<BestEntry> bests(std::size_t i, std::size_t max) const = 0;
    // The history is still being read: the scores and "played" are not complete yet.
    virtual bool history_loading() const = 0;

    // Many songs at once (a selection). Songs not read yet are passed over.
    virtual void set_favourites(std::span<const std::size_t> songs, bool on) = 0;

    // The player's groups ("My groups"), kept in the history by the songs' content hashes.
    virtual const LibraryGroups& groups() const = 0;
    // A new group, last; its id.
    virtual std::uint64_t create_group(std::string_view name) = 0;
    virtual void rename_group(std::uint64_t id, std::string_view name) = 0;
    // The group goes; its songs stay in the library.
    virtual void remove_group(std::uint64_t id) = 0;
    // One place earlier (-1) or later (1) in the groups' order.
    virtual void move_group(std::uint64_t id, int step) = 0;
    // Puts the songs in the group, or takes them out.
    virtual void set_in_group(std::uint64_t id, std::span<const std::size_t> songs, bool in) = 0;

    // Files. Only songs in the player's Songs folder move: not the songs that come with a
    // developer's build, not the song that is open, and an ABC tune only with the other tunes of
    // its file. Why not, in a few words; empty when it can.
    virtual std::string_view why_not_movable(std::size_t i) const = 0;
    // The folders of the Songs folder, '/' separated, in name order ("" is the Songs folder).
    virtual std::vector<std::string> folders() const = 0;
    // Moves the songs' files into `folder` (made when missing) of the Songs folder; Delete moves
    // them out of the library into "Removed songs" in the settings folder, to be taken back from
    // there. Done off the frame thread: the list changes a few frames later. Songs that cannot
    // move are passed over.
    virtual void move_songs(std::span<const std::size_t> songs, std::string_view folder) = 0;
    virtual void remove_songs(std::span<const std::size_t> songs) = 0;
    // A move or a delete is still under way.
    virtual bool files_busy() const = 0;
    // What a finished move or delete did, for the toast, once each.
    virtual std::optional<std::string> take_files_notice() = 0;

    // The opening bars of song `i`, read off the frame thread when asked: empty until they are in
    // (or when the song could not be read: `preview_failed`). Asking for another song drops the
    // last one.
    virtual void request_preview(std::size_t i) = 0;
    virtual void stop_preview() = 0;
    virtual const SongOpening* preview() const = 0;
    virtual bool preview_failed() const = 0;
    // The preview's sound: a key pressed (velocity > 0) or let go (0) as a note of the opening
    // lands or ends, played on the piano the player hears, while the sound is on.
    virtual void preview_note(std::uint8_t key, std::uint8_t velocity) = 0;
    // The keyboard the preview falls onto: the player's, as the main view shows it, else (when
    // the player lets each song choose) the preview song's own range in whole octaves, worked out
    // once when it is read. The opening's own keys until then.
    virtual KeyRange preview_keys() const = 0;
    // The whole song the preview read, for its sheet music; null until it is in.
    virtual std::shared_ptr<const LoadedSong> preview_song() const = 0;

    // ---- The Progress page (#348). The numbers are built on the history thread from every
    // profile's runs; the page asks when it opens and takes the answer a few frames later.
    // `goal_minutes` is the daily goal of the profile in use (0: none).
    virtual void request_progress(int goal_minutes) = 0;
    // The numbers last built, answers that came in since taken (null before the first), for the
    // profile that was in use when asked: a page of another profile stays until the new answer replaces it.
    virtual std::shared_ptr<const ProgressView> progress() = 0;
    // The last request got no numbers: the history could not serve it (it is closed, another etude
    // has it, the database is unusable). Cleared by the next request.
    virtual bool progress_failed() const = 0;
    virtual const ProfileSet& profiles() const = 0;
    virtual std::int64_t active_profile() const = 0;
    // Opens the song with this content hash, resumed from where it was (false: not in the library).
    virtual bool open_song_hash(std::uint64_t hash) = 0;
    // Opens the song with this content hash looping bars `first` to `last` (Song bar numbers, a
    // pickup bar is 0): the bars become a practice item as "Practise these bars" makes one.
    virtual bool practise_bars(std::uint64_t hash, int first, int last) = 0;

    // ---- Get songs: the free catalogues, searched with the library's search text. Nothing is
    // fetched while the player has them offline (AppSettings::get_songs_online); what was cached
    // is still searched.
    // The catalogues' songs as last read (null before any), and the hits for the search text,
    // best first, indexing it.
    virtual const CatalogueSnapshot* catalogue_songs() const = 0;
    virtual std::span<const CatalogueHit> catalogue_hits() const = 0;
    // Changes whenever the hits (or the snapshot they index) changed.
    virtual std::uint64_t catalogue_hits_serial() const = 0;
    // The text the hits are for (the search runs a moment after the typing stops).
    virtual std::string_view catalogue_hits_query() const = 0;
    virtual const CatalogueIndexState& catalogue_state(CatalogueId id) const = 0;
    // Fetches every catalogue's index again now.
    virtual void refresh_catalogues() = 0;
    // An entry's file: fetched for its preview, kept by Download (into the catalogue's group),
    // cancelled, and why it failed.
    virtual CatalogueFile catalogue_file(std::uint32_t entry) const = 0;
    virtual std::string_view catalogue_file_reason(std::uint32_t entry) const = 0;
    virtual void download(std::uint32_t entry) = 0;
    virtual void cancel_download(std::uint32_t entry) = 0;
    // The library's song an entry was downloaded as, while the library still lists it.
    virtual std::optional<std::size_t> downloaded_song(std::uint32_t entry) const = 0;
    // The preview of an entry: its file is fetched, then read as a song's is (preview()); a file
    // that cannot be fetched fails it (`preview_failed`, and catalogue_file_reason says why).
    virtual void request_catalogue_preview(std::uint32_t entry) = 0;
    // Where song `i` came from, when it was downloaded from a catalogue; null otherwise.
    virtual const SongOrigin* origin(std::size_t i) const = 0;
};

} // namespace etude::app::ui
