// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/upload_api.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

struct Setup {
    TempDir root;
    UploadMs now = 1000;
    std::uint32_t counter = 0;
    UploadApi api{UploadApi::Options{root.path / "Songs", root.path / "Removed songs",
                                     [this] { return now; }, [this] { return counter += 1234567u; },
                                     nullptr, nullptr}};
    // The cookie header a browser would send after a right PIN.
    std::string sign_in() {
        const ApiReply r = api.login(api.pin());
        REQUIRE(r.status == 200);
        const std::size_t end = r.set_cookie.find(';');
        return r.set_cookie.substr(0, end);
    }
};

} // namespace

TEST_CASE("upload api: off by default, nobody is let in until it starts") {
    Setup t;
    CHECK(t.api.status() == UploadStatus::Off);
    CHECK_FALSE(t.api.authorize("tv_session=" + std::string(kTokenChars, 'a')));
    CHECK(t.api.remaining_ms() == 0);
    t.api.start();
    CHECK(t.api.status() == UploadStatus::Ready);
    CHECK(t.api.pin().size() == kPinDigits);
    CHECK(is_pin_text(t.api.pin()));
}

TEST_CASE("upload api: the PIN gives a session cookie that is HttpOnly and SameSite=Strict") {
    Setup t;
    t.api.start();
    const ApiReply wrong = t.api.login("abcd");
    CHECK(wrong.status == 403);
    CHECK(wrong.set_cookie.empty());
    const ApiReply ok = t.api.login(t.api.pin());
    CHECK(ok.status == 200);
    CHECK(ok.body == "{\"signed_in\":true}");
    CHECK(ok.set_cookie.rfind("tv_session=", 0) == 0);
    CHECK(ok.set_cookie.find("HttpOnly") != std::string::npos);
    CHECK(ok.set_cookie.find("SameSite=Strict") != std::string::npos);
    CHECK(ok.set_cookie.find("Max-Age=600") != std::string::npos);
    const std::string cookie = ok.set_cookie.substr(0, ok.set_cookie.find(';'));
    CHECK(t.api.authorize("other=1; " + cookie));
    CHECK(t.api.session(cookie).body == "{\"signed_in\":true,\"remaining_s\":600}");
    CHECK(t.api.session("").body == "{\"signed_in\":false}");
    const ApiReply out = t.api.logout(cookie);
    CHECK(out.set_cookie.find("Max-Age=0") != std::string::npos);
    CHECK_FALSE(t.api.authorize(cookie));
}

TEST_CASE("upload api: five wrong PINs lock it, and the answer says how long") {
    Setup t;
    t.api.start();
    for (int i = 0; i < kMaxPinFailures; ++i) {
        const std::string wrong = t.api.pin() == "0000" ? "1111" : "0000";
        CHECK(t.api.login(wrong).status == 403);
    }
    const ApiReply locked = t.api.login(t.api.pin());
    CHECK(locked.status == 429);
    CHECK(locked.body.find("60 seconds") != std::string::npos);
    t.now += kPinLockMs;
    CHECK(t.api.login(t.api.pin()).status == 200);
}

TEST_CASE("upload api: a session ends with the server, and a new start makes a new PIN") {
    Setup t;
    t.api.start();
    const std::string cookie = t.sign_in();
    const std::string first_pin = t.api.pin();
    t.api.stop();
    CHECK_FALSE(t.api.authorize(cookie));
    t.api.start();
    CHECK_FALSE(t.api.authorize(cookie));
    CHECK(t.api.pin() != first_pin);
}

TEST_CASE("upload api: ten idle minutes turn it off; use and uploads keep it on") {
    Setup t;
    t.api.start();
    const std::string cookie = t.sign_in();
    t.now += kServerIdleMs - 1;
    CHECK_FALSE(t.api.tick());
    CHECK(t.api.authorize(cookie)); // use restarts the ten minutes
    t.now += kServerIdleMs - 1;
    CHECK_FALSE(t.api.tick());
    // A request without a session is not use.
    CHECK_FALSE(t.api.authorize("tv_session=" + std::string(kTokenChars, 'f')));
    t.now += 1;
    CHECK(t.api.tick());
    CHECK(t.api.status() == UploadStatus::Off);
    CHECK_FALSE(t.api.authorize(cookie));
    CHECK(t.api.reason() == "Turned off after 10 minutes unused");

    t.api.start();
    t.api.transfer_begin();
    CHECK(t.api.status() == UploadStatus::Receiving);
    t.now += 3 * kServerIdleMs;
    CHECK_FALSE(t.api.tick()); // a long upload is use
    t.api.transfer_end();
    t.now += kServerIdleMs;
    CHECK(t.api.tick());
}

TEST_CASE("upload api: a failed start shows its reason") {
    Setup t;
    t.api.fail("The port 8080 is taken");
    CHECK(t.api.status() == UploadStatus::Error);
    CHECK(t.api.reason() == "The port 8080 is taken");
    t.api.start();
    CHECK(t.api.status() == UploadStatus::Ready);
}

