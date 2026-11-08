// SPDX-License-Identifier: GPL-3.0-or-later
// Who may reach the upload server: only machines on the local network, asked by the Host and Origin
// headers a browser sends. Pure functions over bytes and text; the app turns a socket's address
// into bytes and passes the headers in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// True for an address that is not on the internet: loopback, private (10/8, 172.16/12, 192.168/16),
// link-local (169.254/16, fe80::/10) and unique local (fc00::/7) ones, and an IPv4 address inside
// an IPv6 one (::ffff:a.b.c.d) judged as the IPv4. `bytes` is 4 bytes (IPv4) or 16 (IPv6) in
// network order; any other length is not local.
bool is_local_address(std::span<const std::uint8_t> bytes);

// The address in text ("192.168.1.20", "::1", "fe80::1%en0") as bytes; false when it is not one.
bool parse_address(std::string_view text, std::uint8_t out[16], std::size_t& length);

// True when a request's Host header names this server and not some other site that points at it
// (DNS rebinding): an IP address, "localhost" or one of `names`, with or without ":port".
bool host_header_ok(std::string_view host, std::span<const std::string_view> names);

// True when a request's Origin header is absent (a plain navigation or curl) or is this very page:
// "http://" followed by the request's own Host. A form on another site fails.
bool origin_ok(std::string_view origin, std::string_view host);

// True for a private LAN IPv4 address a home router hands out: 10/8, 172.16/12, 192.168/16 and
// link-local 169.254/16. Not loopback, and not the carrier-grade or VPN range 100.64/10.
bool is_private_lan_address(std::span<const std::uint8_t> bytes);

// The addresses the upload server listens on, from the machine's own IPv4 addresses (text, as
// `local_ipv4_addresses()` gives them): each private LAN one once, in the order given; with none,
// loopback only; with `all_interfaces` (an explicit choice) the one wildcard "0.0.0.0". Text that
// is not an IPv4 address is skipped.
std::vector<std::string> choose_listen_addresses(std::span<const std::string> machine_addresses,
                                                 bool all_interfaces);

// The value of the cookie `name` in a Cookie header ("a=1; tv_session=abc; b=2"), or empty. The
// value is cut at ';' and spaces; a name that only ends the same ("x_tv_session") is not it.
std::string_view cookie_value(std::string_view header, std::string_view name);

} // namespace etude
