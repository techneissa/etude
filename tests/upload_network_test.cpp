// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/upload_network.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>

using namespace etude;

namespace {

bool local(std::string_view text) {
    std::uint8_t bytes[16];
    std::size_t length = 0;
    return parse_address(text, bytes, length) && is_local_address({bytes, length});
}

bool parses(std::string_view text) {
    std::uint8_t bytes[16];
    std::size_t length = 0;
    return parse_address(text, bytes, length);
}

constexpr std::array<std::string_view, 1> kNames{"etude.local"};

} // namespace

TEST_CASE("local network: private IPv4 ranges, edges included") {
    for (const char* yes :
         {"127.0.0.1", "127.255.255.255", "10.0.0.1", "10.255.255.255", "192.168.0.1",
          "192.168.255.255", "172.16.0.1", "172.31.255.255", "169.254.1.1"}) {
        INFO(yes);
        CHECK(local(yes));
    }
    for (const char* no :
         {"8.8.8.8", "1.1.1.1", "11.0.0.1", "172.15.255.255", "172.32.0.1", "192.167.1.1",
          "192.169.1.1", "169.253.1.1", "100.64.0.1", "0.0.0.0", "224.0.0.1", "255.255.255.255"}) {
        INFO(no);
        CHECK_FALSE(local(no));
    }
}

TEST_CASE("local network: IPv6 loopback, unique local, link-local and mapped IPv4") {
    for (const char* yes : {"::1", "fe80::1", "fe80::1%en0", "febf::1", "fc00::1", "fd12:3456::1",
                            "::ffff:192.168.1.5", "::ffff:10.0.0.1", "::ffff:127.0.0.1"}) {
        INFO(yes);
        CHECK(local(yes));
    }
    for (const char* no : {"2001:db8::1", "2606:4700::1111", "fec0::1", "::", "::2",
                           "::ffff:8.8.8.8", "::ffff:172.32.0.1", "ff02::1"}) {
        INFO(no);
        CHECK_FALSE(local(no));
    }
    CHECK_FALSE(is_local_address({}));
    const std::array<std::uint8_t, 5> five{10, 0, 0, 1, 0};
    CHECK_FALSE(is_local_address(five));
}

TEST_CASE("local network: addresses that are not addresses") {
    for (const char* bad :
         {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.-4", "01234.1.1.1", "a.b.c.d", "1..2.3",
          "localhost", "::g", ":::1", "1::2::3", "1:2:3:4:5:6:7:8:9", "12345::1", "::1.2.3"}) {
        INFO(bad);
        CHECK_FALSE(parses(bad));
    }
}

TEST_CASE("host header: this server's own names, with or without a port") {
    CHECK(host_header_ok("192.168.1.20:8080", kNames));
    CHECK(host_header_ok("192.168.1.20", kNames));
    CHECK(host_header_ok("etude.local:8080", kNames));
    CHECK(host_header_ok("ETUDE.local", kNames));
    CHECK(host_header_ok("localhost:8080", kNames));
    CHECK(host_header_ok("[::1]:8080", kNames));
    CHECK(host_header_ok("[fe80::1]", kNames));
}

TEST_CASE("host header: another site pointed at this address is refused (DNS rebinding)") {
    CHECK_FALSE(host_header_ok("evil.example", kNames));
    CHECK_FALSE(host_header_ok("evil.example:8080", kNames));
    CHECK_FALSE(host_header_ok("etude.local.evil.example", kNames));
    CHECK_FALSE(host_header_ok("etude.local:8080:9", kNames));
    CHECK_FALSE(host_header_ok("etude.local:", kNames));
    CHECK_FALSE(host_header_ok("etude.local:80a", kNames));
    CHECK_FALSE(host_header_ok("etude.local:123456", kNames));
    CHECK_FALSE(host_header_ok("", kNames));
    CHECK_FALSE(host_header_ok("[::1", kNames));
    CHECK_FALSE(host_header_ok("[1.2.3.4]:80", kNames));
    CHECK_FALSE(host_header_ok("[::1]x", kNames));
    CHECK_FALSE(host_header_ok("::1", kNames));
}

