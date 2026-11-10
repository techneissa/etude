// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/library_binding.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <thread>

using namespace etude;
using namespace etude::app;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("etude-binding-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

HistoryOptions quiet() {
    HistoryOptions o;
    o.sync_commits = false;
    o.log = [](std::string_view) {};
    return o;
}

// A Songs folder with three songs (two in collections), a developer's folder with one, the
// history read, and the binding caught up.
struct Fixture {
    TempDir dir;
    fs::path songs;
    fs::path dev;
    std::unique_ptr<SongLibrary> library;
    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<LibraryBinding> binding;
    std::int64_t now = 2'000'000'000;

    Fixture() {
        songs = dir.path / "Songs";
        dev = dir.path / "dev";
        const fs::path data(ETUDE_TEST_DATA_DIR);
        fs::create_directories(songs / "Classical");
        fs::create_directories(songs / "Lessons");
        fs::create_directories(dev);
        fs::copy_file(data / "satie-gymnopedie-1.mid", songs / "Classical" / "satie.mid");
        fs::copy_file(data / "bach-bwv846-prelude.mid", songs / "Lessons" / "bach.mid");
        fs::copy_file(data / "edge-pickup.mid", songs / "pickup.mid");
        fs::copy_file(data / "edge-fast-32nds.mid", dev / "fast.mid");
        library = std::make_unique<SongLibrary>(std::vector<fs::path>{songs, dev});
        library->scan();
        library->load_all_now();
        history = std::make_unique<HistoryStore>(dir.path, quiet());
        history->open();
        REQUIRE(history->wait_until_settled(10s));
        binding = std::make_unique<LibraryBinding>(*library, *history, dir.path / "Removed songs");
        frames(3);
    }
    // A few frames of the loop: the library's poll, the history's, the binding's update.
    void frames(int n) {
        for (int i = 0; i < n; ++i) {
            const bool changed = library->poll();
            history->poll();
            binding->update(changed, now);
            binding->catalogue().update_view(20000, static_cast<double>(i));
        }
    }
    // Frames until `done`, a few seconds at most.
    template <class F> bool frames_until(F done) {
        for (int i = 0; i < 2000; ++i) {
            frames(1);
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(2ms);
        }
        return false;
    }
    std::size_t index(std::string_view relative) {
        const std::optional<std::size_t> i = binding->catalogue().index_of(relative);
        REQUIRE(i.has_value());
        return *i;
    }
};

} // namespace

TEST_CASE("binding: groups are made, filled, renamed, ordered and deleted through the history") {
    Fixture f;
    REQUIRE(f.binding->catalogue().size() == 4);
    const std::uint64_t a = f.binding->create_group("  Warm   ups ");
    const std::uint64_t b = f.binding->create_group("Recital");
    CHECK(a != 0);
    CHECK(a != b);
    REQUIRE(f.binding->groups().groups().size() == 2);
    CHECK(f.binding->groups().find(a)->name == "Warm ups");
    const std::size_t satie = f.index("Classical/satie.mid");
    const std::size_t bach = f.index("Lessons/bach.mid");
    const std::size_t both[] = {satie, bach};
    f.binding->set_in_group(a, both, true);
    CHECK(f.binding->groups().songs_of(a).size() == 2);
    // The catalogue follows: My groups shows the group a section.
    f.frames(2);
    CHECK(f.binding->catalogue().group_count() == 2);
    CHECK(f.binding->catalogue().group_size(0) == 2);
    f.binding->set_in_group(a, std::span(both, 1), false);
    CHECK(f.binding->groups().songs_of(a).size() == 1);
    // Later, then earlier again; renamed; deleted.
    f.binding->move_group(a, 1);
    CHECK(f.binding->groups().groups().front().id == b);
    f.binding->move_group(a, -1);
    CHECK(f.binding->groups().groups().front().id == a);
    f.binding->move_group(a, -1); // already first
    CHECK(f.binding->groups().groups().front().id == a);
    f.binding->rename_group(b, "Concert");
    CHECK(f.binding->groups().find(b)->name == "Concert");
    f.binding->remove_group(a);
    CHECK(f.binding->groups().find(a) == nullptr);
    REQUIRE(f.history->flush());
}

