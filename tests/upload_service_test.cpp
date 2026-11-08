// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/upload_service.hpp"
#include "upload_test_support.hpp"

#include "etude/upload_rules.hpp"

#include <doctest/doctest.h>

#include <chrono>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

struct Setup {
    TempDir root;
    fs::path songs = root.path / "data/Songs";
    fs::path removed = root.path / "data/Removed songs";
    fs::path outside = root.path / "outside";
    UploadService service{songs, removed};
    Setup() {
        fs::create_directories(songs);
        fs::create_directories(outside);
    }
};

// Stores `bytes` as `name` the way the server does: in two chunks.
std::string upload(UploadService& s, const std::string& name, const std::string& bytes,
                   ServiceError* error = nullptr) {
    ServiceError local;
    std::unique_ptr<UploadSink> sink = s.begin_upload(name, error != nullptr ? error : &local);
    if (!sink) {
        return {};
    }
    const std::size_t half = bytes.size() / 2;
    if (!sink->write(bytes.data(), half) ||
        !sink->write(bytes.data() + half, bytes.size() - half) || !sink->finish()) {
        if (error != nullptr) {
            *error = sink->error();
        }
        return {};
    }
    return sink->saved_as();
}

} // namespace

TEST_CASE("upload: a file lands in Songs/Inbox whole, and nothing is left behind") {
    Setup t;
    const std::string bytes = midi_bytes("some notes");
    CHECK(upload(t.service, "Fur Elise.mid", bytes) == "Fur Elise.mid");
    CHECK(read_file(t.songs / "Inbox/Fur Elise.mid") == bytes);
    CHECK(tree(t.songs) == "Inbox/\nInbox/.incoming/\nInbox/Fur Elise.mid\n");
}

TEST_CASE("upload: a name that is taken gets a number, never replaces") {
    Setup t;
    CHECK(upload(t.service, "a.mid", midi_bytes("1")) == "a.mid");
    CHECK(upload(t.service, "a.mid", midi_bytes("2")) == "a (2).mid");
    CHECK(upload(t.service, "a.mid", midi_bytes("3")) == "a (3).mid");
    CHECK(read_file(t.songs / "Inbox/a.mid") == midi_bytes("1"));
    CHECK(read_file(t.songs / "Inbox/a (3).mid") == midi_bytes("3"));
}

TEST_CASE("upload: the name is cleaned, and the file type and its bytes must fit") {
    Setup t;
    ServiceError e;
    CHECK(upload(t.service, "Why? (take 2).mid", midi_bytes(), &e) == "Why_ (take 2).mid");
    CHECK(upload(t.service, "virus.exe", "MZ....", &e).empty());
    CHECK(e.status == 415);
    CHECK(upload(t.service, "page.html", "<html>", &e).empty());
    CHECK(e.status == 415);
    CHECK(upload(t.service, "song.mid", "<html><script>", &e).empty()); // not MIDI inside
    CHECK(e.status == 415);
    CHECK(upload(t.service, "empty.mid", "", &e).empty());
    CHECK(e.status == 415);
    CHECK(upload(t.service, "scan.pdf", "%PDF-1.4 ....", &e) == "scan.pdf");
    CHECK(upload(t.service, "photo.jpg", std::string("\xFF\xD8\xFF\xE0....", 8), &e) ==
          "photo.jpg");
    CHECK(upload(t.service, "tune.abc", "X:1\nK:C\nCDEF|", &e) == "tune.abc");
    // Only what passed is there, and no temporary file.
    CHECK(tree(t.songs) == "Inbox/\nInbox/.incoming/\nInbox/Why_ (take 2).mid\nInbox/photo.jpg\n"
                           "Inbox/scan.pdf\nInbox/tune.abc\n");
}

TEST_CASE("upload: a wrong first chunk stops the upload early") {
    Setup t;
    ServiceError e;
    std::unique_ptr<UploadSink> sink = t.service.begin_upload("a.mid", &e);
    REQUIRE(sink);
    const std::string text(100, 'x');
    CHECK_FALSE(sink->write(text.data(), text.size()));
    CHECK(sink->error().status == 415);
    CHECK_FALSE(sink->write(text.data(), 1)); // over: further calls do nothing
    CHECK_FALSE(sink->finish());
    sink.reset();
    CHECK(tree(t.songs) == "Inbox/\nInbox/.incoming/\n");
}

