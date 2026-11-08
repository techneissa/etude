// SPDX-License-Identifier: GPL-3.0-or-later
// The upload server over a real socket on this machine: a plain client written here sends the
// requests a browser, a script or an attacker would, byte for byte.
#include "../app/upload_server.hpp"
#include "timing_factor.hpp"
#include "upload_test_support.hpp"

#include "etude/upload_network.hpp"
#include "etude/upload_rules.hpp"

#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <poll.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

struct Reply {
    int status = 0;
    std::map<std::string, std::string> headers; // names in lower case
    std::string body;
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
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        ::close(fd);
        return reply; // status 0: refused
    }
    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (n <= 0) {
            break;
        }
        sent += static_cast<std::size_t>(n);
    }
    char buffer[8192];
    for (;;) {
        const ssize_t n = ::recv(fd, buffer, sizeof buffer, 0);
        if (n <= 0) {
            break;
        }
        reply.raw.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fd);
    const std::size_t line_end = reply.raw.find("\r\n");
    if (reply.raw.rfind("HTTP/1.", 0) == 0 && line_end != std::string::npos) {
        reply.status = std::atoi(reply.raw.c_str() + 9);
        const std::size_t head_end = reply.raw.find("\r\n\r\n");
        std::size_t at = line_end + 2;
        while (head_end != std::string::npos && at < head_end) {
            const std::size_t eol = reply.raw.find("\r\n", at);
            const std::string line = reply.raw.substr(at, eol - at);
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                for (char& c : name) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                std::size_t v = colon + 1;
                while (v < line.size() && line[v] == ' ') {
                    ++v;
                }
                reply.headers[name] = line.substr(v);
            }
            at = eol + 2;
        }
        if (head_end != std::string::npos) {
            reply.body = reply.raw.substr(head_end + 4);
        }
    }
    return reply;
}

struct Request {
    std::string method = "GET";
    std::string path = "/";
    std::string host;
    std::string cookie;
    std::string origin;
    std::string content_type;
    std::string body;
    std::string content_length; // overrides the real one
};

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

    Reply send(Request r) const {
        if (r.host.empty()) {
            r.host = host();
        }
        std::string text =
            r.method + " " + r.path + " HTTP/1.1\r\nHost: " + r.host + "\r\nConnection: close\r\n";
        if (!r.cookie.empty()) {
            text += "Cookie: " + r.cookie + "\r\n";
        }
        if (!r.origin.empty()) {
            text += "Origin: " + r.origin + "\r\n";
        }
        if (!r.content_type.empty()) {
            text += "Content-Type: " + r.content_type + "\r\n";
        }
        if (r.method == "POST" || !r.body.empty() || !r.content_length.empty()) {
            text += "Content-Length: " +
                    (r.content_length.empty() ? std::to_string(r.body.size()) : r.content_length) +
                    "\r\n";
        }
        text += "\r\n" + r.body;
        return send_raw(server.port(), text);
    }

    Reply get(const std::string& path, const std::string& cookie = "",
              const std::string& host = "") const {
        Request r;
        r.path = path;
        r.cookie = cookie;
        r.host = host;
        return send(r);
    }

    // POST of form fields.
    Reply post(const std::string& path, const std::string& form, const std::string& cookie) const {
        Request r;
        r.method = "POST";
        r.path = path;
        r.cookie = cookie;
        r.content_type = "application/x-www-form-urlencoded";
        r.body = form;
        return send(r);
    }

    // The cookie ("tv_session=...") a right PIN earns.
    std::string sign_in() const {
        const Reply r = post("/api/login", "pin=" + server.pin(), "");
        REQUIRE(r.status == 200);
        const std::string set = r.headers.at("set-cookie");
        return set.substr(0, set.find(';'));
    }

    // A multipart upload of one file; `disposition_name` is the whole filename parameter.
    Reply upload(const std::string& filename_param, const std::string& bytes,
                 const std::string& cookie) const {
        const std::string boundary = "----etudetest";
        Request r;
        r.method = "POST";
        r.path = "/api/upload";
        r.cookie = cookie;
        r.content_type = "multipart/form-data; boundary=" + boundary;
        r.body = "--" + boundary +
                 "\r\nContent-Disposition: form-data; name=\"note\"\r\n\r\nhello\r\n--" + boundary +
                 "\r\nContent-Disposition: form-data; name=\"file\"; " + filename_param +
                 "\r\nContent-Type: application/octet-stream\r\n\r\n" + bytes + "\r\n--" +
                 boundary + "--\r\n";
        return send(r);
    }
    Reply upload_named(const std::string& name, const std::string& bytes,
                       const std::string& cookie) const {
        return upload("filename=\"" + name + "\"", bytes, cookie);
    }
};

} // namespace