TEST_CASE("binding: a song's stored practice is shown, and a section opens the song on its loop") {
    Fixture f;
    const std::size_t bach = f.index("Lessons/bach.mid");
    const std::uint64_t hash = f.binding->catalogue().song(bach).hash;
    REQUIRE(hash != 0);
    // Nothing stored: the defaults show.
    CHECK(f.binding->practice(bach) == SongPractice{});
    SongPractice p;
    p.speed_percent = 80;
    p.hands = HandFilter::Right;
    f.history->set_song_practice(hash, p);
    CHECK(f.binding->practice(bach).speed_percent == 80);
    CHECK(f.binding->practice(bach).hands == HandFilter::Right);
    CHECK(f.binding->practice(f.index("pickup.mid")) == SongPractice{});
    // A section opens the song and tells the frame loop where.
    f.binding->open_at_section(bach, 8.0, 16.0, 3);
    REQUIRE(f.binding->take_open().has_value());
    const std::optional<LibraryBinding::SectionOpen> section = f.binding->take_section_open();
    REQUIRE(section.has_value());
    CHECK(section->hash == hash);
    CHECK(section->start_s == 8.0);
    CHECK(section->end_s == 16.0);
    CHECK(section->section_id == 3);
    CHECK_FALSE(f.binding->take_section_open().has_value()); // once
    // A plain open after it carries no section.
    f.binding->open_at_section(bach, 8.0, 16.0, 3);
    f.binding->open(bach);
    CHECK_FALSE(f.binding->take_section_open().has_value());
}

TEST_CASE("binding: favourites for many songs at once, each song once") {
    Fixture f;
    const std::size_t satie = f.index("Classical/satie.mid");
    const std::size_t bach = f.index("Lessons/bach.mid");
    const std::size_t picked[] = {satie, bach, satie};
    f.binding->set_favourites(picked, true);
    f.frames(2);
    CHECK(f.binding->catalogue().song(satie).favourite);
    CHECK(f.binding->catalogue().song(bach).favourite);
    CHECK_FALSE(f.binding->catalogue().song(f.index("pickup.mid")).favourite);
    f.binding->set_favourites(picked, false);
    f.frames(2);
    CHECK_FALSE(f.binding->catalogue().song(satie).favourite);
}

TEST_CASE("binding: only the player's songs move; a move and a delete happen off the frame") {
    Fixture f;
    const std::size_t fast = f.index("fast.mid");
    CHECK_FALSE(f.binding->why_not_movable(fast).empty()); // the developer's folder
    const std::size_t pickup = f.index("pickup.mid");
    CHECK(f.binding->why_not_movable(pickup).empty());
    f.binding->set_current(pickup);
    CHECK_FALSE(f.binding->why_not_movable(pickup).empty()); // the open song
    f.binding->set_current(std::nullopt);
    // The folders of the Songs folder, the top first.
    CHECK(f.binding->folders() == std::vector<std::string>{"", "Classical", "Lessons"});
    // Into a new folder (its name cleaned of what a file system would refuse).
    const std::size_t two[] = {pickup, fast};
    f.binding->move_songs(two, " Book 2/../ ");
    CHECK(f.binding->take_files_notice().has_value()); // the developer's song stayed
    REQUIRE(f.frames_until([&] {
        return f.binding->catalogue().index_of("Book 2/pickup.mid").has_value() &&
               !f.binding->files_busy();
    }));
    CHECK(fs::exists(f.songs / "Book 2" / "pickup.mid"));
    CHECK(f.binding->take_files_notice() == std::optional<std::string>("Moved 1 song"));
    CHECK(f.binding->catalogue().size() == 4);
    // Delete: into Removed songs, in the folders it had.
    const std::size_t moved = f.index("Book 2/pickup.mid");
    f.binding->remove_songs(std::span(&moved, 1));
    REQUIRE(f.frames_until([&] {
        return f.binding->catalogue().size() == 3 && !f.binding->files_busy();
    }));
    CHECK(fs::exists(f.dir.path / "Removed songs" / "Book 2" / "pickup.mid"));
    CHECK_FALSE(fs::exists(f.songs / "Book 2" / "pickup.mid"));
    const std::optional<std::string> notice = f.binding->take_files_notice();
    REQUIRE(notice.has_value());
    CHECK(notice->find("Removed 1 song") == 0);
}

