// SPDX-License-Identifier: GPL-3.0-or-later
// Phone upload: off until started, PIN and lockout, LAN only, hostile names create nothing, a file is
// what its name says, the open song stays. The last cases drive the server over a loopback socket:
// Host, Origin, Content-Length, chunked bodies and multipart forms are read by the HTTP library, which
// only a socket reaches.
#include "../app/upload_api.hpp"
#include "../app/upload_controller.hpp"
#include "../app/upload_page.hpp"
#include "../app/upload_server.hpp"
#include "upload_test_support.hpp"

#include "etude/upload_auth.hpp"
#include "etude/upload_network.hpp"
#include "etude/upload_rules.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

struct Setup {
    TempDir root;
    UploadMs now = 1000;
    std::uint32_t counter = 0;
    fs::path songs = root.path / "Songs";
    fs::path removed = root.path / "Removed songs";
    fs::path outside = root.path / "outside";
    UploadApi api{UploadApi::Options{songs, removed, [this] { return now; },
                                     [this] { return counter += 1234567u; }, nullptr, nullptr}};
    UploadService service{songs, removed};
    Setup() { fs::create_directories(outside); }
    std::string sign_in() {
        const ApiReply r = api.login(api.pin());
        REQUIRE(r.status == 200);
        return r.set_cookie.substr(0, r.set_cookie.find(';'));
    }
};

// Stores `bytes` as `name` the way the server does, in two chunks; the saved name, empty when refused.
std::string upload(UploadService& s, const std::string& name, const std::string& bytes, int* status = nullptr) {
    ServiceError error;
    std::unique_ptr<UploadSink> sink = s.begin_upload(name, &error);
    if (sink) {
        const std::size_t half = bytes.size() / 2;
        if (sink->write(bytes.data(), half) && sink->write(bytes.data() + half, bytes.size() - half) &&
            sink->finish()) {
            return sink->saved_as();
        }
        error = sink->error();
    }
    if (status) {
        *status = error.status;
    }
    return {};
}

bool local(std::string_view text) {
    std::uint8_t bytes[16];
    std::size_t length = 0;
    return parse_address(text, bytes, length) && is_local_address({bytes, length});
}

std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

constexpr std::array<std::string_view, 1> kNames{"etude.local"};

struct Reply {
    int status = 0;
    std::string set_cookie;
    std::string raw;
    bool has(const std::string& text) const { return raw.find(text) != std::string::npos; }
};

