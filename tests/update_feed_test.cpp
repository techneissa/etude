// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_feed.hpp"

#include "etude/machine_os.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

using namespace etude;

namespace {

const std::string kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const std::string kBase = "https://github.com/techneissa/etude/releases/download/v1.2.3/";

std::string asset(const std::string& name, const std::string& url = "", int size = 100) {
    return "{\"name\": \"" + name + "\", \"size\": " + std::to_string(size) +
           ", \"state\": \"uploaded\", \"browser_download_url\": \"" +
           (url.empty() ? kBase + name : url) + "\"}";
}

std::string feed(const std::string& assets, const std::string& tag = "v1.2.3",
                 const std::string& extra = "") {
    return "{\"url\": \"https://api.github.com/x\", \"tag_name\": \"" + tag +
           "\", \"draft\": false, \"prerelease\": false, \"body\": \"## Fixes\\n* Faster start\", "
           "\"author\": {\"login\": \"techneissa\", \"site_admin\": false}, \"assets\": [" +
           assets + "]" + extra + "}";
}

const std::string kTar = "etude-1.2.3-trixie-arm64.tar.gz";

std::string full_assets() {
    return asset("release.json") + "," + asset(kTar) + "," + asset(kTar + ".minisig") + "," +
           asset(kTar + ".sha256");
}

ReleaseInfo info(const std::string& version = "1.2.3") {
    ReleaseInfo i;
    i.version = version;
    i.os = "trixie";
    i.arch = "arm64";
    i.min_os = "13";
    i.file = release_file_name(version, "trixie", "arm64");
    i.sha256 = kHash;
    i.size = 100;
    return i;
}

MachineOsText pi() {
    MachineOsText m;
    m.arch = "arm64";
    m.id = "debian";
    m.version_id = "13";
    return m;
}

} // namespace

TEST_CASE("update feed: a latest release is read") {
    const LatestReleaseResult r = parse_latest_release(feed(full_assets()));
    REQUIRE(r.ok());
    CHECK(r.release.tag == "v1.2.3");
    CHECK(r.release.version == "1.2.3");
    CHECK(r.release.notes == "## Fixes\n* Faster start");
    CHECK(r.release.page_url == "https://github.com/techneissa/etude/releases/tag/v1.2.3");
    REQUIRE(r.release.assets.size() == 4);
    CHECK(r.release.assets[1].name == kTar);
    CHECK(r.release.assets[1].url == kBase + kTar);
    CHECK(r.release.assets[1].size == 100);
}

TEST_CASE("update feed: what is not a plain release is refused") {
    CHECK(parse_latest_release("").error == FeedError::NotJson);
    CHECK(parse_latest_release("[]").error == FeedError::NotJson);
    CHECK(parse_latest_release("{\"tag_name\": \"v1.0.0\"").error == FeedError::NotJson);
    CHECK(parse_latest_release("{\"tag_name\": \"v1.0.0\"} x").error == FeedError::NotJson);
    CHECK(parse_latest_release("{\"name\": \"x\"}").error == FeedError::MissingField);
    CHECK(parse_latest_release(feed("", "1.2.3")).error == FeedError::BadField);
    CHECK(parse_latest_release(feed("", "v1.2")).error == FeedError::BadField);
    CHECK(parse_latest_release(feed("", "v1.2.3-rc.1")).error == FeedError::BadField);
    CHECK(parse_latest_release(feed("", "v01.2.3")).error == FeedError::BadField);
    CHECK(parse_latest_release("{\"tag_name\": \"v1.2.3\", \"draft\": true}").error ==
          FeedError::Withdrawn);
    CHECK(parse_latest_release("{\"tag_name\": \"v1.2.3\", \"prerelease\": true}").error ==
          FeedError::Withdrawn);
    CHECK(parse_latest_release(std::string(kMaxFeedBytes + 1, ' ')).error == FeedError::TooLarge);
}

TEST_CASE("update feed: nesting is bounded and escapes are read") {
    std::string deep = "{\"tag_name\": \"v1.2.3\", \"x\": ";
    for (int i = 0; i < 40; ++i) {
        deep += "[";
    }
    CHECK(parse_latest_release(deep).error == FeedError::NotJson);
    const LatestReleaseResult r = parse_latest_release(
        "{\"tag_name\": \"v1.2.3\", \"body\": \"caf\\u00e9 \\ud83c\\udfb9 \\\"x\\\"\\n\"}");
    REQUIRE(r.ok());
    CHECK(r.release.notes == "caf\xC3\xA9 \xF0\x9F\x8E\xB9 \"x\"\n");
    CHECK(parse_latest_release("{\"tag_name\": \"v1.2.3\", \"body\": \"\\ud83c\"}").error ==
          FeedError::NotJson);
    CHECK(parse_latest_release("{\"tag_name\": \"v1.2.3\", \"body\": \"\\q\"}").error ==
          FeedError::NotJson);
}