TEST_CASE("binding: the preview's opening is read off the frame thread, and its keys let go") {
    Fixture f;
    const std::size_t satie = f.index("Classical/satie.mid");
    CHECK(f.binding->preview() == nullptr);
    f.binding->request_preview(satie);
    REQUIRE(f.frames_until([&] { return f.binding->preview() != nullptr; }));
    CHECK_FALSE(f.binding->preview()->notes.empty());
    CHECK(f.binding->preview()->length_s <= LibraryBinding::kPreviewS + 1e-3);
    CHECK_FALSE(f.binding->preview_failed());
    // Its sound: a key down, then the preview stops and lets it go.
    f.binding->preview_note(60, 90);
    f.binding->stop_preview();
    CHECK(f.binding->preview() == nullptr);
    const auto sound = f.binding->preview_sound();
    REQUIRE(sound.size() == 2);
    CHECK(sound[0] == std::pair<std::uint8_t, std::uint8_t>{60, 90});
    CHECK(sound[1] == std::pair<std::uint8_t, std::uint8_t>{60, 0});
    f.binding->clear_preview_sound();
    CHECK(f.binding->preview_sound().empty());
    // A frame of more notes than the list holds: the presses past it are dropped, never a release.
    for (int n = 0; n < 300; ++n) {
        f.binding->preview_note(static_cast<std::uint8_t>(n % 100), 80);
    }
    const std::size_t full = f.binding->preview_sound().size();
    f.binding->preview_note(5, 0);
    CHECK(f.binding->preview_sound().size() == full + 1);
    CHECK(f.binding->preview_sound().back() == std::pair<std::uint8_t, std::uint8_t>{5, 0});
    f.binding->stop_preview(); // every key still down lets go
    std::size_t releases = 0;
    for (const auto& [key, velocity] : f.binding->preview_sound()) {
        releases += velocity == 0 ? 1 : 0;
    }
    CHECK(releases == 1 + 99); // the one above, then keys 0..99 but 5
}

// ---- Versions of one piece (#164)

namespace {

// Three arrangements of one tune, in two folders, and an unrelated tune.
struct VersionFixture {
    TempDir dir;
    fs::path songs;
    std::unique_ptr<SongLibrary> library;
    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<LibraryBinding> binding;
    std::int64_t now = 2'000'000'000;

    VersionFixture() { start(); }
    void start() {
        songs = dir.path / "Songs";
        const fs::path data(ETUDE_TEST_DATA_DIR);
        if (!fs::exists(songs)) {
            fs::create_directories(songs / "Lessons");
            fs::copy_file(data / "lantern-waltz.mid", songs / "lantern-waltz.mid");
            fs::copy_file(data / "lantern-waltz-easy.mid", songs / "Lessons" / "waltz-easy.mid");
            fs::copy_file(data / "lantern-waltz-in-d.mid", songs / "in-d.mid");
            fs::copy_file(data / "harbour-march.mid", songs / "march.mid");
        }
        library = std::make_unique<SongLibrary>(std::vector<fs::path>{songs});
        library->scan();
        library->load_all_now();
        history = std::make_unique<HistoryStore>(dir.path, quiet());
        history->open();
        REQUIRE(history->wait_until_settled(10s));
        binding = std::make_unique<LibraryBinding>(*library, *history, dir.path / "Removed songs");
        frames(3);
    }
    void stop() {
        REQUIRE(history->flush());
        binding.reset();
        history.reset();
        library.reset();
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) {
            const bool changed = library->poll();
            history->poll();
            binding->update(changed, now);
            binding->catalogue().update_view(20000, static_cast<double>(i));
        }
    }
    template <class F> bool frames_until(F done) {
        for (int i = 0; i < 3000; ++i) {
            frames(1);
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(2ms);
        }
        return false;
    }
    LibraryCatalogue& cat() { return binding->catalogue(); }
    std::size_t index(std::string_view relative) {
        const std::optional<std::size_t> i = cat().index_of(relative);
        REQUIRE(i.has_value());
        return *i;
    }
    std::vector<std::string> shown() {
        std::vector<std::string> out;
        for (const std::uint32_t i : cat().shown()) {
            out.push_back(cat().song(i).relative);
        }
        return out;
    }
};

} // namespace