TEST_CASE("origin header: absent, or this very page") {
    CHECK(origin_ok("", "192.168.1.20:8080"));
    CHECK(origin_ok("http://192.168.1.20:8080", "192.168.1.20:8080"));
    CHECK(origin_ok("http://ETUDE.local:8080", "etude.local:8080"));
    CHECK_FALSE(origin_ok("http://evil.example", "192.168.1.20:8080"));
    CHECK_FALSE(origin_ok("http://192.168.1.20:8081", "192.168.1.20:8080"));
    CHECK_FALSE(origin_ok("https://192.168.1.20:8080", "192.168.1.20:8080"));
    CHECK_FALSE(origin_ok("null", "192.168.1.20:8080"));
    CHECK_FALSE(origin_ok("http://", "192.168.1.20:8080"));
    CHECK_FALSE(origin_ok("http://192.168.1.20:8080/", "192.168.1.20:8080"));
    CHECK_FALSE(origin_ok("http://192.168.1.20:8080", ""));
}

TEST_CASE("listen addresses: only private LAN addresses, loopback when there is none") {
    const auto choose = [](std::vector<std::string> machine, bool all = false) {
        return choose_listen_addresses(machine, all);
    };
    using V = std::vector<std::string>;
    CHECK(choose({"192.168.1.20"}) == V{"192.168.1.20"});
    CHECK(choose({"10.0.0.5", "192.168.1.20"}) == V{"10.0.0.5", "192.168.1.20"});
    CHECK(choose({"172.16.0.1", "172.31.255.255", "169.254.3.4"}) ==
          V{"172.16.0.1", "172.31.255.255", "169.254.3.4"});
    CHECK(choose({"192.168.1.20", "192.168.1.20"}) == V{"192.168.1.20"}); // once
    // A public address, a VPN's, loopback, the wildcard and bad text are never listened on.
    CHECK(choose({"8.8.8.8", "100.92.33.39", "127.0.0.1", "0.0.0.0", "172.32.0.1", "172.15.0.1",
                  "fe80::1", "nonsense", "", "192.168.1"}) == V{"127.0.0.1"});
    CHECK(choose({"8.8.8.8", "192.168.2.1", "100.64.0.1"}) == V{"192.168.2.1"});
    CHECK(choose({}) == V{"127.0.0.1"});
    // Every interface only when asked for.
    CHECK(choose({"192.168.1.20"}, true) == V{"0.0.0.0"});
    CHECK(choose({}, true) == V{"0.0.0.0"});
}

TEST_CASE("private LAN addresses: the edges of each range") {
    const auto lan = [](std::array<std::uint8_t, 4> a) { return is_private_lan_address(a); };
    CHECK(lan({10, 0, 0, 0}));
    CHECK(lan({10, 255, 255, 255}));
    CHECK(lan({172, 16, 0, 0}));
    CHECK(lan({172, 31, 255, 255}));
    CHECK(lan({192, 168, 0, 0}));
    CHECK(lan({169, 254, 0, 1}));
    CHECK_FALSE(lan({172, 15, 255, 255}));
    CHECK_FALSE(lan({172, 32, 0, 0}));
    CHECK_FALSE(lan({192, 167, 1, 1}));
    CHECK_FALSE(lan({169, 253, 1, 1}));
    CHECK_FALSE(lan({127, 0, 0, 1}));
    CHECK_FALSE(lan({100, 64, 0, 1}));
    CHECK_FALSE(lan({0, 0, 0, 0}));
    CHECK_FALSE(is_private_lan_address(std::array<std::uint8_t, 16>{}));
    CHECK_FALSE(is_private_lan_address({}));
}

TEST_CASE("cookie header: the named cookie, and only it") {
    CHECK(cookie_value("tv_session=abc123", "tv_session") == "abc123");
    CHECK(cookie_value("a=1; tv_session=abc123; b=2", "tv_session") == "abc123");
    CHECK(cookie_value("a=1;tv_session=abc123", "tv_session") == "abc123");
    CHECK(cookie_value("tv_session=abc123 ", "tv_session") == "abc123");
    CHECK(cookie_value("x_tv_session=zzz", "tv_session").empty());
    CHECK(cookie_value("tv_session_x=zzz", "tv_session").empty());
    CHECK(cookie_value("a=1; b=2", "tv_session").empty());
    CHECK(cookie_value("", "tv_session").empty());
    CHECK(cookie_value("tv_session", "tv_session").empty());
    CHECK(cookie_value("tv_session=", "tv_session").empty());
    // The first one wins, so a second copy a script adds later cannot replace the real one.
    CHECK(cookie_value("tv_session=first; tv_session=second", "tv_session") == "first");
}