// Sends `request` to 127.0.0.1:port and reads the answer until the server closes the connection.
Reply send_raw(int port, const std::string& request) {
    std::signal(SIGPIPE, SIG_IGN);
    Reply reply;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    timeval timeout{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    REQUIRE(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    for (std::size_t sent = 0; sent < request.size();) {
        const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (n <= 0) {
            break;
        }
        sent += static_cast<std::size_t>(n);
    }
    char buffer[8192];
    for (ssize_t n; (n = ::recv(fd, buffer, sizeof buffer, 0)) > 0;) {
        reply.raw.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fd);
    if (reply.raw.rfind("HTTP/1.", 0) == 0) {
        reply.status = std::atoi(reply.raw.c_str() + 9);
    }
    const std::size_t at = reply.raw.find("Set-Cookie: ");
    if (at != std::string::npos) {
        reply.set_cookie = reply.raw.substr(at + 12, reply.raw.find(';', at) - at - 12);
    }
    return reply;
}

struct Server {
    TempDir root;
    UploadServer server;
    explicit Server(const std::function<void(UploadServerOptions&)>& tweak = nullptr)
        : server([this, &tweak] {
              UploadServerOptions o;
              o.songs = root.path / "Songs";
              o.removed = root.path / "Removed songs";
              o.port = 0;
              o.bind = {"127.0.0.1"};
              if (tweak) {
                  tweak(o);
              }
              return o;
          }()) {
        std::string error;
        REQUIRE_MESSAGE(server.start(&error), error);
    }
    std::string host() const { return "127.0.0.1:" + std::to_string(server.port()); }

    // A request with the given head lines and body; Content-Length is the body's unless `length` is set.
    Reply request(const std::string& method, const std::string& path, const std::string& cookie = "",
                  const std::string& body = "", const std::string& more = "", const std::string& host_name = "",
                  const std::string& length = "") const {
        std::string text = method + " " + path + " HTTP/1.1\r\nHost: " + (host_name.empty() ? host() : host_name) +
                           "\r\nConnection: close\r\n" + more;
        if (!cookie.empty()) {
            text += "Cookie: " + cookie + "\r\n";
        }
        if (method == "POST") {
            text += "Content-Length: " + (length.empty() ? std::to_string(body.size()) : length) + "\r\n";
        }
        return send_raw(server.port(), text + "\r\n" + body);
    }
    Reply post(const std::string& path, const std::string& form, const std::string& cookie) const {
        return request("POST", path, cookie, form, "Content-Type: application/x-www-form-urlencoded\r\n");
    }
    std::string sign_in() const {
        const Reply r = post("/api/login", "pin=" + server.pin(), "");
        REQUIRE(r.status == 200);
        return r.set_cookie;
    }
    // A multipart upload of one file; `filename_param` is the whole filename parameter.
    Reply upload(const std::string& filename_param, const std::string& bytes, const std::string& cookie) const {
        const std::string b = "----etudetest";
        return request("POST", "/api/upload", cookie,
                       "--" + b + "\r\nContent-Disposition: form-data; name=\"file\"; " + filename_param +
                           "\r\nContent-Type: application/octet-stream\r\n\r\n" + bytes + "\r\n--" + b + "--\r\n",
                       "Content-Type: multipart/form-data; boundary=" + b + "\r\n");
    }
};

} // namespace

TEST_CASE("upload: off until started; a right PIN earns an HttpOnly, SameSite cookie") {
    Setup t;
    CHECK(t.api.status() == UploadStatus::Off);
    CHECK_FALSE(t.api.authorize("tv_session=" + std::string(kTokenChars, 'a')));
    t.api.start();
    CHECK(t.api.login("abcd").status == 403);
    const ApiReply ok = t.api.login(t.api.pin());
    CHECK(ok.set_cookie.find("HttpOnly") != std::string::npos);
    CHECK(ok.set_cookie.find("SameSite=Strict") != std::string::npos);
    const std::string cookie = ok.set_cookie.substr(0, ok.set_cookie.find(';'));
    CHECK(t.api.authorize(cookie));
    const std::string first_pin = t.api.pin();
    t.api.stop(); // a session ends with the server, and a new start makes a new PIN
    t.api.start();
    CHECK_FALSE(t.api.authorize(cookie));
    CHECK(t.api.pin() != first_pin);
}

TEST_CASE("upload: five wrong PINs lock it for a minute, and ten idle minutes turn it off") {
    UploadAuth auth("4821");
    for (int i = 0; i < kMaxPinFailures; ++i) {
        CHECK(auth.login("1111", std::string(kTokenChars, 'a'), 100).result == LoginResult::WrongPin);
    }
    CHECK(auth.login("4821", std::string(kTokenChars, 'a'), 101).result == LoginResult::Locked);
    CHECK(auth.login("4821", std::string(kTokenChars, 'a'), 100 + kPinLockMs - 1).result == LoginResult::Locked);
    CHECK(auth.login("4821", std::string(kTokenChars, 'a'), 100 + kPinLockMs).result == LoginResult::Ok);

    Setup t;
    t.api.start();
    const std::string cookie = t.sign_in();
    t.now += kServerIdleMs - 1;
    CHECK_FALSE(t.api.tick());
    t.now += 1;
    CHECK(t.api.tick());
    CHECK(t.api.status() == UploadStatus::Off);
    CHECK_FALSE(t.api.authorize(cookie));
}

TEST_CASE("upload: only home-network addresses, this server's own names, this page's origin") {
    for (const char* yes : {"127.0.0.1", "10.255.255.255", "172.16.0.1", "172.31.255.255", "192.168.0.1",
                            "169.254.1.1", "::1", "fe80::1", "fd12:3456::1", "::ffff:192.168.1.5"}) {
        CAPTURE(yes);
        CHECK(local(yes));
    }
    for (const char* no : {"8.8.8.8", "172.15.255.255", "172.32.0.1", "100.64.0.1", "0.0.0.0",
                           "2001:db8::1", "::ffff:8.8.8.8", "not an address", ""}) {
        CAPTURE(no);
        CHECK_FALSE(local(no));
    }
    using V = std::vector<std::string>;
    const auto listen = [](V machine) { return choose_listen_addresses(machine, false); };
    CHECK(listen({"192.168.1.20", "8.8.8.8", "100.92.33.39", "127.0.0.1"}) == V{"192.168.1.20"});
    CHECK(listen({"8.8.8.8"}) == V{"127.0.0.1"});
    CHECK(shown_address(V{"10.0.0.5", "192.168.1.20", "169.254.3.4"}, false) == "192.168.1.20");
    CHECK(shown_address(V{"127.0.0.1", "8.8.8.8"}, false).empty());
    for (const char* ok : {"192.168.1.20:8080", "etude.local:8080", "localhost", "[::1]:8080"}) {
        CAPTURE(ok);
        CHECK(host_header_ok(ok, kNames));
    }
    for (const char* rebinding : {"evil.example", "etude.local.evil.example", "etude.local:80a", "", "::1"}) {
        CAPTURE(rebinding);
        CHECK_FALSE(host_header_ok(rebinding, kNames));
    }
    CHECK(origin_ok("", "192.168.1.20:8080"));
    CHECK(origin_ok("http://192.168.1.20:8080", "192.168.1.20:8080"));
    for (const char* other : {"http://evil.example", "null", "https://192.168.1.20:8080", "http://192.168.1.20:8081"}) {
        CAPTURE(other);
        CHECK_FALSE(origin_ok(other, "192.168.1.20:8080"));
    }
    CHECK(cookie_value("tv_session=first; tv_session=second", "tv_session") == "first");
    CHECK(cookie_value("x_tv_session=zzz", "tv_session").empty());
}

TEST_CASE("upload: hostile names are refused, never repaired") {
    CHECK(clean_file_name("Fur Elise.mid").name == "Fur Elise.mid");
    CHECK(clean_file_name("\xE6\x9C\x88\xE5\x85\x89.mid").ok()); // 月光
    const std::string at_limit = std::string(kMaxNameBytes - 4, 'a') + ".mid";
    CHECK(clean_file_name(at_limit).ok());
    CHECK(clean_file_name(at_limit + "x").problem == NameProblem::TooLong);
    const struct {
        std::string name;
        NameProblem problem;
    } bad[] = {
        {"../a.mid", NameProblem::Separator},
        {"..\\a.mid", NameProblem::Separator},
        {"/etc/passwd.mid", NameProblem::Separator},
        {"a\xE2\x88\x95"
         "b.mid", // division slash
         NameProblem::Separator},
        {"..", NameProblem::Dots},
        {".htaccess", NameProblem::Dots},
        {"song.mid.", NameProblem::Dots},
        {"\xEF\xBC\x8E\xEF\xBC\x8E.mid", NameProblem::Dots}, // fullwidth dots
        {"", NameProblem::Empty},
        {std::string("a\0b.mid", 7), NameProblem::Control},
        {"a\nb.mid", NameProblem::Control},
        {"song\xE2\x80\xAE"
         "dim.mid", // right-to-left override
         NameProblem::Control},
        {"a\xFF.mid", NameProblem::BadUtf8},
        {"a\xC0\xAF.mid", NameProblem::BadUtf8}, // overlong "/"
    };
    for (const auto& b : bad) {
        CAPTURE(b.name);
        const CleanName c = clean_file_name(b.name);
        CHECK(c.problem == b.problem);
        CHECK(c.name.empty());
    }
    for (const char* hostile : {"/etc/passwd", "../a.mid", "a/../../b.mid", "a//b.mid", ".hidden/a.mid", "Inbox/.incoming/x.part"}) {
        CAPTURE(hostile);
        CHECK_FALSE(clean_relative_path(hostile, true).ok());
    }
    std::string deep = "a";
    for (std::size_t i = 1; i < kMaxDepth; ++i) {
        deep += "/a";
    }
    CHECK(clean_relative_path(deep, false).ok());
    CHECK(clean_relative_path(deep + "/a", false).problem == NameProblem::TooLong);
    CHECK(json_escape("a\"b<script>\xFF") == "a\\\"b\\u003cscript\\u003e\\ufffd");
}

TEST_CASE("upload names: characters only Windows cannot hold are kept as typed") {
    for (const char* name : {"Why? (take 2).mid", "Why: part 2.mid", "a<b>c|d*e.mid", "\"live\".mid"}) {
        CAPTURE(name);
        CHECK(clean_file_name(name).name == name);
    }
}

TEST_CASE("upload: a file must be what its name says") {
    for (const char* ok : {"a.mid", "a.MID", "a.mxl", "a.abc", "a.pdf", "a.JPEG", "a.heic"}) {
        CAPTURE(ok);
        CHECK(check_upload_type(ok).ok());
    }
    for (const char* no : {"a.exe", "a.sh", "a.html", "a.svg", "a.zip", "a.mid.exe", "a", "a."}) {
        CAPTURE(no);
        CHECK(check_upload_type(no).problem != NameProblem::None);
    }
    CHECK(header_matches(".mid", bytes("MThd\0\0\0\6")));
    CHECK_FALSE(header_matches(".mid", bytes("<?xml version")));
    CHECK_FALSE(header_matches(".pdf", bytes("MThd")));
    CHECK_FALSE(header_matches(".jpg", bytes("\x89PNG")));
    CHECK_FALSE(header_matches(".xml", bytes("<html><body>")));
    CHECK_FALSE(header_matches(".xml", bytes("<svg xmlns=\"http://www.w3.org/2000/svg\">")));
    CHECK_FALSE(header_matches(".abc", bytes("just some text, no tune")));
    CHECK(header_matches(".musicxml", bytes("<?xml version=\"1.0\"?>")));
    CHECK(header_matches(".abc", bytes("X:1\nK:C\n")));
    Setup t;
    int status = 0;
    CHECK(upload(t.service, "virus.exe", "MZ....", &status).empty());
    CHECK(status == 415);
    CHECK(upload(t.service, "song.mid", "<html><script>", &status).empty());
    CHECK(status == 415);
    CHECK(upload(t.service, "empty.mid", "", &status).empty());
    CHECK(status == 415);
    CHECK(upload(t.service, "scan.pdf", "%PDF-1.4 ....") == "scan.pdf");
    CHECK(tree(t.songs) == "Inbox/\nInbox/.incoming/\nInbox/scan.pdf\n");
}

TEST_CASE("upload: a song lands whole in Songs/Inbox, a taken name gets a number, the limit is exact") {
    Setup t;
    CHECK(upload(t.service, "a.mid", midi_bytes("1")) == "a.mid");
    CHECK(upload(t.service, "a.mid", midi_bytes("2")) == "a (2).mid");
    CHECK(read_file(t.songs / "Inbox/a.mid") == midi_bytes("1"));
    CHECK(tree(t.songs) == "Inbox/\nInbox/.incoming/\nInbox/a (2).mid\nInbox/a.mid\n");
    const std::string head = midi_bytes();
    const std::string block(1024 * 1024, 'x');
    for (const bool over : {false, true}) {
        ServiceError e;
        std::unique_ptr<UploadSink> sink = t.service.begin_upload(over ? "over.mid" : "limit.mid", &e);
        REQUIRE(sink);
        REQUIRE(sink->write(head.data(), head.size()));
        for (std::uint64_t left = kMaxUploadBytes - head.size(); left > 0;) {
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(left, block.size()));
            REQUIRE(sink->write(block.data(), n));
            left -= n;
        }
        if (over) {
            CHECK_FALSE(sink->write("x", 1)); // one byte past the limit
            CHECK(sink->error().status == 413);
            CHECK_FALSE(sink->finish());
        } else {
            CHECK(sink->finish()); // exactly at it
        }
    }
    CHECK(fs::exists(t.songs / "Inbox/limit.mid"));
    CHECK_FALSE(fs::exists(t.songs / "Inbox/over.mid"));
}