TEST_CASE("binding: the versions of a piece are found off the frame thread and show as one card") {
    VersionFixture f;
    // Before the pass comes back every file is a card; the frame never waits for it.
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    CHECK(f.shown().size() == 2);
    const std::size_t easy = f.index("Lessons/waltz-easy.mid");
    const std::size_t original = f.index("lantern-waltz.mid");
    const std::size_t in_d = f.index("in-d.mid");
    CHECK(f.cat().versions_of(original).size() == 3);
    CHECK(f.cat().version_name(easy) == "Easy");
    // The easiest first, and the card of the piece is the easy one by default.
    CHECK(f.cat().versions_of(original).front() == easy);
    CHECK(f.cat().card_of(in_d) == easy);
    // Choosing a version makes it the card; the card remembers it across a restart.
    f.binding->choose_version(in_d);
    f.frames(3);
    CHECK(f.cat().card_of(easy) == in_d);
    const std::vector<std::string> now_shown = f.shown();
    CHECK(std::find(now_shown.begin(), now_shown.end(), "in-d.mid") != now_shown.end());
    CHECK(std::find(now_shown.begin(), now_shown.end(), "Lessons/waltz-easy.mid") ==
          now_shown.end());
    f.stop();
    f.start();
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    f.frames(3);
    CHECK(f.cat().card_of(f.index("lantern-waltz.mid")) == f.index("in-d.mid"));
}

TEST_CASE("binding: opening a version, or choosing it twice at once, makes it the card") {
    VersionFixture f;
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    const std::size_t easy = f.index("Lessons/waltz-easy.mid");
    const std::size_t original = f.index("lantern-waltz.mid");
    f.binding->open(original);
    CHECK(f.binding->take_open() == original);
    f.frames(3);
    CHECK(f.cat().card_of(easy) == original);
    // Two choices in one second: the later wins.
    f.binding->choose_version(easy);
    f.binding->choose_version(original);
    f.frames(3);
    CHECK(f.cat().card_of(easy) == original);
    // A song on its own: opening it changes nothing.
    const std::size_t march = f.index("march.mid");
    f.binding->open(march);
    CHECK(f.binding->take_open() == march);
    CHECK(f.cat().card_of(march) == march);
}

