// SPDX-License-Identifier: GPL-3.0-or-later
// scripts/release-pack-mac.sh and scripts/release-sign.sh, run on a Mac against a stand-in build
// folder: a build of another version than the tag is refused; signed with a key made for the test,
// the zip is accepted by the app's signature check from the bytes and from their hash, and the
// script signs only a folder with exactly one file of its kind. Only a Mac has the tools.
#include "upload_test_support.hpp"

#if defined(__APPLE__)

#include "../app/mac_update.hpp"

#include "etude/minisign.hpp"
#include "etude/release_info.hpp"
#include "etude/update_key.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

struct Run {
    int status = -1;
    std::string out;
};

Run run_command(const std::string& command) {
    Run run;
    FILE* pipe = ::popen((command + " 2>&1 </dev/null").c_str(), "r");
    REQUIRE(pipe != nullptr);
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
        run.out += buffer;
    }
    const int raw = ::pclose(pipe);
    run.status = raw == -1 ? -1 : WEXITSTATUS(raw);
    return run;
}

bool have(const std::string& tool) { return run_command("command -v " + tool).status == 0; }

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
    const Run packed = run_command(pack_command(build, dir.path / "out", "--expect-version 9.9.9"));
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
    REQUIRE(run_command(pack_command(build, dir.path / "out")).status == 0);

    // A key pair made for this test, and a copy of the signing script beside a source file that holds
    // its public half, since the script signs only with the key built into the app.
    const fs::path pub = dir.path / "test.pub";
    const fs::path sec = dir.path / "test.key";
    REQUIRE(run_command("minisign -G -W -f -p '" + pub.string() + "' -s '" + sec.string() + "'").status == 0);
    std::string public_key = read_file(pub);
    public_key = public_key.substr(public_key.find('\n') + 1);
    public_key = public_key.substr(0, public_key.find('\n'));
    REQUIRE(public_key.size() == 56);
    const fs::path signer = dir.path / "tree/scripts/release-sign.sh";
    write_file(signer, read_file(fs::path(ETUDE_SOURCE_DIR) / "scripts/release-sign.sh"));
    ::chmod(signer.c_str(), 0755);
    write_file(dir.path / "tree/core/src/update_key.cpp",
               "constexpr std::string_view kUpdatePublicKey = \"" + public_key + "\";\n");
    const Run signed_run = run_command("MINISIGN_PASSWORD= bash '" + signer.string() + "' '" +
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

    // The same check verify_release_signature makes, with the test key in place of the built-in one.
    const SignatureCheck whole = verify_minisign(data, signature, public_key, release_trusted_comment(info.info));
    CHECK(whole.ok());
    CHECK(whole.trusted_comment == "etude 1.2.3 macos arm64");
    // And the way the app does it for a download: a hash read from the saved file in pieces.
    Digest512 digest{};
    REQUIRE(digest_of_file(dir.path / "out" / name, digest));
    CHECK(verify_minisign_digest(digest, signature, public_key, release_trusted_comment(info.info)).ok());
    // The built-in key is another one: this zip is not accepted as a release.
    CHECK_FALSE(verify_release_signature(data, signature, release_trusted_comment(info.info)).ok());
    CHECK_FALSE(verify_release_digest(digest, signature, release_trusted_comment(info.info)).ok());
    // A changed byte, and a comment of another system, do not pass with the right key either.
    std::string changed = bytes;
    changed[changed.size() / 2] ^= 1;
    CHECK_FALSE(verify_minisign({reinterpret_cast<const std::uint8_t*>(changed.data()), changed.size()},
                                signature, public_key, release_trusted_comment(info.info))
                    .ok());
    CHECK_FALSE(verify_minisign(data, signature, public_key, "etude 1.2.3 trixie arm64").ok());
    // The signature is the prehashed kind, which the app can check from a hash.
    CHECK(minisign_is_prehashed(signature));
}

TEST_CASE("release pack mac: the signing script wants exactly one file of its kind in the folder") {
    if (!have("minisign") || !have("sha256sum")) {
        MESSAGE("minisign or sha256sum is not installed: the signing guard is not run");
        return;
    }
    TempDir dir;
    // A key and a copy of the script beside a source that holds its public half, as above.
    const fs::path pub = dir.path / "test.pub";
    const fs::path sec = dir.path / "test.key";
    REQUIRE(run_command("minisign -G -W -f -p '" + pub.string() + "' -s '" + sec.string() + "'").status == 0);
    std::string public_key = read_file(pub);
    public_key = public_key.substr(public_key.find('\n') + 1);
    public_key = public_key.substr(0, public_key.find('\n'));
    const fs::path signer = dir.path / "tree/scripts/release-sign.sh";
    write_file(signer, read_file(fs::path(ETUDE_SOURCE_DIR) / "scripts/release-sign.sh"));
    ::chmod(signer.c_str(), 0755);
    write_file(dir.path / "tree/core/src/update_key.cpp",
               "constexpr std::string_view kUpdatePublicKey = \"" + public_key + "\";\n");
    const auto sign = [&](const fs::path& folder, const std::string& info) {
        return run_command("MINISIGN_PASSWORD= bash '" + signer.string() + "' '" + folder.string() + "' '" +
                           sec.string() + "' " + info);
    };

    // The Pi's run: one tarball is signed, a second one beside it stops the run before any signing.
    const std::string tar = "etude-1.2.3-trixie-arm64.tar.gz";
    const fs::path pi = dir.path / "pi";
    write_file(pi / tar, "tarball");
    const Run hash = run_command("shasum -a 256 '" + (pi / tar).string() + "'");
    const std::string digest = hash.out.substr(0, 64);
    write_file(pi / (tar + ".sha256"), digest + "  " + tar + "\n");
    write_file(pi / "release.json",
               "{\n  \"version\": \"1.2.3\",\n  \"os\": \"trixie\",\n  \"arch\": \"arm64\",\n  \"min_os\": \"13\",\n"
               "  \"file\": \"" + tar + "\",\n  \"sha256\": \"" + digest + "\",\n  \"size\": 7\n}\n");
    const Run one = sign(pi, "");
    INFO(one.out);
    CHECK(one.status == 0);
    CHECK(fs::exists(pi / (tar + ".minisig")));
    fs::remove(pi / (tar + ".minisig"));
    write_file(pi / "etude-1.2.2-trixie-arm64.tar.gz", "an older tarball");
    const Run two = sign(pi, "");
    CHECK(two.status != 0);
    CHECK(two.out.find("expected one etude-*.tar.gz") != std::string::npos);
    CHECK_FALSE(fs::exists(pi / (tar + ".minisig")));

    // The Mac's run wants exactly one zip the same way.
    const fs::path out = dir.path / "out";
    const fs::path build = fake_build(dir.path / "build", "1.2.3");
    REQUIRE(run_command(pack_command(build, out)).status == 0);
    write_file(out / "etude-1.2.2-macos-arm64.zip", "an older zip");
    const Run zips = sign(out, "--info release-macos.json");
    CHECK(zips.status != 0);
    CHECK(zips.out.find("expected one etude-*.zip") != std::string::npos);
    fs::remove(out / "etude-1.2.2-macos-arm64.zip");
    CHECK(sign(out, "--info release-macos.json").status == 0);
}

#endif // __APPLE__
