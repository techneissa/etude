// SPDX-License-Identifier: GPL-3.0-or-later
// scripts/release-pack-mac.sh and scripts/release-sign.sh on a Mac against a stand-in build folder: a
// build of another version than the tag is refused, and a zip signed with a test key passes the app's
// signature check. Only a Mac has the tools.
#include "upload_test_support.hpp"

#if defined(__APPLE__)

#include "../app/mac_update.hpp"

#include "etude/minisign.hpp"
#include "etude/release_info.hpp"
#include "etude/update_key.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

bool have(const std::string& tool) { return shell("command -v " + tool).status == 0; }

// A build folder as the script reads one: a program that reports its version, the piano samples and
// the sheet music fonts. The program is a one-line script, which is all `--version` needs.
fs::path fake_build(const fs::path& dir, const std::string& version) {
    const fs::path program = dir / "app/etude";
    fs::create_directories(program.parent_path());
    write_file(program, "#!/bin/sh\necho 'etude " + version + "'\n");
    ::chmod(program.c_str(), 0755);
    write_file(dir / "app/etude-piano.sf2", "sf2");
    write_file(dir / "app/verovio-data/Bravura.xml", "<xml/>");
    write_file(dir / "app/verovio-data/text/Times.xml", "<xml/>");
    return dir;
}

std::string pack_command(const fs::path& build, const fs::path& out, const std::string& extra = "") {
    return std::string("bash '") + ETUDE_SOURCE_DIR + "/scripts/release-pack-mac.sh' --no-build --build-dir '" +
           build.string() + "' --out '" + out.string() + "' --min-os 14 " + extra;
}

} // namespace

TEST_CASE("release pack mac: a build of another version than the tag is refused") {
    TempDir dir;
    const fs::path build = fake_build(dir.path / "build", "1.2.3");
    const ShellRun packed = shell(pack_command(build, dir.path / "out", "--expect-version 9.9.9"));
    CHECK(packed.status != 0);
    CHECK(packed.out.find("9.9.9 was expected") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path / "out/etude-1.2.3-macos-arm64.zip"));
}

TEST_CASE("release pack mac: the zip, signed with a test key by release-sign.sh, passes the app's check") {
    if (!have("minisign") || !have("sha256sum")) {
        MESSAGE("minisign or sha256sum is not installed: the signing step is not run");
        return;
    }
    TempDir dir;
    const fs::path build = fake_build(dir.path / "build", "1.2.3");
    REQUIRE(shell(pack_command(build, dir.path / "out")).status == 0);

    const fs::path pub = dir.path / "test.pub";
    const fs::path sec = dir.path / "test.key";
    REQUIRE(shell("minisign -G -W -f -p '" + pub.string() + "' -s '" + sec.string() + "'").status == 0);
    std::string public_key = read_file(pub);
    public_key = public_key.substr(public_key.find('\n') + 1);
    public_key = public_key.substr(0, public_key.find('\n'));
    REQUIRE(public_key.size() == 56);
    const fs::path signer = dir.path / "tree/scripts/release-sign.sh";
    write_file(signer, read_file(fs::path(ETUDE_SOURCE_DIR) / "scripts/release-sign.sh"));
    ::chmod(signer.c_str(), 0755);
    write_file(dir.path / "tree/core/src/update_key.cpp",
               "constexpr std::string_view kUpdatePublicKey = \"" + public_key + "\";\n");
    const ShellRun signed_run = shell("MINISIGN_PASSWORD= bash '" + signer.string() + "' '" +
                                       (dir.path / "out").string() + "' '" + sec.string() +
                                       "' --info release-macos.json");
    INFO(signed_run.out);
    REQUIRE(signed_run.status == 0);

    const std::string name = "etude-1.2.3-macos-arm64.zip";
    const std::string bytes = read_file(dir.path / "out" / name);
    const std::string signature = read_file(dir.path / "out" / (name + ".minisig"));
    const ReleaseInfoResult info = parse_release_info(read_file(dir.path / "out/release-macos.json"));
    REQUIRE(info.ok());
    CHECK(release_trusted_comment(info.info) == "etude 1.2.3 macos arm64");
    const std::span<const std::uint8_t> data(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());

    const SignatureCheck whole = verify_minisign(data, signature, public_key, release_trusted_comment(info.info));
    CHECK(whole.ok());
    CHECK(whole.trusted_comment == "etude 1.2.3 macos arm64");
    Digest512 digest{};
    REQUIRE(digest_of_file(dir.path / "out" / name, digest));
    CHECK(verify_minisign_digest(digest, signature, public_key, release_trusted_comment(info.info)).ok());
    CHECK_FALSE(verify_release_signature(data, signature, release_trusted_comment(info.info)).ok());
    CHECK_FALSE(verify_release_digest(digest, signature, release_trusted_comment(info.info)).ok());
    std::string changed = bytes;
    changed[changed.size() / 2] ^= 1;
    CHECK_FALSE(verify_minisign({reinterpret_cast<const std::uint8_t*>(changed.data()), changed.size()},
                                signature, public_key, release_trusted_comment(info.info))
                    .ok());
    CHECK_FALSE(verify_minisign(data, signature, public_key, "etude 1.2.3 trixie arm64").ok());
    CHECK(minisign_is_prehashed(signature));
}

#endif // __APPLE__