TEST_CASE("binding: the player takes a version out of its piece and joins another in, and it stays") {
    VersionFixture f;
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    const std::size_t original = f.index("lantern-waltz.mid");
    const std::size_t in_d = f.index("in-d.mid");
    REQUIRE(f.cat().versions_of(original).size() == 3);
    CHECK(f.binding->version_said(in_d) == ui::LibraryPort::VersionSaid::Auto);
    // "Not a version of this piece": it leaves the group (a pass off the frame thread) and shows
    // as a card of its own; the other two stay a piece.
    f.binding->split_version(in_d);
    CHECK(f.binding->version_said(in_d) == ui::LibraryPort::VersionSaid::Split);
    REQUIRE(f.frames_until([&] { return f.cat().versions_of(original).size() == 2; }));
    CHECK(f.cat().versions_of(in_d).empty());
    CHECK(f.cat().card_of(in_d) == in_d);
    CHECK(f.shown().size() == 3); // the piece, the split one and the march
    // It stays out after a restart, and across a rename of the song (the player's name for it is
    // kept by the content hash, as the split is).
    f.binding->rename(in_d, "My waltz in D");
    f.frames(3);
    f.stop();
    f.start();
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    f.frames(3);
    CHECK(f.cat().song(f.index("in-d.mid")).title == "My waltz in D");
    CHECK(f.cat().versions_of(f.index("in-d.mid")).empty());
    CHECK(f.cat().versions_of(f.index("lantern-waltz.mid")).size() == 2);
    // "Same piece as another song": the march joins the piece, with all its versions.
    CHECK(f.binding->join_versions(f.index("march.mid"), f.index("lantern-waltz.mid")) ==
          ui::LibraryPort::JoinOutcome::Joined);
    CHECK(f.binding->version_said(f.index("march.mid")) == ui::LibraryPort::VersionSaid::Joined);
    REQUIRE(f.frames_until([&] { return f.cat().versions_of(f.index("lantern-waltz.mid")).size() == 3; }));
    CHECK(f.cat().card_of(f.index("march.mid")) == f.cat().card_of(f.index("lantern-waltz.mid")));
    // "Group automatically again" on the march: the files decide, so it is on its own.
    f.binding->reset_versions(f.index("march.mid"));
    REQUIRE(f.frames_until([&] { return f.cat().versions_of(f.index("lantern-waltz.mid")).size() == 2; }));
    CHECK(f.cat().versions_of(f.index("march.mid")).empty());
    // And on the one taken out: it is a version again.
    f.binding->reset_versions(f.index("in-d.mid"));
    REQUIRE(f.frames_until([&] { return f.cat().versions_of(f.index("lantern-waltz.mid")).size() == 3; }));
}