TEST_CASE("update feed: an asset must be this repository's download of that name") {
    const std::string other = "https://github.com/other/etude/releases/download/v1.2.3/a.tar.gz";
    const std::string wrong_tag =
        "https://github.com/techneissa/etude/releases/download/v9.9.9/a.tar.gz";
    const std::string moved = kBase + "b.tar.gz";
    const std::string http = "http://github.com/techneissa/etude/releases/download/v1.2.3/a";
    const std::string evil = kBase + "../../../../evil/a";
    const std::string assets = asset("a.tar.gz", other) + "," + asset("a.tar.gz", wrong_tag) + "," +
                               asset("a.tar.gz", moved) + "," + asset("a", http) + "," +
                               asset("a", evil) + "," + asset("../a", kBase + "../a") + "," +
                               asset("good.txt");
    const LatestReleaseResult r = parse_latest_release(feed(assets));
    REQUIRE(r.ok());
    REQUIRE(r.release.assets.size() == 1);
    CHECK(r.release.assets[0].name == "good.txt");
}

TEST_CASE("update feed: a bad item in the assets does not hide the good ones after it") {
    const std::string assets = "null, 7, \"text\", [1], " + asset("good.txt");
    const LatestReleaseResult r = parse_latest_release(feed(assets));
    REQUIRE(r.ok());
    REQUIRE(r.release.assets.size() == 1);
    CHECK(r.release.assets[0].name == "good.txt");
}

TEST_CASE("update feed: the release's page is built from the tag, not taken from the answer") {
    const LatestReleaseResult r =
        parse_latest_release(feed("", "v2.0.0", ", \"html_url\": \"https://evil.example/x\""));
    REQUIRE(r.ok());
    CHECK(r.release.page_url == "https://github.com/techneissa/etude/releases/tag/v2.0.0");
}

TEST_CASE("update feed: the hosts are the API, the downloads and GitHub's asset hosts") {
    const auto hosts = update_fetch_hosts();
    const auto has = [&](std::string_view h) {
        return std::find(hosts.begin(), hosts.end(), h) != hosts.end();
    };
    CHECK(has("api.github.com"));
    CHECK(has("github.com"));
    CHECK(has("release-assets.githubusercontent.com"));
    CHECK(has("objects.githubusercontent.com"));
    CHECK_FALSE(has("raw.githubusercontent.com"));
    CHECK_FALSE(has("example.com"));
}

TEST_CASE("update feed: the assets for this machine") {
    const LatestReleaseResult r = parse_latest_release(feed(full_assets()));
    REQUIRE(r.ok());
    const MachineOsText machine = pi();
    const AssetPick ok = pick_release_assets(r.release, info(), machine.view());
    REQUIRE(ok.result == PickResult::Ok);
    CHECK(ok.tarball.name == kTar);
    CHECK(ok.signature.name == kTar + ".minisig");
    CHECK(ok.signature.url == kBase + kTar + ".minisig");
}

TEST_CASE("update feed: a release is refused for the wrong version, machine or missing files") {
    const LatestReleaseResult r = parse_latest_release(feed(full_assets()));
    REQUIRE(r.ok());
    MachineOsText machine = pi();
    CHECK(pick_release_assets(r.release, info("1.2.4"), machine.view()).result ==
          PickResult::WrongVersion);
    machine.version_id = "12";
    CHECK(pick_release_assets(r.release, info(), machine.view()).result ==
          PickResult::NotThisMachine);
    machine = pi();
    machine.arch = "amd64";
    CHECK(pick_release_assets(r.release, info(), machine.view()).result ==
          PickResult::NotThisMachine);
    machine = pi();
    machine.id = "ubuntu";
    machine.id_like = "debian";
    machine.version_id = "24.04";
    CHECK(pick_release_assets(r.release, info(), machine.view()).result ==
          PickResult::NotThisMachine);

    machine = pi();
    const LatestReleaseResult no_tar = parse_latest_release(feed(asset("release.json")));
    CHECK(pick_release_assets(no_tar.release, info(), machine.view()).result ==
          PickResult::MissingTarball);
    const LatestReleaseResult no_sig =
        parse_latest_release(feed(asset("release.json") + "," + asset(kTar)));
    CHECK(pick_release_assets(no_sig.release, info(), machine.view()).result ==
          PickResult::MissingSignature);
    // Another system's tarball under another name is not this release's file.
    const std::string other = "etude-1.2.3-bookworm-arm64.tar.gz";
    const LatestReleaseResult wrong_file =
        parse_latest_release(feed(asset(other) + "," + asset(other + ".minisig")));
    CHECK(pick_release_assets(wrong_file.release, info(), machine.view()).result ==
          PickResult::MissingTarball);
}