TEST_CASE("upload server: off until started, one port, stops and starts again") {
    TempDir dir;
    UploadServerOptions o;
    o.songs = dir.path / "Songs";
    o.removed = dir.path / "Removed songs";
    o.port = 0;
    o.bind = {"127.0.0.1"};
    UploadServer server(o);
    CHECK(server.status() == UploadStatus::Off);
    CHECK(server.pin().empty());
    CHECK(server.port() == 0);
    REQUIRE(server.start());
    CHECK(server.status() == UploadStatus::Ready);
    CHECK(is_pin_text(server.pin()));
    const int port = server.port();
    CHECK(port > 0);
    CHECK(send_raw(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                             "\r\nConnection: close\r\n\r\n")
              .status == 200);
    server.stop();
    CHECK(server.status() == UploadStatus::Off);
    CHECK(send_raw(port, "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n").status == 0);
    REQUIRE(server.start());
    CHECK(server.status() == UploadStatus::Ready);
    CHECK(send_raw(server.port(),
                   "GET / HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(server.port()) +
                       "\r\nConnection: close\r\n\r\n")
              .status == 200);
}

TEST_CASE("upload server: a port that is taken is a failure with a reason") {
    TempDir dir;
    UploadServerOptions o;
    o.songs = dir.path / "Songs";
    o.removed = dir.path / "Removed songs";
    o.port = 0;
    o.bind = {"127.0.0.1"};
    UploadServer first(o);
    REQUIRE(first.start());
    o.port = first.port();
    UploadServer second(o);
    std::string error;
    CHECK_FALSE(second.start(&error));
    CHECK(second.status() == UploadStatus::Error);
    CHECK(error.find("taken") != std::string::npos);
    CHECK(second.reason() == error);
}

TEST_CASE("upload server: the page is public, everything else needs the PIN") {
    Server s;
    const Reply page = s.send({});
    CHECK(page.status == 200);
    CHECK(page.headers.at("content-type").find("text/html") == 0);
    CHECK(page.headers.at("x-content-type-options") == "nosniff");
    CHECK(page.headers.at("cache-control") == "no-store");
    CHECK(page.headers.at("x-frame-options") == "DENY");
    CHECK(page.headers.count("content-security-policy") == 1);

    for (const char* path : {"/api/files", "/api/nothing", "/etc/passwd", "/../etc/passwd"}) {
        Request r;
        r.path = path;
        INFO(path);
        CHECK(s.send(r).status == 401);
    }
    CHECK(s.post("/api/rename", "path=a.mid&name=b.mid", "").status == 401);
    CHECK(s.post("/api/move", "path=a.mid&folder=x", "").status == 401);
    CHECK(s.post("/api/delete", "path=a.mid", "").status == 401);
    CHECK(s.upload_named("a.mid", midi_bytes(), "").status == 401);
    // A made-up or malformed cookie is no session.
    CHECK(s.post("/api/delete", "path=a.mid", "tv_session=" + std::string(32, 'a')).status == 401);
    CHECK(s.post("/api/delete", "path=a.mid", "tv_session=../../x").status == 401);
    CHECK(s.post("/api/delete", "path=a.mid", "tv_session=" + s.server.pin()).status == 401);
    CHECK(tree(s.root.path).empty());
}