TEST_CASE("upload: the size limit is exact") {
    Setup t;
    const std::string head = midi_bytes();
    const std::string block(1024 * 1024, 'x');
    ServiceError e;
    {
        std::unique_ptr<UploadSink> sink = t.service.begin_upload("limit.mid", &e);
        REQUIRE(sink);
        REQUIRE(sink->write(head.data(), head.size()));
        std::uint64_t left = kMaxUploadBytes - head.size();
        while (left > 0) {
            const std::size_t n =
                static_cast<std::size_t>(std::min<std::uint64_t>(left, block.size()));
            REQUIRE(sink->write(block.data(), n));
            left -= n;
        }
        CHECK(sink->bytes() == kMaxUploadBytes);
        CHECK(sink->finish()); // exactly at the limit is fine
    }
    {
        std::unique_ptr<UploadSink> sink = t.service.begin_upload("over.mid", &e);
        REQUIRE(sink);
        REQUIRE(sink->write(head.data(), head.size()));
        std::uint64_t left = kMaxUploadBytes - head.size();
        while (left > 0) {
            const std::size_t n =
                static_cast<std::size_t>(std::min<std::uint64_t>(left, block.size()));
            REQUIRE(sink->write(block.data(), n));
            left -= n;
        }
        CHECK_FALSE(sink->write("x", 1)); // one byte over
        CHECK(sink->error().status == 413);
        CHECK_FALSE(sink->finish());
    }
    CHECK(tree(t.songs) == "Inbox/\nInbox/.incoming/\nInbox/limit.mid\n");
}

TEST_CASE("upload: hostile names create nothing, in the Songs folder or outside it") {
    Setup t;
    const std::string before = tree(t.root.path);
    const std::string nul_name("a\0b.mid", 7);
    const std::string long_name = std::string(5000, 'a') + ".mid";
    const std::string absolute = (t.outside / "evil.mid").string();
    for (const std::string& name :
         {std::string("../evil.mid"), std::string("../../evil.mid"), std::string("..\\evil.mid"),
          absolute, std::string("/tmp/evil.mid"), std::string("Inbox/../evil.mid"),
          std::string("a/b.mid"), std::string("."), std::string(".."), std::string(".htaccess"),
          std::string(""), std::string("   "), nul_name, long_name,
          std::string("evil\xE2\x88\x95"
                      "x.mid"), // U+2215 division slash
          std::string("\xEF\xBC\x8E\xEF\xBC\x8E\xEF\xBC\x8F"
                      "evil.mid"), // ．．／
          std::string("a\xE2\x80\xAE"
                      "dim.mid"), // right-to-left override
          std::string("evil.mid\x7f"), std::string("bad\xFF.mid")}) {
        ServiceError e;
        INFO("name of " << name.size() << " bytes");
        CHECK(t.service.begin_upload(name, &e) == nullptr);
        CHECK(e.status >= 400);
        CHECK_FALSE(e.message.empty());
    }
    // Not one of them made a file or a folder anywhere.
    CHECK(tree(t.outside).empty());
    CHECK(tree(t.root.path) == before);
}

TEST_CASE("upload: an Inbox that is a link out of the Songs folder is refused") {
    Setup t;
    std::error_code ec;
    fs::create_directory_symlink(t.outside, t.songs / "Inbox", ec);
    if (ec) {
        return; // no links on this file system
    }
    ServiceError e;
    CHECK(t.service.begin_upload("a.mid", &e) == nullptr);
    CHECK(e.status == 500);
    CHECK(tree(t.outside).empty());
}

TEST_CASE("upload: partial files older than an hour are removed at start") {
    Setup t;
    const fs::path stale = t.songs / "Inbox/.incoming/.stale.part";
    const fs::path fresh = t.songs / "Inbox/.incoming/.fresh.part";
    write_file(stale);
    write_file(fresh);
    fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(2));
    UploadService again(t.songs, t.removed);
    CHECK_FALSE(fs::exists(stale));
    CHECK(fs::exists(fresh));
}

TEST_CASE("library list: allowed files and folders only, no hidden or linked ones") {
    Setup t;
    write_file(t.songs / "a.mid");
    write_file(t.songs / "Classical/Bach/prelude.mxl", "12345");
    write_file(t.songs / "Inbox/scan.pdf");
    write_file(t.songs / "Inbox/photo.heic");
    write_file(t.songs / "notes.txt");
    write_file(t.songs / ".hidden/x.mid");
    write_file(t.songs / "._a.mid");
    write_file(t.songs / "Inbox/.incoming/.t.part");
    write_file(t.outside / "secret.mid");
    std::error_code ec;
    fs::create_directory_symlink(t.outside, t.songs / "link", ec);
    const Listing l = t.service.list();
    std::vector<std::string> paths;
    for (const ListedFile& f : l.files) {
        paths.push_back(f.path + ":" + f.kind);
    }
    CHECK(paths == std::vector<std::string>{"Classical/Bach/prelude.mxl:song",
                                            "Inbox/photo.heic:image", "Inbox/scan.pdf:document",
                                            "a.mid:song"});
    CHECK(l.files[0].size == 5);
    CHECK(l.folders == std::vector<std::string>{"Classical", "Classical/Bach", "Inbox"});
    CHECK_FALSE(l.truncated);
}