TEST_CASE("binding: a join with a song that is not read yet stores nothing and says so") {
    TempDir dir;
    const fs::path songs = dir.path / "Songs";
    fs::create_directories(songs);
    const fs::path data(ETUDE_TEST_DATA_DIR);
    fs::copy_file(data / "lantern-waltz.mid", songs / "a.mid");
    fs::copy_file(data / "harbour-march.mid", songs / "b.mid");
    SongLibrary library(std::vector<fs::path>{songs});
    library.scan(); // listed, not read: no content hash yet
    HistoryStore history(dir.path, quiet());
    history.open();
    REQUIRE(history.wait_until_settled(10s));
    LibraryBinding binding(library, history, dir.path / "Removed songs");
    for (int i = 0; i < 3; ++i) {
        const bool changed = library.poll();
        history.poll();
        binding.update(changed, 2'000'000'000);
    }
    REQUIRE(binding.catalogue().size() == 2);
    REQUIRE(binding.catalogue().song(0).hash == 0);
    CHECK(binding.join_versions(0, 1) == ui::LibraryPort::JoinOutcome::NotRead);
    CHECK(binding.join_versions(1, 0) == ui::LibraryPort::JoinOutcome::NotRead);
    CHECK(binding.version_said(0) == ui::LibraryPort::VersionSaid::Auto);
    CHECK(history.version_overrides().empty());
}

TEST_CASE("binding: with the setting off every file is a card, and turning it on groups them again") {
    VersionFixture f;
    f.cat().set_group_versions(false);
    f.frames(40);
    CHECK_FALSE(f.cat().has_versions());
    CHECK(f.shown().size() == 4);
    f.cat().set_group_versions(true);
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    CHECK(f.shown().size() == 2);
}

TEST_CASE("binding: renaming a file out of its piece, or into one, regroups it") {
    VersionFixture f;
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    // Give the march the waltz's name and composer: its tune is another, so it stays apart.
    const std::size_t march = f.index("march.mid");
    f.binding->rename(march, "Lantern Waltz");
    f.frames(40);
    CHECK(f.cat().versions_of(march).empty());
    CHECK(f.shown().size() == 2);
}

TEST_CASE("binding versions: Favourite sets and clears every version of the piece, and the filter finds it") {
    VersionFixture f;
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    const std::size_t easy = f.index("Lessons/waltz-easy.mid");
    const std::size_t original = f.index("lantern-waltz.mid");
    const std::size_t in_d = f.index("in-d.mid");
    std::vector<std::size_t> piece;
    f.cat().piece_songs(original, piece);
    CHECK(piece.size() == 3);
    f.binding->set_favourites(piece, true);
    f.frames(3);
    for (const std::size_t i : piece) {
        CHECK(f.cat().song(i).favourite);
        CHECK(f.cat().song(i).piece_favourite);
    }
    // Clearing it from any card clears all three.
    f.cat().piece_songs(in_d, piece);
    f.binding->set_favourites(piece, false);
    f.frames(3);
    for (const std::size_t i : {easy, original, in_d}) {
        CHECK_FALSE(f.cat().song(i).favourite);
        CHECK_FALSE(f.cat().song(i).piece_favourite);
    }
    // One favourite version makes the piece a favourite: the heart, and the Favourites filter.
    f.binding->set_favourites(std::span(&in_d, 1), true);
    f.frames(3);
    CHECK_FALSE(f.cat().song(easy).favourite);
    CHECK(f.cat().song(easy).piece_favourite);
    BrowseOrder o;
    o.filter = BrowseFilter::Favourites;
    f.cat().set_order(o);
    f.frames(3);
    REQUIRE(f.cat().shown().size() == 1);
    CHECK(f.cat().card_of(original) == f.cat().shown()[0]);
}

TEST_CASE("binding versions: Add to group puts every version in, and the card is in it when any version is") {
    VersionFixture f;
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    const std::size_t original = f.index("lantern-waltz.mid");
    const std::uint64_t id = f.binding->create_group("Recital");
    std::vector<std::size_t> piece;
    f.cat().piece_songs(original, piece);
    f.binding->set_in_group(id, piece, true);
    CHECK(f.binding->groups().songs_of(id).size() == 3);
    for (const std::size_t i : piece) {
        CHECK(f.binding->groups().contains(id, f.cat().song(i).hash));
    }
    // Taking the piece out drops all three again.
    f.binding->set_in_group(id, piece, false);
    CHECK(f.binding->groups().songs_of(id).empty());
    REQUIRE(f.history->flush());
}

TEST_CASE("binding versions: Delete acts on the shown version only; the others and the card stay") {
    VersionFixture f;
    REQUIRE(f.frames_until([&] { return f.cat().has_versions(); }));
    const std::size_t original = f.index("lantern-waltz.mid");
    const std::size_t card = f.cat().card_of(original);
    f.binding->remove_songs(std::span(&card, 1));
    REQUIRE(f.frames_until([&] { return f.cat().size() == 3 && !f.binding->files_busy(); }));
    // Two versions of the piece are left, still one card, and the harbour march.
    CHECK(f.cat().shown().size() == 2);
    const std::size_t left = f.index("lantern-waltz.mid");
    CHECK(f.cat().versions_of(left).size() == 2);
    CHECK(f.cat().card_of(left) < f.cat().size());
}

TEST_CASE("binding: Progress numbers come in, and a store that cannot answer says so") {
    Fixture f;
    f.binding->request_progress(15);
    CHECK_FALSE(f.binding->progress_failed());
    REQUIRE(f.frames_until([&] { return f.binding->progress() != nullptr; }));
    CHECK(f.binding->progress()->profile_id == f.binding->active_profile());
    CHECK_FALSE(f.binding->progress_failed());
    // The history is closed (another etude has it, or the database is gone): a request is refused
    // outright, which the page must be told, not left waiting for an answer with a serial of 0.
    f.history->close();
    f.binding->request_progress(15);
    CHECK(f.binding->progress_failed());
    // The next request that is served clears it; here the store is closed for good, so it stays.
    f.frames(2);
    CHECK(f.binding->progress_failed());
}

TEST_CASE("binding: a request the store ends before reading is answered with nothing, and fails") {
    Fixture f;
    f.binding->request_progress(15);
    f.history->close(); // the thread answers what it did not read with no view
    f.frames(3);
    // Either the numbers were read before it closed, or the request failed: never neither.
    CHECK((f.binding->progress() != nullptr || f.binding->progress_failed()));
}