TEST_CASE("upload server: a wrong PIN is refused, the right one earns a cookie that works") {
    Server s;
    const std::string wrong = s.server.pin() == "0000" ? "1111" : "0000";
    const Reply bad = s.post("/api/login", "pin=" + wrong, "");
    CHECK(bad.status == 403);
    CHECK(bad.headers.count("set-cookie") == 0);
    CHECK(s.post("/api/login", "pin=", "").status == 403);
    CHECK(s.post("/api/login", "", "").status == 403);
    CHECK(s.post("/api/login", "pin=" + s.server.pin() + "9", "").status == 403);

    const Reply good = s.post("/api/login", "pin=" + s.server.pin(), "");
    CHECK(good.status == 200);
    const std::string set = good.headers.at("set-cookie");
    CHECK(set.find("HttpOnly") != std::string::npos);
    CHECK(set.find("SameSite=Strict") != std::string::npos);
    const std::string cookie = set.substr(0, set.find(';'));
    Request r;
    r.path = "/api/files";
    r.cookie = cookie;
    const Reply files = s.send(r);
    CHECK(files.status == 200);
    CHECK(files.body.find("\"files\":[]") != std::string::npos);
    CHECK(s.get("/api/session", cookie).body.rfind("{\"signed_in\":true", 0) == 0);
    CHECK(s.get("/api/session").body == "{\"signed_in\":false}");
}

TEST_CASE("upload server: five wrong PINs lock the PIN, the right one included") {
    Server s;
    const std::string wrong = s.server.pin() == "0000" ? "1111" : "0000";
    for (int i = 0; i < kMaxPinFailures; ++i) {
        CHECK(s.post("/api/login", "pin=" + wrong, "").status == 403);
    }
    CHECK(s.post("/api/login", "pin=" + s.server.pin(), "").status == 429);
}

TEST_CASE("upload server: a request must name this server and come from this page") {
    Server s;
    const std::string cookie = s.sign_in();
    // DNS rebinding: another site's name pointing at this address.
    CHECK(s.get("/", cookie, "evil.example").status == 400);
    CHECK(s.get("/api/files", cookie, "evil.example:" + std::to_string(s.server.port())).status ==
          400);
    CHECK(s.get("/api/files", cookie, "etude.local:" + std::to_string(s.server.port())).status ==
          200);
    // A form on another site: the browser sends its Origin.
    Request r;
    r.method = "POST";
    r.path = "/api/delete";
    r.cookie = cookie;
    r.origin = "http://evil.example";
    r.content_type = "application/x-www-form-urlencoded";
    r.body = "path=a.mid";
    CHECK(s.send(r).status == 403);
    r.origin = "null";
    CHECK(s.send(r).status == 403);
    r.origin = "http://" + s.host();
    CHECK(s.send(r).status == 404); // this page's own Origin gets through to the file check
    // Other methods are not offered.
    for (const char* method : {"PUT", "DELETE", "PATCH", "OPTIONS", "HEAD", "TRACE"}) {
        Request m;
        m.method = method;
        m.path = "/api/files";
        m.cookie = cookie;
        INFO(method);
        CHECK(s.send(m).status == 405);
    }
}

TEST_CASE("upload server: a form bigger than a form is refused before it is read") {
    Server s;
    const std::string cookie = s.sign_in();
    Request r;
    r.method = "POST";
    r.path = "/api/login";
    r.content_type = "application/x-www-form-urlencoded";
    r.content_length = "5000"; // nothing follows: the answer must not wait for it
    CHECK(s.send(r).status == 413);
    r.path = "/api/rename";
    r.cookie = cookie;
    CHECK(s.send(r).status == 413);
    r.content_length = "99999999999";
    r.path = "/api/upload";
    r.content_type = "multipart/form-data; boundary=x";
    CHECK(s.send(r).status == 413);
    r.content_length = std::to_string(kMaxUploadBytes + 1024 * 1024 + 1); // one byte past the limit
    CHECK(s.send(r).status == 413);
}