TEST_CASE("library rename: inside its folder, to an allowed name, never over another file") {
    Setup t;
    write_file(t.songs / "Classical/old.mid", "OLD");
    write_file(t.songs / "Classical/taken.mid", "TAKEN");
    ServiceResult r = t.service.rename("Classical/old.mid", "new name.mid");
    REQUIRE(r.ok);
    CHECK(r.path == "Classical/new name.mid");
    CHECK(read_file(t.songs / "Classical/new name.mid") == "OLD");
    CHECK_FALSE(fs::exists(t.songs / "Classical/old.mid"));

    r = t.service.rename("Classical/new name.mid", "taken.mid");
    CHECK_FALSE(r.ok);
    CHECK(r.error.status == 409);
    CHECK(read_file(t.songs / "Classical/taken.mid") == "TAKEN");

    for (const char* hostile : {"../x.mid", "x/y.mid", "..", "x.exe", "x.mid.exe", "",
                                ".hidden.mid", "x\xE2\x88\x95y.mid"}) {
        INFO(hostile);
        CHECK_FALSE(t.service.rename("Classical/new name.mid", hostile).ok);
    }
    CHECK(fs::exists(t.songs / "Classical/new name.mid"));
}

TEST_CASE("library paths: traversal, absolute paths and other types are refused for every action") {
    Setup t;
    write_file(t.songs / "a.mid");
    write_file(t.outside / "secret.mid", "SECRET");
    write_file(t.songs / "doc.txt", "TXT");
    for (const std::string& path :
         {std::string("../outside/secret.mid"), (t.outside / "secret.mid").string(),
          std::string("/etc/passwd"), std::string("a.mid/../../outside/secret.mid"),
          std::string("./a.mid"), std::string("Inbox/../a.mid"), std::string("..\\a.mid"),
          std::string("doc.txt"), std::string(""), std::string("a.mid/"), std::string(1500, 'a'),
          std::string("a\0.mid", 6), std::string("\xEF\xBC\x8E\xEF\xBC\x8E/a.mid")}) {
        INFO("path of " << path.size() << " bytes");
        CHECK_FALSE(t.service.rename(path, "z.mid").ok);
        CHECK_FALSE(t.service.move(path, "Other").ok);
        CHECK_FALSE(t.service.remove(path).ok);
    }
    CHECK(read_file(t.outside / "secret.mid") == "SECRET");
    CHECK(fs::exists(t.songs / "a.mid"));
    CHECK(read_file(t.songs / "doc.txt") == "TXT");
    CHECK_FALSE(fs::exists(t.removed));
}

TEST_CASE("library paths: a link inside the Songs folder cannot reach a file outside it") {
    Setup t;
    write_file(t.outside / "secret.mid", "SECRET");
    std::error_code ec;
    fs::create_directory_symlink(t.outside, t.songs / "out", ec);
    if (ec) {
        return;
    }
    fs::create_symlink(t.outside / "secret.mid", t.songs / "file.mid", ec);
    // Through the linked folder, and as a linked file.
    CHECK(t.service.rename("out/secret.mid", "z.mid").error.status == 403);
    CHECK_FALSE(t.service.remove("out/secret.mid").ok);
    CHECK_FALSE(t.service.move("out/secret.mid", "Mine").ok);
    CHECK_FALSE(t.service.remove("file.mid").ok);
    CHECK_FALSE(t.service.rename("file.mid", "z.mid").ok);
    // A destination that is a link out is refused too.
    write_file(t.songs / "a.mid");
    CHECK(t.service.move("a.mid", "out").error.status == 403);
    CHECK(read_file(t.outside / "secret.mid") == "SECRET");
    CHECK(tree(t.outside) == "secret.mid\n");
}