TEST_CASE("upload: hostile names and paths create, move and delete nothing outside the Songs folder") {
    Setup t;
    write_file(t.songs / "a.mid");
    write_file(t.outside / "secret.mid", "SECRET");
    const std::string before = tree(t.root.path);
    for (const std::string& name : {std::string("../evil.mid"), (t.outside / "evil.mid").string(), std::string("a/b.mid"),
                                    std::string(".."), std::string(""), std::string("a\0b.mid", 7),
                                    std::string(5000, 'a') + ".mid", std::string("evil\xE2\x88\x95x.mid")}) {
        ServiceError e;
        CHECK(t.service.begin_upload(name, &e) == nullptr);
        CHECK(e.status >= 400);
    }
    for (const std::string& path : {std::string("../outside/secret.mid"), (t.outside / "secret.mid").string(),
                                    std::string("a.mid/../../outside/secret.mid"), std::string("..\\a.mid"),
                                    std::string("\xEF\xBC\x8E\xEF\xBC\x8E/a.mid")}) {
        CAPTURE(path);
        CHECK_FALSE(t.service.rename(path, "z.mid").ok);
        CHECK_FALSE(t.service.move(path, "Other").ok);
        CHECK_FALSE(t.service.remove(path).ok);
    }
    CHECK_FALSE(t.service.rename("a.mid", "../x.mid").ok);
    CHECK_FALSE(t.service.move("a.mid", "../x").ok);
    CHECK(tree(t.root.path) == before);
    // A link out of the Songs folder is never followed: not for an Inbox, a folder or a file.
    std::error_code ec;
    fs::create_directory_symlink(t.outside, t.songs / "out", ec);
    if (!ec) {
        fs::create_symlink(t.outside / "secret.mid", t.songs / "file.mid", ec);
        CHECK_FALSE(t.service.remove("out/secret.mid").ok);
        CHECK_FALSE(t.service.rename("file.mid", "z.mid").ok);
        CHECK(t.service.move("a.mid", "out").error.status == 403);
        fs::remove(t.songs / "out");
        fs::create_directory_symlink(t.outside, t.songs / "Inbox", ec);
        CHECK(t.service.begin_upload("a.mid", nullptr) == nullptr);
        CHECK(read_file(t.outside / "secret.mid") == "SECRET");
        CHECK(tree(t.outside) == "secret.mid\n");
    }
}

