// SPDX-License-Identifier: GPL-3.0-or-later
// Reading the latest release of the public repository for the update check: GitHub's answer for
// `releases/latest`, the release's notes, and the choice of the files to download. Pure text in,
// plain structs out: no file system, no network. The fetching is app/update_service.hpp's.
//
// Nothing here is trusted by itself. Every address that leaves this file must have the shape of a
// download of this repository's own release, and the downloaded tarball is trusted only through
// its minisign signature (update_key.hpp).
#pragma once

#include "etude/release_info.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

inline constexpr std::string_view kRepositoryUrl = "https://github.com/techneissa/etude";
inline constexpr std::string_view kBuyMeACoffeeUrl = "https://buymeacoffee.com/techneissa";
inline constexpr std::string_view kLatestReleaseUrl =
    "https://api.github.com/repos/techneissa/etude/releases/latest";
// The file of a release that says which tarball it holds (release_info.hpp).
inline constexpr std::string_view kReleaseInfoAsset = "release.json";
// The same for the Mac's zip. It is a file of its own so that the Pi's release.json, which versions
// already out read, never describes another system's file.
inline constexpr std::string_view kReleaseInfoAssetMac = "release-macos.json";

// The release-info file to read on this machine: kReleaseInfoAssetMac on a Mac, else kReleaseInfoAsset.
std::string_view release_info_asset_for(const MachineOs& machine);

// The only hosts the update check and download may reach: the API, GitHub's download address, and
// the hosts GitHub sends a release asset's redirect to (its current one and the older ones).
std::span<const std::string_view> update_fetch_hosts();

struct ReleaseAsset {
    std::string name;
    std::string url;
    std::uint64_t size = 0;
};

struct LatestRelease {
    std::string tag;      // "v1.2.3"
    std::string version;  // "1.2.3"
    std::string notes;    // the release's text, as written
    std::string page_url; // this repository's page for the release, built from the tag
    std::vector<ReleaseAsset> assets;
};

enum class FeedError : std::uint8_t {
    None,
    TooLarge,     // over kMaxFeedBytes
    NotJson,      // not JSON, nested too deep, or not an object
    MissingField, // tag_name is absent
    BadField,     // a tag that is not v<major>.<minor>.<patch>
    Withdrawn,    // a draft or a prerelease: never offered
};

struct LatestReleaseResult {
    LatestRelease release;
    FeedError error = FeedError::None;
    std::string field;

    bool ok() const { return error == FeedError::None; }
};

inline constexpr std::size_t kMaxFeedBytes = 512 * 1024;

// Reads the answer of `releases/latest`: tag_name, body, draft, prerelease and assets
// (name, size, browser_download_url). An asset whose address is not the download of that name from
// this repository's release `tag` is left out, so every address in the result is one we may fetch.
LatestReleaseResult parse_latest_release(std::string_view json);

// The asset called `name`, or null.
const ReleaseAsset* find_release_asset(const LatestRelease& release, std::string_view name);

// What the release holds for this machine, given the release.json it announced.
enum class PickResult : std::uint8_t {
    Ok,
    WrongVersion,     // release.json is for another version than the release's tag
    NotThisMachine,   // built for another arch, or a system this one is not (release_fits_machine)
    MissingTarball,   // the release has no file of the name release.json gives
    MissingSignature, // ... or no .minisig for it
};

struct AssetPick {
    PickResult result = PickResult::MissingTarball;
    ReleaseAsset tarball;
    ReleaseAsset signature;
};

AssetPick pick_release_assets(const LatestRelease& release, const ReleaseInfo& info,
                              const MachineOs& machine);

// The first lines of a release's notes, as plain text for a small card: markdown marks (headings,
// bullets, emphasis, code ticks, links, a pull request's "by @who in <address>" tail) dropped,
// blank lines and the generated headings and changelog line skipped, at most `max_lines` lines of
// at most `max_chars` characters (a longer one is cut at a word with an ellipsis).
std::vector<std::string> release_notes_lines(std::string_view notes, std::size_t max_lines,
                                             std::size_t max_chars);

} // namespace etude