TEST_CASE("upload server: an upload lands in Songs/Inbox and is listed") {
    Server s;
    const std::string cookie = s.sign_in();
    const std::string bytes = midi_bytes(std::string(5000, 'n'));
    const Reply r = s.upload_named("Fur Elise.mid", bytes, cookie);
    CHECK(r.status == 200);
    CHECK(r.body == "{\"ok\":true,\"name\":\"Fur Elise.mid\",\"saved_as\":\"Fur Elise.mid\"}");
    CHECK(read_file(s.root.path / "Songs/Inbox/Fur Elise.mid") == bytes);
    CHECK(tree(s.root.path / "Songs") == "Inbox/\nInbox/.incoming/\nInbox/Fur Elise.mid\n");
    // The same name again is numbered.
    CHECK(s.upload_named("Fur Elise.mid", bytes, cookie).body.find("Fur Elise (2).mid") !=
          std::string::npos);
    Request files;
    files.path = "/api/files";
    files.cookie = cookie;
    const Reply list = s.send(files);
    CHECK(list.body.find("\"path\":\"Inbox/Fur Elise.mid\",\"size\":5014") != std::string::npos);
}

TEST_CASE("upload server: hostile file names are refused and nothing is written anywhere") {
    Server s;
    const std::string cookie = s.sign_in();
    const std::string before = tree(s.root.path);
    const std::string bytes = midi_bytes();
    struct Case {
        const char* label;
        std::string filename_param;
    };
    const std::vector<Case> cases{
        {"parent", "filename=\"../evil.mid\""},
        {"parents", "filename=\"../../../../tmp/evil.mid\""},
        {"backslash", "filename=\"..\\\\evil.mid\""},
        {"absolute", "filename=\"/tmp/evil.mid\""},
        {"nested", "filename=\"a/b.mid\""},
        {"encoded", "filename*=UTF-8''..%2F..%2Fevil.mid"},
        {"encoded nul", "filename*=UTF-8''a%00b.mid"},
        {"nul", std::string("filename=\"a\0b.mid\"", 18)},
        {"dot", "filename=\".\""},
        {"dotdot", "filename=\"..\""},
        {"hidden", "filename=\".htaccess\""},
        {"division slash", "filename=\"a\xE2\x88\x95"
                           "b.mid\""},
        {"fullwidth dots", "filename=\"\xEF\xBC\x8E\xEF\xBC\x8E\xEF\xBC\x8F"
                           "evil.mid\""},
        {"override", "filename=\"a\xE2\x80\xAE"
                     "dim.mid\""},
        {"long", "filename=\"" + std::string(5000, 'a') + ".mid\""},
        {"bad utf8", "filename=\"a\xFF\xFE.mid\""},
        {"exe", "filename=\"setup.exe\""},
        {"double", "filename=\"song.mid.exe\""},
        {"html", "filename=\"page.html\""},
        {"empty", "filename=\"\""},
    };
    for (const Case& c : cases) {
        INFO(c.label);
        const Reply r = s.upload(c.filename_param, bytes, cookie);
        CHECK(r.status >= 400);
        CHECK(r.status < 500);
        CHECK(r.has("\"error\""));
    }
    CHECK(tree(s.root.path) == before);
    CHECK_FALSE(fs::exists("/tmp/evil.mid"));
}

TEST_CASE("upload server: the file must be what its name says") {
    Server s;
    const std::string cookie = s.sign_in();
    CHECK(s.upload_named("fake.mid", "<html><script>alert(1)</script></html>", cookie).status ==
          415);
    CHECK(s.upload_named("fake.pdf", midi_bytes(), cookie).status == 415);
    CHECK(s.upload_named("empty.mid", "", cookie).status == 415);
    CHECK(s.upload_named("real.pdf", "%PDF-1.7 pretend", cookie).status == 200);
    CHECK(tree(s.root.path / "Songs") == "Inbox/\nInbox/.incoming/\nInbox/real.pdf\n");
}