TEST_CASE("upload: rename and move never replace a file, delete moves it to Removed songs") {
    Setup t;
    write_file(t.songs / "Classical/old.mid", "OLD");
    write_file(t.songs / "Classical/taken.mid", "TAKEN");
    write_file(t.songs / "Inbox/taken.mid", "NEW");
    CHECK(t.service.rename("Classical/old.mid", "taken.mid").error.status == 409);
    CHECK(read_file(t.songs / "Classical/taken.mid") == "TAKEN");
    const ServiceResult moved = t.service.move("Inbox/taken.mid", "Classical");
    REQUIRE(moved.ok);
    CHECK(moved.path == "Classical/taken (2).mid");
    CHECK(read_file(t.songs / "Classical/taken.mid") == "TAKEN");
    REQUIRE(t.service.remove("Classical/old.mid").ok);
    CHECK(read_file(t.removed / "Classical/old.mid") == "OLD");
    CHECK_FALSE(fs::exists(t.songs / "Classical/old.mid"));
}

TEST_CASE("upload: the song the piano has open is not renamed, moved or deleted") {
    Setup t;
    write_file(t.songs / "Classical/open.mid", "OPEN");
    write_file(t.songs / "tunes.abc", "X:1\nK:C\nCDEF|");
    std::string open = (t.songs / "Classical/open.mid").string();
    UploadService service(t.songs, t.removed, [&open] { return open; });
    for (const char* path : {"Classical/open.mid", "tunes.abc"}) {
        CAPTURE(path);
        open = path == std::string("tunes.abc") ? (t.songs / path).string() + "#X=3" : (t.songs / path).string();
        CHECK(service.rename(path, "new.mid").error.status == 409);
        CHECK(service.move(path, "Elsewhere").error.status == 409);
        CHECK(service.remove(path).error.status == 409);
    }
    CHECK_FALSE(fs::exists(t.removed));
    open.clear(); // closed: free again
    CHECK(service.remove("Classical/open.mid").ok);
}