namespace {

const std::string kZip = "etude-1.2.3-macos-arm64.zip";

ReleaseInfo mac_info(const std::string& version = "1.2.3") {
    ReleaseInfo i;
    i.version = version;
    i.os = "macos";
    i.arch = "arm64";
    i.min_os = "14";
    i.file = release_file_name(version, "macos", "arm64");
    i.sha256 = kHash;
    i.size = 100;
    return i;
}

MachineOsText mac(const std::string& version = "14.5") {
    MachineOsText m;
    m.arch = "arm64";
    m.id = "macos";
    m.version_id = version;
    return m;
}

std::string mac_assets() {
    return full_assets() + "," + asset("release-macos.json") + "," + asset(kZip) + "," +
           asset(kZip + ".minisig") + "," + asset(kZip + ".sha256");
}

} // namespace

TEST_CASE("update feed: a Mac reads release-macos.json, every other system release.json") {
    CHECK(release_info_asset_for(mac().view()) == "release-macos.json");
    CHECK(release_info_asset_for(pi().view()) == "release.json");
    CHECK(release_info_asset_for(MachineOsText().view()) == "release.json");
    const LatestReleaseResult r = parse_latest_release(feed(mac_assets()));
    REQUIRE(r.ok());
    CHECK(find_release_asset(r.release, release_info_asset_for(mac().view())) != nullptr);
    CHECK(find_release_asset(r.release, release_info_asset_for(pi().view())) != nullptr);
}

TEST_CASE("update feed: the assets for a Mac are the zip and its signature") {
    const LatestReleaseResult r = parse_latest_release(feed(mac_assets()));
    REQUIRE(r.ok());
    const MachineOsText machine = mac();
    const AssetPick ok = pick_release_assets(r.release, mac_info(), machine.view());
    REQUIRE(ok.result == PickResult::Ok);
    CHECK(ok.tarball.name == kZip);
    CHECK(ok.tarball.url == kBase + kZip);
    CHECK(ok.signature.name == kZip + ".minisig");
}

TEST_CASE("update feed: a Mac is refused a release for the wrong version, macOS, arch or system") {
    const LatestReleaseResult r = parse_latest_release(feed(mac_assets()));
    REQUIRE(r.ok());
    CHECK(pick_release_assets(r.release, mac_info("1.2.4"), mac().view()).result == PickResult::WrongVersion);
    // Older macOS than the release's min_os, and a Mac of another architecture.
    CHECK(pick_release_assets(r.release, mac_info(), mac("13.6").view()).result == PickResult::NotThisMachine);
    MachineOsText intel = mac();
    intel.arch = "amd64";
    CHECK(pick_release_assets(r.release, mac_info(), intel.view()).result == PickResult::NotThisMachine);
    // The Mac's zip is never picked for a Pi, and the Pi's tarball never for a Mac.
    CHECK(pick_release_assets(r.release, mac_info(), pi().view()).result == PickResult::NotThisMachine);
    CHECK(pick_release_assets(r.release, info(), mac().view()).result == PickResult::NotThisMachine);
    // The zip not uploaded yet, or its signature missing.
    const LatestReleaseResult no_zip = parse_latest_release(feed(asset("release-macos.json")));
    CHECK(pick_release_assets(no_zip.release, mac_info(), mac().view()).result == PickResult::MissingTarball);
    const LatestReleaseResult no_sig =
        parse_latest_release(feed(asset("release-macos.json") + "," + asset(kZip)));
    CHECK(pick_release_assets(no_sig.release, mac_info(), mac().view()).result == PickResult::MissingSignature);
}

TEST_CASE("update feed: find_release_asset") {
    const LatestReleaseResult r = parse_latest_release(feed(full_assets()));
    REQUIRE(r.ok());
    REQUIRE(find_release_asset(r.release, kReleaseInfoAsset) != nullptr);
    CHECK(find_release_asset(r.release, "nothing") == nullptr);
}