TEST_CASE("upload server: rename, move and delete go through the same rules") {
    Server s;
    const std::string cookie = s.sign_in();
    CHECK(s.upload_named("a.mid", midi_bytes(), cookie).status == 200);
    write_file(s.root.path / "secret.mid", "SECRET"); // beside the Songs folder, not in it

    Reply r = s.post("/api/rename", "path=Inbox%2Fa.mid&name=b.mid", cookie);
    CHECK(r.status == 200);
    CHECK(r.body == "{\"ok\":true,\"path\":\"Inbox/b.mid\"}");
    r = s.post("/api/move", "path=Inbox%2Fb.mid&folder=Classical%2FBach", cookie);
    CHECK(r.body == "{\"ok\":true,\"path\":\"Classical/Bach/b.mid\"}");

    // Traversal and absolute paths, named every way a form can.
    for (const char* path :
         {"..%2Fsecret.mid", "Songs%2F..%2Fsecret.mid", "%2Fetc%2Fpasswd",
          "Classical%2F..%2F..%2Fsecret.mid", "..%5Csecret.mid",
          "Classical%2FBach%2F..%2F..%2F..%2Fsecret.mid", "%2E%2E%2Fsecret.mid"}) {
        INFO(path);
        CHECK(s.post("/api/rename", std::string("path=") + path + "&name=z.mid", cookie).status ==
              400);
        CHECK(s.post("/api/move", std::string("path=") + path + "&folder=x", cookie).status == 400);
        CHECK(s.post("/api/delete", std::string("path=") + path, cookie).status == 400);
    }
    CHECK(s.post("/api/move", "path=Classical%2FBach%2Fb.mid&folder=..%2Fout", cookie).status ==
          400);
    CHECK(s.post("/api/move", "path=Classical%2FBach%2Fb.mid&folder=%2Ftmp", cookie).status == 400);
    CHECK(s.post("/api/rename", "path=Classical%2FBach%2Fb.mid&name=..%2Fx.mid", cookie).status ==
          400);
    CHECK(s.post("/api/rename", "path=Classical%2FBach%2Fb.mid&name=x.exe", cookie).status == 400);
    CHECK(read_file(s.root.path / "secret.mid") == "SECRET");
    CHECK(fs::exists(s.root.path / "Songs/Classical/Bach/b.mid"));

    r = s.post("/api/delete", "path=Classical%2FBach%2Fb.mid", cookie);
    CHECK(r.status == 200);
    CHECK_FALSE(fs::exists(s.root.path / "Songs/Classical/Bach/b.mid"));
    CHECK(fs::exists(s.root.path / "Removed songs/Classical/Bach/b.mid"));
    CHECK(s.post("/api/delete", "path=Classical%2FBach%2Fb.mid", cookie).status == 404);
}

TEST_CASE("upload server: logout ends the session") {
    Server s;
    const std::string cookie = s.sign_in();
    Request files;
    files.path = "/api/files";
    files.cookie = cookie;
    CHECK(s.send(files).status == 200);
    CHECK(s.post("/api/logout", "", cookie).status == 200);
    CHECK(s.send(files).status == 401);
}

TEST_CASE("upload server: the addresses it offers are all on the local network") {
    for (const std::string& text : local_ipv4_addresses()) {
        std::uint8_t bytes[16];
        std::size_t length = 0;
        INFO(text);
        REQUIRE(parse_address(text, bytes, length));
        CHECK(length == 4);
        CHECK(is_local_address({bytes, length}));
        CHECK(bytes[0] != 127); // never loopback
    }
}

namespace {

int connect_to(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (fd >= 0 && ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// True when the server has closed `fd` (or answered and closed) within `ms`.
bool closed_within(int fd, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 50) > 0) {
            char buffer[4096];
            const ssize_t n = ::recv(fd, buffer, sizeof buffer, 0);
            if (n <= 0) {
                return true;
            }
        }
    }
    return false;
}