TEST_CASE("upload: the phone page's script reaches no other server and writes text, not markup") {
    const std::string html(upload_page_html());
    CHECK(html.size() > 10000);
    for (const char* outside : {"src=\"http", "href=\"http", "url(http", "@import", "<link", "src=\"//"}) {
        CAPTURE(outside);
        CHECK(html.find(outside) == std::string::npos);
    }
    for (const char* reach : {"fetch(\"http", "fetch('http", ".src", "createElement(\"script", "createElement(\"iframe",
                              "importScripts", "new Worker", "WebSocket", "sendBeacon", "window.open",
                              "document.cookie", "innerHTML", "document.write", "eval("}) {
        CAPTURE(reach);
        CHECK(html.find(reach) == std::string::npos);
    }
    std::size_t calls = 0;
    for (std::size_t at = html.find("call(\""); at != std::string::npos; at = html.find("call(\"", at + 1)) {
        const std::size_t comma = html.find(", \"", at);
        REQUIRE(comma != std::string::npos);
        CHECK(html.compare(comma + 3, 5, "/api/") == 0); // every request names a path of this server
        ++calls;
    }
    CHECK(calls >= 6);
}

TEST_CASE("upload server: the page is public, everything else needs the PIN, five wrong PINs lock") {
    Server s;
    CHECK(s.request("GET", "/").status == 200);
    for (const char* path : {"/api/files", "/etc/passwd", "/../etc/passwd"}) {
        CAPTURE(path);
        CHECK(s.request("GET", path).status == 401);
    }
    CHECK(s.post("/api/delete", "path=a.mid", "").status == 401);
    CHECK(s.upload("filename=\"a.mid\"", midi_bytes(), "").status == 401);
    CHECK(s.post("/api/delete", "path=a.mid", "tv_session=" + s.server.pin()).status == 401); // the PIN is no cookie
    const std::string cookie = s.sign_in();
    CHECK(s.request("GET", "/api/files", cookie).status == 200);
    const std::string wrong = s.server.pin() == "0000" ? "1111" : "0000";
    for (int i = 0; i < kMaxPinFailures; ++i) {
        CHECK(s.post("/api/login", "pin=" + wrong, "").status == 403);
    }
    CHECK(s.post("/api/login", "pin=" + s.server.pin(), "").status == 429);
}

