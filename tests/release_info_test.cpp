// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/release_info.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace etude;

namespace {

const std::string kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string json(const std::string& version = "1.2.3", const std::string& file = "",
                 const std::string& extra = "") {
    const std::string name = file.empty() ? "etude-" + version + "-bookworm-arm64.tar.gz" : file;
    return "{\"version\": \"" + version + "\", \"os\": \"bookworm\", \"arch\": \"arm64\", "
           "\"min_os\": \"12\", \"file\": \"" + name + "\", \"sha256\": \"" + kHash +
           "\", \"size\": 48000000" + extra + "}";
}

// `text` with the first `from` replaced by `to`.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

} // namespace

TEST_CASE("release info: a good release.json is read") {
    const ReleaseInfoResult r = parse_release_info(json());
    REQUIRE(r.ok());
    CHECK(r.info.version == "1.2.3");
    CHECK(r.info.os == "bookworm");
    CHECK(r.info.arch == "arm64");
    CHECK(r.info.min_os == "12");
    CHECK(r.info.file == "etude-1.2.3-bookworm-arm64.tar.gz");
    CHECK(r.info.sha256 == kHash);
    CHECK(r.info.size == 48000000);
}

TEST_CASE("release info: the names the release script and the signer use") {
    CHECK(release_file_name("1.2.3", "bookworm", "arm64") == "etude-1.2.3-bookworm-arm64.tar.gz");
    const ReleaseInfoResult r = parse_release_info(json("2.0.0"));
    REQUIRE(r.ok());
    CHECK(release_trusted_comment(r.info) == "etude 2.0.0 bookworm arm64");
}

TEST_CASE("release info: spacing, escapes and other keys do not matter") {
    const std::string spaced = "\n {\n\t\"size\" : 1 ,\"sha256\":\"" + kHash +
                               "\",\"file\":\"etude-1.2.3-bookworm-arm64.tar.gz\","
                               "\"min_os\":\"12\",\"arch\":\"arm64\",\"os\":\"bookworm\","
                               "\"version\":\"1.2.\\u0033\",\"notes\":\"a\\nb\",\"build\":7}\n ";
    const ReleaseInfoResult r = parse_release_info(spaced);
    REQUIRE(r.ok());
    CHECK(r.info.version == "1.2.3");
    CHECK(r.info.size == 1);
}

TEST_CASE("release info: text that is not one flat JSON object is refused") {
    for (const char* bad : {"", "   ", "[]", "null", "{", "}", "{\"a\":}", "{\"a\" \"b\"}",
                            "{\"a\":\"b\",}", "{\"a\":\"b\"} x", "{\"a\":\"b\"}{\"a\":\"b\"}",
                            "{a:1}", "{\"a\":{\"b\":1}}", "{\"a\":[1]}", "{\"a\":true}",
                            "{\"a\":-1}", "{\"a\":01}", "{\"a\":1.5}", "{\"a\":\"\\q\"}",
                            "{\"a\":\"\\ud800\"}", "{\"a\":\"unterminated}"}) {
        CAPTURE(bad);
        CHECK(parse_release_info(bad).error == ReleaseInfoError::NotJson);
    }
    CHECK(parse_release_info(replaced(json(), "\"os\"", "\"version\"")).error ==
          ReleaseInfoError::NotJson); // a repeated key
}

TEST_CASE("release info: a file over the size limit is refused unread") {
    CHECK(parse_release_info(json("1.2.3", "", std::string(kMaxReleaseInfoBytes, ' '))).error ==
          ReleaseInfoError::TooLarge);
}

TEST_CASE("release info: a missing field is named") {
    for (const char* key : {"version", "os", "arch", "min_os", "file", "sha256", "size"}) {
        const std::string text = replaced(json(), std::string("\"") + key + "\"", "\"zzz\"");
        const ReleaseInfoResult r = parse_release_info(text);
        CAPTURE(key);
        CHECK(r.error == ReleaseInfoError::MissingField);
        CHECK(r.field == key);
    }
}

TEST_CASE("release info: a bad field is named") {
    const auto bad = [](const std::string& text, const char* field) {
        const ReleaseInfoResult r = parse_release_info(text);
        CAPTURE(text);
        CHECK(r.error == ReleaseInfoError::BadField);
        CHECK(r.field == field);
    };
    bad(json("v1.2.3"), "version");
    bad(json("1.2"), "version");
    bad(replaced(json(), "\"bookworm\"", "\"Book/worm\""), "os");
    bad(replaced(json(), "\"arm64\"", "\"\""), "arch");
    bad(replaced(json(), "\"12\"", "\"twelve\""), "min_os");
    bad(replaced(json(), "\"12\"", "\"12..1\""), "min_os");
    bad(replaced(json(), kHash, kHash.substr(1)), "sha256");
    bad(replaced(json(), kHash, "0123456789ABCDEF" + kHash.substr(16)), "sha256");
    bad(replaced(json(), "48000000", "0"), "size");
    bad(replaced(json(), "48000000", "99999999999"), "size");
    bad(replaced(json(), ": 48000000", ": \"48000000\""), "size");
    bad(replaced(json(), "\"version\": \"1.2.3\"", "\"version\": 123"), "version");
}