constexpr auto kSteady = std::chrono::steady_clock::now;

} // namespace

TEST_CASE("upload server: a client that trickles bytes is cut at the total deadline") {
    Server s([](UploadServerOptions& o) { o.request_deadline = std::chrono::milliseconds(700); });
    const int fd = connect_to(s.server.port());
    REQUIRE(fd >= 0);
    const std::string head = "POST /api/login HTTP/1.1\r\nHost: " + s.host() +
                             "\r\nContent-Type: application/x-www-form-urlencoded"
                             "\r\nContent-Length: 4000\r\n\r\n";
    REQUIRE(::send(fd, head.data(), head.size(), 0) == static_cast<ssize_t>(head.size()));
    // A byte every 150 ms keeps every single read well inside the library's 10 s timeout; only
    // the total deadline can end it (the body never completes: 4000 bytes would take ten minutes).
    const auto start = kSteady();
    bool cut = false;
    for (int i = 0; i < 40 && !cut; ++i) {
        if (::send(fd, "p", 1, 0) != 1) {
            cut = true;
        } else {
            cut = closed_within(fd, 150);
        }
    }
    const auto took = kSteady() - start;
    ::close(fd);
    CHECK(cut);
    CHECK(took >= std::chrono::milliseconds(600));
    CHECK_TIMING(took < std::chrono::milliseconds(3500));
}