TEST_CASE("upload server: another site's name or origin is refused, and so is an oversize or chunked form") {
    Server s;
    const std::string cookie = s.sign_in();
    const std::string port = std::to_string(s.server.port());
    CHECK(s.request("GET", "/api/files", cookie, "", "", "evil.example:" + port).status == 400); // DNS rebinding
    CHECK(s.request("GET", "/api/files", cookie, "", "", "etude.local:" + port).status == 200);
    for (const char* origin : {"http://evil.example", "null"}) {
        CAPTURE(origin);
        CHECK(s.request("POST", "/api/delete", cookie, "path=a.mid",
                        std::string("Origin: ") + origin + "\r\nContent-Type: application/x-www-form-urlencoded\r\n")
                  .status == 403);
    }
    CHECK(s.request("PUT", "/api/files", cookie).status == 405);
    // Nothing follows these Content-Lengths: the answer must not wait for the body.
    const std::string form = "Content-Type: application/x-www-form-urlencoded\r\n";
    CHECK(s.request("POST", "/api/login", "", "", form, "", "5000").status == 413);
    CHECK(s.request("POST", "/api/upload", cookie, "", "Content-Type: multipart/form-data; boundary=x\r\n", "",
                    std::to_string(kMaxUploadBytes + 1024 * 1024 + 1))
              .status == 413);
    const std::string chunked = "POST /api/rename HTTP/1.1\r\nHost: " + s.host() + "\r\nCookie: " + cookie +
                                "\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n" + form +
                                "\r\n4\r\npin=\r\n0\r\n\r\n";
    CHECK(send_raw(s.server.port(), chunked).status == 411);
}