TEST_CASE("release info: the file name must be the one the rule gives, so it cannot point elsewhere") {
    for (const char* name : {"../etude-1.2.3-bookworm-arm64.tar.gz", "/etc/passwd",
                             "etude-1.2.4-bookworm-arm64.tar.gz", "other.tar.gz",
                             "etude-1.2.3-bookworm-arm64.tar.gz/x"}) {
        CAPTURE(name);
        const ReleaseInfoResult r = parse_release_info(json("1.2.3", name));
        CHECK(r.error == ReleaseInfoError::BadField);
        CHECK(r.field == "file");
    }
}

TEST_CASE("release info: the Mac's file is a zip, and its release-macos.json is read like any other") {
    CHECK(release_file_name("1.2.3", "macos", "arm64") == "etude-1.2.3-macos-arm64.zip");
    const std::string mac_json =
        "{\"version\": \"1.2.3\", \"os\": \"macos\", \"arch\": \"arm64\", \"min_os\": \"14\", "
        "\"file\": \"etude-1.2.3-macos-arm64.zip\", \"sha256\": \"" + kHash + "\", \"size\": 28000000}";
    const ReleaseInfoResult r = parse_release_info(mac_json);
    REQUIRE(r.ok());
    CHECK(release_trusted_comment(r.info) == "etude 1.2.3 macos arm64");
    // A zip name under another system, or a tarball name for the Mac, is not the rule's file.
    CHECK(parse_release_info(replaced(mac_json, "macos-arm64.zip", "macos-arm64.tar.gz")).error ==
          ReleaseInfoError::BadField);
    CHECK(parse_release_info(replaced(json(), "arm64.tar.gz", "arm64.zip")).error ==
          ReleaseInfoError::BadField);
}

TEST_CASE("release info: a prerelease version is never offered") {
    const ReleaseInfoResult r = parse_release_info(json("2.0.0-rc.1"));
    CHECK(r.error == ReleaseInfoError::BadField);
    CHECK(r.field == "version");
}

namespace {

// Raspberry Pi OS 13 on a Pi 5, unless a field is changed.
MachineOs pi(std::string_view version = "13") {
    MachineOs m;
    m.arch = "arm64";
    m.id = "debian";
    m.version_id = version;
    return m;
}

} // namespace

TEST_CASE("release info: a release fits a Debian system of its arch at least as new as min_os") {
    const ReleaseInfoResult r = parse_release_info(replaced(json(), "\"12\"", "\"13\""));
    REQUIRE(r.ok());
    CHECK(release_fits_machine(r.info, pi("13")));
    CHECK(release_fits_machine(r.info, pi("13.0")));
    CHECK(release_fits_machine(r.info, pi("13.1")));
    CHECK(release_fits_machine(r.info, pi("14")));
    CHECK_FALSE(release_fits_machine(r.info, pi("12")));
    CHECK_FALSE(release_fits_machine(r.info, pi("12.9")));
    CHECK_FALSE(release_fits_machine(r.info, pi("")));
    CHECK_FALSE(release_fits_machine(r.info, pi("trixie")));

    MachineOs raspbian = pi("13");
    raspbian.id = "raspbian";
    CHECK(release_fits_machine(r.info, raspbian));
    raspbian.version_id = "12";
    CHECK_FALSE(release_fits_machine(r.info, raspbian));
}

TEST_CASE("release info: min_os 13.0 and a machine on 13 are the same version") {
    const ReleaseInfoResult r = parse_release_info(replaced(json(), "\"12\"", "\"13.0\""));
    REQUIRE(r.ok());
    CHECK(release_fits_machine(r.info, pi("13")));
    CHECK_FALSE(release_fits_machine(r.info, pi("12.9.9")));
}

namespace {

// A Mac on macOS 14.5, unless a field is changed.
MachineOs mac(std::string_view version = "14.5") {
    MachineOs m;
    m.arch = "arm64";
    m.id = "macos";
    m.version_id = version;
    return m;
}

ReleaseInfo mac_release(const std::string& min_os = "14") {
    ReleaseInfo info;
    info.version = "1.2.3";
    info.os = "macos";
    info.arch = "arm64";
    info.min_os = min_os;
    info.file = release_file_name("1.2.3", "macos", "arm64");
    info.sha256 = kHash;
    return info;
}

} // namespace