TEST_CASE("upload server: one peer cannot hold every worker") {
    Server s([](UploadServerOptions& o) { o.max_per_peer = 3; });
    std::vector<int> held;
    for (int i = 0; i < 3; ++i) {
        held.push_back(connect_to(s.server.port()));
        REQUIRE(held.back() >= 0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // all three are being served
    const int fourth = connect_to(s.server.port());
    REQUIRE(fourth >= 0);
    CHECK(closed_within(fourth, 2000)); // refused at once
    for (const int fd : held) {
        CHECK_FALSE(closed_within(fd, 100)); // the three are still there
    }
    ::close(fourth);
    for (const int fd : held) {
        ::close(fd);
    }
    // They went away, so the same peer can come back.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(s.get("/").status == 200);
}

TEST_CASE("upload server: a chunked body is refused on every POST") {
    Server s;
    const std::string cookie = s.sign_in();
    for (const char* path : {"/api/login", "/api/rename", "/api/upload"}) {
        INFO(path);
        const std::string raw = std::string("POST ") + path + " HTTP/1.1\r\nHost: " + s.host() +
                                "\r\nCookie: " + cookie +
                                "\r\nConnection: close\r\nTransfer-Encoding: chunked"
                                "\r\nContent-Type: application/x-www-form-urlencoded"
                                "\r\n\r\n4\r\npin=\r\n0\r\n\r\n";
        CHECK(send_raw(s.server.port(), raw).status == 411);
    }
}

TEST_CASE("upload server: only the first file of a form is stored, and nothing else is appended") {
    Server s;
    const std::string cookie = s.sign_in();
    const std::string boundary = "----two";
    const auto part = [&](const std::string& disposition, const std::string& bytes) {
        return "--" + boundary + "\r\nContent-Disposition: form-data; " + disposition + "\r\n\r\n" +
               bytes + "\r\n";
    };
    const std::string first = midi_bytes("AAAA first file AAAA");
    Request r;
    r.method = "POST";
    r.path = "/api/upload";
    r.cookie = cookie;
    r.content_type = "multipart/form-data; boundary=" + boundary;
    r.body = part("name=\"note\"", "a text field before") +
             part("name=\"file\"; filename=\"a.mid\"", first) +
             part("name=\"caption\"", "a text field after, which must not be appended") +
             part("name=\"file2\"; filename=\"b.mid\"", midi_bytes("BBBB second file BBBB")) +
             "--" + boundary + "--\r\n";
    const Reply reply = s.send(r);
    CHECK(reply.status == 200);
    CHECK(reply.body.find("\"saved_as\":\"a.mid\"") != std::string::npos);
    CHECK(reply.body.find("\"ignored\":1") != std::string::npos);
    CHECK(read_file(s.root.path / "Songs/Inbox/a.mid") == first);
    CHECK_FALSE(fs::exists(s.root.path / "Songs/Inbox/b.mid"));
    CHECK(tree(s.root.path / "Songs") == "Inbox/\nInbox/.incoming/\nInbox/a.mid\n");
}

TEST_CASE("upload server: the song the piano has open is not renamed, moved or deleted") {
    Server s([](UploadServerOptions& o) {
        o.open_song = [songs = o.songs] { return (songs / "Classical/open.mid").string(); };
    });
    write_file(s.root.path / "Songs/Classical/open.mid", midi_bytes());
    write_file(s.root.path / "Songs/Classical/other.mid", midi_bytes());
    const std::string cookie = s.sign_in();
    for (const char* call : {"/api/rename", "/api/move", "/api/delete"}) {
        INFO(call);
        const Reply r = s.post(call, "path=Classical%2Fopen.mid&name=z.mid&folder=Jazz", cookie);
        CHECK(r.status == 409);
        CHECK(r.has("open on the piano"));
    }
    CHECK(fs::exists(s.root.path / "Songs/Classical/open.mid"));
    CHECK(s.post("/api/rename", "path=Classical%2Fother.mid&name=z.mid", cookie).status == 200);
}

TEST_CASE("upload server: the addresses it plans to listen on are private or loopback, never all") {
    // Planned, not bound: opening a port on the real network would raise the system's firewall
    // question on a Mac. The choice itself is tested in upload_network_test.
    UploadServerOptions o;
    for (const std::string& text : planned_addresses(o)) {
        std::uint8_t bytes[16];
        std::size_t length = 0;
        INFO(text);
        REQUIRE(parse_address(text, bytes, length));
        CHECK((text == "127.0.0.1" || is_private_lan_address({bytes, length})));
    }
    CHECK_FALSE(planned_addresses(o).empty());
    o.bind = {"127.0.0.1"};
    CHECK(planned_addresses(o) == std::vector<std::string>{"127.0.0.1"});
    o.bind.clear();
    o.all_interfaces = true;
    CHECK(planned_addresses(o) == std::vector<std::string>{"0.0.0.0"});
}

TEST_CASE("upload server: addresses() names what it listens on, and nothing when off") {
    Server s;
    CHECK(s.server.addresses() == std::vector<std::string>{"127.0.0.1"});
    s.server.stop();
    CHECK(s.server.addresses().empty());
}

TEST_CASE("upload server: the page says it is not encrypted") {
    Server s;
    CHECK(s.get("/").has("not encrypted"));
}

TEST_CASE("upload server: the phone page is compiled in, whole, and loads nothing from outside") {
    Server s;
    const Reply page = s.get("/");
    REQUIRE(page.status == 200);
    const std::string& html = page.body;
    CHECK(html.size() > 10000);
    CHECK(html.rfind("<!doctype html>", 0) == 0);
    // The parts of the design: the PIN's cells, Add songs and Photo, the queue, the library with
    // its folder chips and the menu of one file, and the help line of the #102 decision.
    for (const char* part : {"id=\"about\"",
                             "No songs match",
                             "}, 150)",
                             "facts(f)",
                             "id=\"pin\"",
                             "id=\"pick\"",
                             "id=\"photo\"",
                             "id=\"queue\"",
                             "id=\"chips\"",
                             "/api/login",
                             "/api/upload",
                             "/api/files",
                             "/api/rename",
                             "/api/move",
                             "/api/delete",
                             "Rename",
                             "Move to",
                             "Delete",
                             "Keep",
                             "Photo",
                             "Library",
                             "Inbox",
                             "convert it on a computer first and upload the MusicXML",
                             "Plain HTTP"}) {
        INFO(part);
        CHECK(page.has(part));
    }
    // One document: no script, style, font or image from anywhere else.
    for (const char* outside :
         {"src=\"http", "href=\"http", "url(http", "@import", "<link", "src=\"//", "href=\"//"}) {
        INFO(outside);
        CHECK_FALSE(page.has(outside));
    }
}

TEST_CASE("upload server: the phone page's script reaches no other server and writes text, not markup") {
    Server s;
    const Reply page = s.get("/");
    REQUIRE(page.status == 200);
    const std::string& html = page.body;
    // Nor does its script reach out: every request it makes names a path of this server, no element
    // that loads (a script, frame, image or link) is made from script, none is given a source, and
    // nothing opens a window, a socket, a worker or a beacon.
    for (const char* reach : {"fetch(\"http",
                              "fetch('http",
                              "fetch(`http",
                              ".open(\"GET\", \"http",
                              ".open(\"POST\", \"http",
                              ".src",
                              "\"src\"",
                              "'src'",
                              "el(\"script",
                              "el(\"iframe",
                              "el(\"img",
                              "el(\"link",
                              "createElement(\"script",
                              "createElement(\"iframe",
                              "createElement(\"img",
                              "createElement(\"link",
                              "import(",
                              "importScripts",
                              "new Worker",
                              "WebSocket",
                              "EventSource",
                              "sendBeacon",
                              "window.open",
                              "location.href",
                              "location =",
                              "location=",
                              "document.cookie"}) {
        INFO(reach);
        CHECK_FALSE(page.has(reach));
    }
    // Every `call("METHOD", "/path"` and every XHR names a path under /api/.
    std::size_t calls = 0;
    for (std::size_t at = html.find("call(\""); at != std::string::npos;
         at = html.find("call(\"", at + 1)) {
        const std::size_t comma = html.find(", \"", at);
        REQUIRE(comma != std::string::npos);
        INFO(html.substr(at, 40));
        CHECK(html.compare(comma + 3, 5, "/api/") == 0);
        ++calls;
    }
    CHECK(calls >= 6);
    // The one link the script makes is an icon of the page's own sprite.
    CHECK(html.find("setAttribute(\"href\", \"#i-\"") != std::string::npos);
    CHECK(html.find("setAttribute(\"href\"") == html.rfind("setAttribute(\"href\""));
    CHECK(html.find("fetch(path") != std::string::npos); // the one fetch takes a path made above
    CHECK(html.find("fetch(", html.find("fetch(path") + 1) == std::string::npos);
    CHECK(html.find("x.open(\"POST\", \"/api/upload\")") != std::string::npos);
    // What it writes into the page from a file name goes in as text, never as markup.
    CHECK_FALSE(page.has("innerHTML"));
    // The long help paragraph is not on the page itself: it is shown from the (i) by the title.
    CHECK_FALSE(page.has("class=\"help\""));
    CHECK(html.find("Everything lands in the Inbox.") ==
          html.rfind("Everything lands in the Inbox."));
    CHECK(html.find("var ABOUT = \"Everything lands in the Inbox.") != std::string::npos);
    CHECK_FALSE(page.has("document.write"));
    CHECK_FALSE(page.has("eval("));
}

TEST_CASE("upload server: the session reply carries the time left, and uploads are counted") {
    Server s;
    const std::string cookie = s.sign_in();
    Request session;
    session.path = "/api/session";
    session.cookie = cookie;
    const Reply who = s.send(session);
    CHECK(who.body.rfind("{\"signed_in\":true,\"remaining_s\":", 0) == 0);
    CHECK(s.server.progress().completed == 0);
    CHECK(s.upload_named("a.mid", midi_bytes(), cookie).status == 200);
    CHECK(s.upload_named("b.exe", "MZ", cookie).status == 415);
    const UploadProgress progress = s.server.progress();
    CHECK_FALSE(progress.active);
    CHECK(progress.completed == 1);
    CHECK(progress.failed == 1);
    CHECK(progress.name == "b.exe");
}