TEST_CASE("library move: into a folder, made when missing; a name taken gets a number") {
    Setup t;
    write_file(t.songs / "Inbox/a.mid", "NEW");
    write_file(t.songs / "Classical/a.mid", "OLD");
    ServiceResult r = t.service.move("Inbox/a.mid", "Classical");
    REQUIRE(r.ok);
    CHECK(r.path == "Classical/a (2).mid");
    CHECK(read_file(t.songs / "Classical/a.mid") == "OLD");
    CHECK(read_file(t.songs / "Classical/a (2).mid") == "NEW");

    write_file(t.songs / "Inbox/b.mid");
    r = t.service.move("Inbox/b.mid", "Jazz/Bebop");
    REQUIRE(r.ok);
    CHECK(r.path == "Jazz/Bebop/b.mid");
    r = t.service.move("Jazz/Bebop/b.mid", ""); // the top folder
    REQUIRE(r.ok);
    CHECK(r.path == "b.mid");

    for (const char* hostile :
         {"../x", "/tmp", "a/../../x", ".hidden", "Inbox/.incoming", "a//b", "x\xE2\x88\x95y"}) {
        INFO(hostile);
        CHECK_FALSE(t.service.move("b.mid", hostile).ok);
    }
    CHECK(fs::exists(t.songs / "b.mid"));
    CHECK_FALSE(fs::exists(t.songs / "Inbox/.incoming/b.mid"));
}

TEST_CASE("library delete: the file goes to Removed songs in the folders it had, never erased") {
    Setup t;
    write_file(t.songs / "Classical/Bach/p.mid", "BACH");
    write_file(t.songs / "top.mid", "TOP");
    ServiceResult r = t.service.remove("Classical/Bach/p.mid");
    REQUIRE(r.ok);
    CHECK_FALSE(fs::exists(t.songs / "Classical/Bach/p.mid"));
    CHECK(read_file(t.removed / "Classical/Bach/p.mid") == "BACH");
    REQUIRE(t.service.remove("top.mid").ok);
    CHECK(read_file(t.removed / "top.mid") == "TOP");
    CHECK(t.service.remove("top.mid").error.status == 404); // already gone
    UploadService none(t.songs, {});
    write_file(t.songs / "c.mid");
    CHECK_FALSE(none.remove("c.mid").ok);
    CHECK(fs::exists(t.songs / "c.mid"));
}

TEST_CASE("library actions: the song the piano has open stays where it is") {
    Setup t;
    write_file(t.songs / "Classical/open.mid", "OPEN");
    write_file(t.songs / "tunes.abc", "X:1\nK:C\nCDEF|");
    write_file(t.songs / "Classical/other.mid", "OTHER");
    std::string open = (t.songs / "Classical/open.mid").string();
    UploadService service(t.songs, t.removed, [&open] { return open; });
    for (int round = 0; round < 2; ++round) {
        const std::string path = round == 0 ? "Classical/open.mid" : "tunes.abc";
        if (round == 1) {
            open = (t.songs / "tunes.abc").string() + "#X=3"; // an ABC tune of the file
        }
        INFO(path);
        const ServiceResult rename = service.rename(path, "new.mid");
        CHECK_FALSE(rename.ok);
        CHECK(rename.error.status == 409);
        CHECK(service.move(path, "Elsewhere").error.status == 409);
        CHECK(service.remove(path).error.status == 409);
    }
    CHECK(read_file(t.songs / "Classical/open.mid") == "OPEN");
    CHECK(fs::exists(t.songs / "tunes.abc"));
    CHECK_FALSE(fs::exists(t.removed));
    // Another song is free to move, and so is the open one once it is closed.
    CHECK(service.rename("Classical/other.mid", "other2.mid").ok);
    open.clear();
    CHECK(service.remove("Classical/open.mid").ok);
    // A song that is gone, or named in a way that matches no file, protects nothing.
    open = (t.songs / "missing.mid").string();
    CHECK(service.rename("tunes.abc", "tunes2.abc").ok);
}

TEST_CASE("upload service: a song's length is in the listing when the app knows it, and no file is "
          "read for it") {
    Setup t;
    write_file(t.songs / "a.mid", midi_bytes());
    write_file(t.songs / "Sub/b.mxl", "<?xml?>");
    write_file(t.songs / "scan.pdf", "%PDF-1.4");
    std::vector<std::string> asked;
    UploadService service(t.songs, t.removed, nullptr, [&](const fs::path& file) {
        asked.push_back(file.filename().string());
        return file.filename() == "a.mid" ? 192.4 : -1.0;
    });
    const Listing listing = service.list();
    REQUIRE(listing.files.size() == 3);
    for (const ListedFile& f : listing.files) {
        INFO(f.path);
        if (f.path == "a.mid") {
            CHECK(f.length_s == doctest::Approx(192.4));
        } else {
            CHECK(f.length_s < 0.0); // unknown to the app, or not a song at all
        }
    }
    // Only songs are asked about: a PDF has no length to look up.
    CHECK(std::count(asked.begin(), asked.end(), "scan.pdf") == 0);
    CHECK(std::count(asked.begin(), asked.end(), "a.mid") == 1);
}