TEST_CASE("release info: a Mac release fits a Mac on that macOS or a newer one, and nothing else") {
    CHECK(release_fits_machine(mac_release(), mac("14.5")));
    CHECK(release_fits_machine(mac_release(), mac("14")));
    CHECK(release_fits_machine(mac_release(), mac("15.1")));
    CHECK(release_fits_machine(mac_release(), mac("27.0.1")));
    CHECK_FALSE(release_fits_machine(mac_release(), mac("13.6.7")));
    CHECK_FALSE(release_fits_machine(mac_release(), mac("")));
    CHECK(release_fits_machine(mac_release("14.2"), mac("14.2.1")));
    CHECK_FALSE(release_fits_machine(mac_release("14.2"), mac("14.1")));
    MachineOs intel = mac();
    intel.arch = "amd64";
    CHECK_FALSE(release_fits_machine(mac_release(), intel));
    // Neither way round: a Debian release does not run on a Mac, and a Mac release not on a Pi.
    const ReleaseInfoResult pi_release = parse_release_info(replaced(json(), "\"12\"", "\"13\""));
    REQUIRE(pi_release.ok());
    CHECK_FALSE(release_fits_machine(pi_release.info, mac("27.0")));
    CHECK_FALSE(release_fits_machine(mac_release("13"), pi("13")));
    CHECK_FALSE(release_fits_machine(mac_release("1"), pi("13")));
}

TEST_CASE("release info: another architecture does not fit") {
    const ReleaseInfoResult r = parse_release_info(json());
    REQUIRE(r.ok());
    MachineOs x86 = pi("13");
    x86.arch = "x86_64";
    CHECK_FALSE(release_fits_machine(r.info, x86));
    x86.arch = "amd64";
    CHECK_FALSE(release_fits_machine(r.info, x86));
}

TEST_CASE("release info: Ubuntu does not fit, whatever its own version number is") {
    const ReleaseInfoResult r = parse_release_info(json());
    REQUIRE(r.ok());
    MachineOs ubuntu;
    ubuntu.arch = "arm64";
    ubuntu.id = "ubuntu";
    ubuntu.id_like = "debian";
    ubuntu.version_id = "24.04"; // a number past 12, which is not a Debian version
    ubuntu.debian_version = "trixie/sid";
    CHECK_FALSE(release_fits_machine(r.info, ubuntu));
    ubuntu.debian_version = "";
    CHECK_FALSE(release_fits_machine(r.info, ubuntu));
}

TEST_CASE("release info: a derivative fits by its Debian version, never by its own") {
    const ReleaseInfoResult r = parse_release_info(replaced(json(), "\"12\"", "\"13\""));
    REQUIRE(r.ok());
    MachineOs derivative;
    derivative.arch = "arm64";
    derivative.id = "somebase";
    derivative.id_like = "ubuntu debian";
    derivative.version_id = "99";
    derivative.debian_version = "13.1";
    CHECK(release_fits_machine(r.info, derivative));
    derivative.debian_version = "12.8";
    CHECK_FALSE(release_fits_machine(r.info, derivative));
    derivative.id_like = "ubuntu";
    derivative.debian_version = "13.1";
    CHECK_FALSE(release_fits_machine(r.info, derivative)); // not like Debian at all
    derivative.id_like = "notdebian debian-ish";
    CHECK_FALSE(release_fits_machine(r.info, derivative)); // whole words only
}

TEST_CASE("release info: a trusted comment is read back into its three fields") {
    ReleaseComment c;
    REQUIRE(parse_release_trusted_comment("etude 1.2.3 trixie arm64", c));
    CHECK(c.version == "1.2.3");
    CHECK(c.os == "trixie");
    CHECK(c.arch == "arm64");
    ReleaseInfo info;
    info.version = "0.10.2";
    info.os = "trixie";
    info.arch = "arm64";
    REQUIRE(parse_release_trusted_comment(release_trusted_comment(info), c));
    CHECK(c.version == "0.10.2");
    for (const char* bad : {"", "etude", "etude 1.2.3", "etude 1.2.3 trixie", "etude 1.2.3 trixie arm64 x",
                            "etude 1.2 trixie arm64", "etude 1.2.3-rc.1 trixie arm64", "tooncal 1.2.3 trixie arm64",
                            "etude 1.2.3  arm64", "etude 1.2.3 Trixie arm64", "etude 1.2.3 trixie arm-64",
                            "etude 1.2.3 trixie arm64\n", "etude  1.2.3 trixie arm64"}) {
        CAPTURE(bad);
        CHECK_FALSE(parse_release_trusted_comment(bad, c));
    }
}

TEST_CASE("release info: the trusted comment is taken from the signature file's text") {
    const std::string sig =
        "untrusted comment: signature from minisign secret key\nRUQabc==\n"
        "trusted comment: etude 1.2.3 trixie arm64\nglobalsig==\n";
    CHECK(signature_trusted_comment(sig) == "etude 1.2.3 trixie arm64");
    CHECK(signature_trusted_comment("a\r\ntrusted comment:x\r\n") == "x");
    CHECK(signature_trusted_comment("untrusted comment: only\n").empty());
    CHECK(signature_trusted_comment("").empty());
}