TEST_CASE("upload api: the file list is JSON that file names cannot break") {
    Setup t;
    t.api.start();
    write_file(t.root.path / "Songs/Inbox/quote\"name.mid");
    write_file(t.root.path / "Songs/Inbox/<b>tag&amp.mid");
    write_file(t.root.path / "Songs/Classical/plain.mid", "12345");
    const ApiReply r = t.api.files();
    CHECK(r.status == 200);
    CHECK(r.body.find("\"path\":\"Classical/plain.mid\",\"size\":5,\"kind\":\"song\"") !=
          std::string::npos);
    CHECK(r.body.find("quote\\\"name.mid") != std::string::npos);
    CHECK(r.body.find("\\u003cb\\u003etag\\u0026amp.mid") != std::string::npos);
    CHECK(r.body.find('<') == std::string::npos); // no raw markup anywhere in the reply
    CHECK(r.body.find("\"folders\":[\"Classical\",\"Inbox\"]") != std::string::npos);
    CHECK(r.body.find("\"truncated\":false") != std::string::npos);
}

TEST_CASE("upload api: rename, move and delete reply with the new place or the reason") {
    Setup t;
    t.api.start();
    write_file(t.root.path / "Songs/Inbox/a.mid");
    ApiReply r = t.api.rename("Inbox/a.mid", "b.mid");
    CHECK(r.status == 200);
    CHECK(r.body == "{\"ok\":true,\"path\":\"Inbox/b.mid\"}");
    r = t.api.move("Inbox/b.mid", "Classical");
    CHECK(r.body == "{\"ok\":true,\"path\":\"Classical/b.mid\"}");
    r = t.api.remove("Classical/b.mid");
    CHECK(r.status == 200);
    CHECK(fs::exists(t.root.path / "Removed songs/Classical/b.mid"));
    r = t.api.remove("Classical/b.mid");
    CHECK(r.status == 404);
    CHECK(r.body == "{\"error\":\"That file is not there\"}");
    r = t.api.rename("../x.mid", "y.mid");
    CHECK(r.status == 400);
}

TEST_CASE("upload api: begin_upload and the replies after it") {
    Setup t;
    t.api.start();
    ServiceError e;
    CHECK(t.api.begin_upload("../x.mid", &e) == nullptr);
    CHECK(UploadApi::refused(e).status == 400);
    std::unique_ptr<UploadSink> sink = t.api.begin_upload("x.mid", &e);
    REQUIRE(sink);
    const std::string bytes = midi_bytes();
    REQUIRE(sink->write(bytes.data(), bytes.size()));
    REQUIRE(sink->finish());
    const ApiReply r = UploadApi::uploaded("x.mid", sink->saved_as());
    CHECK(r.body == "{\"ok\":true,\"name\":\"x.mid\",\"saved_as\":\"x.mid\"}");
    CHECK(UploadApi::uploaded("a\"b.mid", "a\"b.mid").body.find("a\\\"b.mid") != std::string::npos);
}

TEST_CASE("upload api: the session reply tells the page how long the server stays up") {
    Setup t;
    t.api.start();
    const std::string cookie = t.sign_in();
    CHECK(t.api.session("").body == "{\"signed_in\":false}");
    CHECK(t.api.session(cookie).body == "{\"signed_in\":true,\"remaining_s\":600}");
    // Asking is not use: the time keeps running while the page only reads it.
    t.now += 61'000;
    CHECK(t.api.session(cookie).body == "{\"signed_in\":true,\"remaining_s\":539}");
}

TEST_CASE("upload api: the progress of uploads is counted for the screen") {
    Setup t;
    t.api.start();
    CHECK_FALSE(t.api.progress().active);
    t.api.progress_begin("a.mid", 1000);
    t.api.progress_add(250);
    t.api.progress_add(250);
    UploadProgress p = t.api.progress();
    CHECK(p.active);
    CHECK(p.name == "a.mid");
    CHECK(p.received == 500);
    CHECK(p.total == 1000);
    t.api.progress_end(true);
    t.api.progress_begin("b.mid", 10);
    t.api.progress_end(false);
    t.api.progress_begin("c.mid", 10);
    t.api.progress_idle(); // a thrown error: not counted
    p = t.api.progress();
    CHECK_FALSE(p.active);
    CHECK(p.completed == 1);
    CHECK(p.failed == 1);
    // A new start is a new visit.
    t.api.start();
    CHECK(t.api.progress().completed == 0);
}

TEST_CASE("upload api: the file list carries a song's length in whole seconds, when known") {
    TempDir root;
    write_file(root.path / "Songs/a.mid", midi_bytes());
    write_file(root.path / "Songs/b.mid", midi_bytes());
    write_file(root.path / "Songs/c.pdf", "%PDF-1.4");
    UploadMs now = 1000;
    std::uint32_t counter = 0;
    UploadApi api{UploadApi::Options{
        root.path / "Songs", root.path / "Removed songs", [&] { return now; },
        [&] { return counter += 1234567u; }, nullptr,
        [](const fs::path& f) { return f.filename() == "a.mid" ? 192.6 : -1.0; }}};
    const std::string body = api.files().body;
    CHECK(body.find("{\"path\":\"a.mid\",\"size\":14,\"kind\":\"song\",\"length\":193}") !=
          std::string::npos);
    CHECK(body.find("{\"path\":\"b.mid\",\"size\":14,\"kind\":\"song\"}") != std::string::npos);
    CHECK(body.find("{\"path\":\"c.pdf\",\"size\":8,\"kind\":\"document\"}") != std::string::npos);
}