TEST_CASE("upload server: hostile file names create nothing, and a file must be what its name says") {
    Server s;
    const std::string cookie = s.sign_in();
    const Reply ok = s.upload("filename=\"Fur Elise.mid\"", midi_bytes("notes"), cookie);
    CHECK(ok.status == 200);
    CHECK(read_file(s.root.path / "Songs/Inbox/Fur Elise.mid") == midi_bytes("notes"));
    const std::string before = tree(s.root.path);
    for (const std::string& param : {std::string("filename=\"../evil.mid\""), std::string("filename=\"..\\\\evil.mid\""),
                                     std::string("filename=\"/tmp/evil.mid\""), std::string("filename*=UTF-8''..%2F..%2Fevil.mid"),
                                     std::string("filename*=UTF-8''a%00b.mid"), std::string("filename=\".htaccess\""),
                                     std::string("filename=\"a\xE2\x88\x95"
                                                 "b.mid\""),
                                     "filename=\"" + std::string(5000, 'a') + ".mid\"", std::string("filename=\"setup.exe\""),
                                     std::string("filename=\"\"")}) {
        CAPTURE(param);
        const Reply r = s.upload(param, midi_bytes(), cookie);
        CHECK(r.status >= 400);
        CHECK(r.status < 500);
    }
    CHECK(s.upload("filename=\"fake.mid\"", "<html><script>alert(1)</script></html>", cookie).status == 415);
    CHECK(s.upload("filename=\"fake.pdf\"", midi_bytes(), cookie).status == 415);
    CHECK(tree(s.root.path) == before);
    CHECK_FALSE(fs::exists("/tmp/evil.mid"));
}

TEST_CASE("upload server: the song the piano has open is not renamed, moved or deleted") {
    Server s([](UploadServerOptions& o) {
        o.open_song = [songs = o.songs] { return (songs / "Classical/open.mid").string(); };
    });
    write_file(s.root.path / "Songs/Classical/open.mid", midi_bytes());
    const std::string cookie = s.sign_in();
    for (const char* call : {"/api/rename", "/api/move", "/api/delete"}) {
        CAPTURE(call);
        CHECK(s.post(call, "path=Classical%2Fopen.mid&name=z.mid&folder=Jazz", cookie).status == 409);
    }
    CHECK(fs::exists(s.root.path / "Songs/Classical/open.mid"));
}

TEST_CASE("upload server: it listens only on private or loopback addresses, never all") {
    // Planned, not bound: a real network address would raise a Mac's firewall question.
    UploadServerOptions o;
    for (const std::string& text : planned_addresses(o)) {
        std::uint8_t bytes[16];
        std::size_t length = 0;
        CAPTURE(text);
        REQUIRE(parse_address(text, bytes, length));
        CHECK((text == "127.0.0.1" || is_private_lan_address({bytes, length})));
    }
    CHECK_FALSE(planned_addresses(o).empty());
}
