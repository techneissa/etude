// SPDX-License-Identifier: GPL-3.0-or-later
// The app's side of an update: signatures, the key, tar and release files, the feed, the service and
// install hand-over, the helper's settings, record and unpack, and a Mac's swap and install (ditto,
// codesign and plutil stood in for; a folder stands in for GitHub). The root helper is in helper_test.cpp.
#include "../app/mac_relaunch_script.hpp"
#include "../app/mac_update.hpp"
#include "../app/update_files.hpp"
#include "../app/update_service.hpp"
#include "../app/update_start.hpp"
#include "../helper/unpack.hpp"
#include "upload_test_support.hpp"

#include "etude/base64.hpp"
#include "etude/data_folder.hpp"
#include "etude/mac_install.hpp"
#include "etude/machine_os.hpp"
#include "etude/minisign.hpp"
#include "etude/release_info.hpp"
#include "etude/semver.hpp"
#include "etude/tar_archive.hpp"
#include "etude/update_conf.hpp"
#include "etude/update_feed.hpp"
#include "etude/update_handoff.hpp"
#include "etude/update_key.hpp"
#include "etude/update_state.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

// A tar archive in memory, entry by entry, with real checksums.
class Tar {
  public:
    Tar& add(const std::string& name, char type = '0', const std::string& data = "",
             const std::string& link = "", unsigned mode = 0644) {
        std::vector<std::uint8_t> h(512, 0);
        const auto put = [&](std::size_t at, const std::string& s, std::size_t max) {
            std::memcpy(h.data() + at, s.data(), std::min(s.size(), max));
        };
        const auto octal = [&](std::size_t at, std::size_t width, std::uint64_t v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%0*llo", static_cast<int>(width - 1),
                          static_cast<unsigned long long>(v));
            std::memcpy(h.data() + at, buf, width - 1);
        };
        put(0, name, 100);
        octal(100, 8, mode);
        octal(108, 8, 0);
        octal(116, 8, 0);
        octal(124, 12, data.size());
        octal(136, 12, 0);
        h[156] = static_cast<std::uint8_t>(type);
        put(157, link, 100);
        std::memcpy(h.data() + 257, "ustar  ", 8);
        std::memset(h.data() + 148, ' ', 8);
        unsigned sum = 0;
        for (const std::uint8_t b : h) {
            sum += b;
        }
        char buf[16];
        std::snprintf(buf, sizeof buf, "%06o", sum);
        std::memcpy(h.data() + 148, buf, 6);
        h[154] = 0;
        h[155] = ' ';
        bytes_.insert(bytes_.end(), h.begin(), h.end());
        bytes_.insert(bytes_.end(), data.begin(), data.end());
        bytes_.insert(bytes_.end(), (512 - data.size() % 512) % 512, 0);
        return *this;
    }
    Tar& dir(const std::string& name) { return add(name + "/", '5', "", "", 0755); }
    Tar& file(const std::string& name, const std::string& data = "x", unsigned mode = 0644) {
        return add(name, '0', data, "", mode);
    }
    Tar& symlink(const std::string& name, const std::string& target) {
        return add(name, '2', "", target, 0777);
    }
    std::vector<std::uint8_t> done() const {
        std::vector<std::uint8_t> out = bytes_;
        out.insert(out.end(), 1024, 0);
        return out;
    }
    const std::vector<std::uint8_t>& raw() const { return bytes_; }

  private:
    std::vector<std::uint8_t> bytes_;
};

// A gzip member holding `data` in stored blocks (at most 65535 bytes each), with a real CRC-32.
std::vector<std::uint8_t> gzip_stored(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out{0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, data.size() - at);
        const bool last = at + n == data.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(n & 0xff));
        out.push_back(static_cast<std::uint8_t>(n >> 8));
        out.push_back(static_cast<std::uint8_t>(~n & 0xff));
        out.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xff));
        out.insert(out.end(), data.begin() + static_cast<long>(at),
                   data.begin() + static_cast<long>(at + n));
        at += n;
    } while (at < data.size());
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t b : data) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xedb88320u & (~(crc & 1) + 1));
        }
    }
    crc = ~crc;
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(crc >> (8 * i)));
    }
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(data.size() >> (8 * i)));
    }
    return out;
}

constexpr std::uint8_t kSampleTarGzBytes[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xed, 0x99, 0xdd, 0x6a, 0x83, 0x30, 0x18, 0x86, 0x3d, 0xee,
    0x55, 0x64, 0xec, 0x58, 0x63, 0xfe, 0xf4, 0x74, 0x30, 0xca, 0x18, 0xc8, 0x18, 0xec, 0x0a, 0xd2, 0x36, 0xad, 0xb2, 0x36,
    0x29, 0x26, 0x96, 0x5d, 0xfe, 0x6c, 0x0e, 0x46, 0xb1, 0x22, 0xeb, 0x5a, 0x75, 0x9b, 0xdf, 0x73, 0x12, 0xf5, 0xf4, 0xf1,
    0x7d, 0xf3, 0x45, 0x95, 0xab, 0x56, 0x2a, 0x24, 0x11, 0x8d, 0x18, 0x0e, 0x7a, 0x22, 0xae, 0x49, 0x85, 0xf0, 0x6b, 0x4d,
    0x73, 0x6d, 0xb9, 0x4e, 0xb9, 0xa0, 0x01, 0x12, 0xc1, 0x00, 0x54, 0xd6, 0xc9, 0x12, 0xa1, 0x60, 0xa2, 0xa8, 0x13, 0xff,
    0x8b, 0x42, 0xe3, 0x5f, 0xe1, 0x9f, 0xc4, 0x94, 0x82, 0xff, 0x51, 0xfc, 0xfb, 0xfb, 0xa1, 0xfd, 0x33, 0xd6, 0xf0, 0x4f,
    0xe8, 0x31, 0xff, 0x31, 0xf8, 0xef, 0x9d, 0xfb, 0x3b, 0xaf, 0xdd, 0xe6, 0x33, 0xb5, 0xcc, 0x0d, 0xf2, 0xfa, 0x91, 0x7f,
    0x1d, 0x66, 0x01, 0x30, 0xad, 0xfc, 0xdb, 0x5c, 0x96, 0xaa, 0x87, 0x1d, 0xe0, 0x07, 0xfd, 0x2f, 0x04, 0x87, 0xfe, 0x1f,
    0xc7, 0xbf, 0x7f, 0x82, 0xc7, 0xf5, 0x4f, 0x92, 0x04, 0xf6, 0xff, 0x51, 0xfd, 0xef, 0x0b, 0xa9, 0x4d, 0x64, 0xd7, 0xf4,
    0x46, 0xfe, 0x13, 0xce, 0x3b, 0xfc, 0xa7, 0x0d, 0xff, 0x8c, 0xc7, 0x04, 0xf6, 0xff, 0x21, 0xb0, 0x72, 0xb7, 0xdf, 0x2a,
    0x0b, 0x1b, 0xe1, 0x44, 0x89, 0x70, 0x84, 0x1f, 0x32, 0xa3, 0x37, 0x59, 0xa1, 0xdf, 0x7b, 0x3c, 0xff, 0xb7, 0xe4, 0xfe,
    0x2b, 0xf3, 0x94, 0x9f, 0x9d, 0xff, 0x53, 0x51, 0xe7, 0x3f, 0x83, 0xfc, 0x8f, 0xd6, 0xff, 0x07, 0x55, 0x9a, 0x43, 0x61,
    0xc2, 0x95, 0x74, 0x12, 0xcb, 0x2b, 0xc0, 0x8b, 0x2b, 0xc0, 0x10, 0xd0, 0x7f, 0xe0, 0xff, 0xe2, 0xf9, 0x8f, 0xc5, 0x84,
    0x13, 0x98, 0xff, 0x26, 0xd3, 0xff, 0xa2, 0xa5, 0xff, 0x29, 0xf4, 0x3f, 0xf4, 0x3f, 0x5e, 0x1b, 0xed, 0x22, 0xf7, 0xe1,
    0xa0, 0xa8, 0xff, 0x78, 0xff, 0x77, 0x9e, 0xff, 0x08, 0x39, 0xeb, 0x7f, 0xc6, 0xe0, 0xfc, 0x37, 0x04, 0xdb, 0xba, 0xfa,
    0x91, 0x96, 0x3b, 0x05, 0x51, 0x80, 0xfc, 0x9f, 0xe4, 0x7f, 0x59, 0x95, 0xa5, 0xd2, 0x2e, 0x3c, 0x16, 0xf0, 0x4d, 0xbe,
    0xff, 0xa5, 0xe9, 0x05, 0xf3, 0x1f, 0x25, 0x34, 0x09, 0x10, 0xbd, 0x59, 0x09, 0x41, 0xfe, 0xbf, 0xe3, 0x3f, 0x7b, 0x7e,
    0x9c, 0xbf, 0xbc, 0xcd, 0x7b, 0x99, 0xff, 0xba, 0xbf, 0xff, 0x35, 0xff, 0xff, 0xc5, 0xbc, 0x1e, 0x09, 0xa1, 0xff, 0x07,
    0xe0, 0xe9, 0x35, 0x83, 0x12, 0x04, 0x00, 0x00, 0x98, 0x20, 0x9f, 0x5d, 0x9e, 0x69, 0x3b, 0x00, 0x28, 0x00, 0x00,
};

std::span<const std::uint8_t> sample_tar_gz() { return kSampleTarGzBytes; }

// The folder the long name holds.
constexpr const char* kLongFolder =
    "share/etude/verovio-data/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

using namespace etude;
using namespace etude::app;
using namespace upload_test;

// Made by the real minisign 0.11 with a key pair generated for these tests only; the secret half is
// gone and the key is not the app's.
const std::string kPayload = "etude test payload: not a real release.\n";
const std::string kComment = "etude 1.2.3 bookworm arm64";
const std::string kTestKey = "RWQ6O+XM6TL4f2t5zvWbRDAA20Tmy0CrIHW0OhJ0RZqg/WDMeOM0R9ng";
const std::string kSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUQ6O+XM6TL4f8RO94hZ3KhqNqNO5heUnbldGAMnwwQizVE1AhDnVH94E3I9PuOYHeZyoGnnFJHr5mnSQ6MsZGHDsV"
    "WqEu8WHwA=\n"
    "trusted comment: etude 1.2.3 bookworm arm64\n"
    "K2DQhXqR7AnTzkhdYN7+OubRJNd3xw3duEbymJCbMIGmVCfrKDY2Hv5ktCvFY7U20UD1TVn76d7MoN/2W/uUAw==\n";
// The legacy kind (`minisign -l`: signs the file itself).
const std::string kLegacySignature =
    "untrusted comment: signature from minisign secret key\n"
    "RWQ6O+XM6TL4f3Z3nmx3FNQLd3N/ND/Bhd1mW/qBhCJxqbGdXTKZk27bFpTIjIPn7MjtSqoWds63afOa3xvmYxTJrM"
    "gmShstWQk=\n"
    "trusted comment: etude 1.2.3 bookworm arm64\n"
    "eVT2MCJZnz3ILihs/ba5CoSw2a6pthDWtwuunV/UJe+/c+jav7cgiixi7XMP7J2S0tkhoZOxop5N9gMcx/cBAA==\n";
// Signed correctly, by another key pair.
const std::string kOtherKeySignature =
    "untrusted comment: signature from minisign secret key\n"
    "RURA6GhIuZEJevqlfmPkMDcmFZlTIWHxw3EVVlGQ/OwxbQhJsUFpfoAHmyMEDDgzNH2vzIdIBKKFhWfmTWR+njCq84"
    "NAEXqvEQk=\n"
    "trusted comment: etude 1.2.3 bookworm arm64\n"
    "mNd/wB5ApnURrkNDZvTZkJHN5I0MGyWbzxnmcm/G4mNcdKjpeh7PfJcWpIDa1Oa86dgWmfkXi02B1t457PxBBQ==\n";

std::vector<std::uint8_t> bytes_of(const std::string& s) { return {s.begin(), s.end()}; }

SignatureError check(const std::string& data, const std::string& signature, const std::string& key = kTestKey,
                     const std::string& comment = kComment) {
    return verify_minisign(bytes_of(data), signature, key, comment).error;
}

// `text` with the first `from` replaced by `to`.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

const std::string kTop = "etude-1.2.3";

// A release.json for release `version` (the hash is a stand-in unless given).
std::string release_json(const std::string& version, const std::string& os, const std::string& file, const std::string& min_os,
                         const std::string& size, const std::string& hash = std::string(64, 'a')) {
    return "{\"version\": \"" + version + "\", \"os\": \"" + os + "\", \"arch\": \"arm64\", \"min_os\": \"" + min_os +
           "\", \"file\": \"" + file + "\", \"sha256\": \"" + hash + "\", \"size\": " + size + "}";
}

TarResult read(const Tar& t, const TarLimits& limits = {}) { return read_tar(t.done(), kTop, limits); }

Tar good() {
    Tar t;
    t.dir(kTop).dir(kTop + "/bin").file(kTop + "/bin/etude", "app", 0755);
    return t;
}

const std::string kBase = "https://github.com/techneissa/etude/releases/download/v1.2.3/";
const std::string kTar = "etude-1.2.3-trixie-arm64.tar.gz";
const std::string kZip = "etude-1.2.3-macos-arm64.zip";

std::string asset(const std::string& name, const std::string& url = "") {
    return "{\"name\": \"" + name + "\", \"size\": 100, \"state\": \"uploaded\", \"browser_download_url\": \"" +
           (url.empty() ? kBase + name : url) + "\"}";
}

std::string feed(const std::string& assets, const std::string& tag = "v1.2.3") {
    return "{\"tag_name\": \"" + tag + "\", \"draft\": false, \"prerelease\": false, \"body\": \"Fixes\", \"assets\": [" +
           assets + "]}";
}

std::string files_of(const std::string& name) { return asset(name) + "," + asset(name + ".minisig"); }

ReleaseInfo info_for(const std::string& os, const std::string& min_os) {
    ReleaseInfo i;
    i.version = "1.2.3";
    i.os = os;
    i.arch = "arm64";
    i.min_os = min_os;
    i.file = release_file_name("1.2.3", os, "arm64");
    i.sha256 = std::string(64, 'a');
    i.size = 100;
    return i;
}

MachineOsText machine(const std::string& id, const std::string& version, const std::string& arch = "arm64") {
    MachineOsText m;
    m.arch = arch;
    m.id = id;
    m.version_id = version;
    return m;
}

const std::string kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

template <typename Pred> bool wait_for(UpdateService& s, Pred&& done, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (done(s.update())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for_file(const fs::path& file, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (fs::exists(file)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// Serves release 1.2.3 from a folder (FolderFetcher, so no network): the feed, its release info
// `info_name` and the file with its signature.
void serve_release(const fs::path& root, const std::string& info_name, const std::string& info, const std::string& file,
                   const std::string& bytes) {
    write_file(root / "sites/api.github.com/repos/techneissa/etude/releases/latest", feed(asset(info_name) + "," + files_of(file)));
    const fs::path download = root / "sites/github.com/techneissa/etude/releases/download/v1.2.3";
    write_file(download / info_name, info);
    write_file(download / file, bytes);
    write_file(download / (file + ".minisig"), "untrusted comment: a signature\nRUQ=\n");
}

// The service's options for a machine that installs, reading `root`'s sites; the app is 1.1.0.
UpdateOptions base_options(const fs::path& root, const char* os_id, const char* os_version) {
    UpdateOptions o;
    o.fetcher = std::make_unique<FolderFetcher>((root / "sites").string());
    o.current_version = "1.1.0";
    o.machine.arch = "arm64";
    o.machine.id = os_id;
    o.machine.version_id = os_version;
    o.key_ready = true;
    o.installs = true;
    o.update_dir = root / "update";
    o.state_file = root / "state/update-check.txt";
    o.poll_ms = 10;
    o.startup_delay_ms = 0;
    return o;
}

// A release 1.2.3 whose tarball is the sample (or `tarball`), for a machine that installs. The
// signature check is the test's own and records what it was given; the helper's part is played by the
// test writing its files.
struct Rig {
    TempDir root;
    FolderFetcher* fetcher = nullptr;
    std::atomic<int> verified{0};
    std::vector<std::uint8_t> checked_bytes;
    std::string checked_comment;
    std::atomic<bool> signature_good{true};

    explicit Rig(const std::vector<std::uint8_t>& tarball = {}, std::size_t announced_size = 0) {
        const std::vector<std::uint8_t> sample(sample_tar_gz().begin(), sample_tar_gz().end());
        const std::vector<std::uint8_t>& bytes = tarball.empty() ? sample : tarball;
        serve_release(root.path, "release.json", release_json("1.2.3", "trixie", kTar, "13", std::to_string(announced_size != 0 ? announced_size : bytes.size()), kHash),
                      kTar, std::string(bytes.begin(), bytes.end()));
        fs::create_directories(update_dir());
    }

    fs::path update_dir() const { return root.path / "update"; }

    UpdateOptions options() {
        UpdateOptions o = base_options(root.path, "debian", "13");
        fetcher = static_cast<FolderFetcher*>(o.fetcher.get());
        o.pickup_wait_ms = 1500;
        o.verify = [this](std::span<const std::uint8_t> data, std::string_view, std::string_view comment) {
            checked_bytes.assign(data.begin(), data.end());
            checked_comment = std::string(comment);
            ++verified;
            SignatureCheck result;
            if (!signature_good) {
                result.error = SignatureError::BadSignature;
            }
            return result;
        };
        return o;
    }
};

bool is_phase(const ui::UpdateView& v, ui::UpdatePhase p) { return v.phase == p; }

// Checks, and waits for what the service offers.
void offer_update(UpdateService& s) {
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Available) && v.installable; }));
}

bool failed(UpdateService& s) {
    return wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Failed); });
}

// ---- A Mac's install

// A bundle that is a folder with a program and a VERSION file, enough for the script and the checks.
fs::path fake_bundle(const fs::path& where, const std::string& version) {
    write_file(where / "Contents/MacOS/etude", "#!/bin/sh\n");
    ::chmod((where / "Contents/MacOS/etude").c_str(), 0755);
    write_file(where / "VERSION", version);
    return where;
}

std::string version_of(const fs::path& bundle) { return read_file(bundle / "VERSION"); }

std::string status_value(const fs::path& status, const std::string& key) {
    const std::string text = "\n" + read_file(status);
    const std::size_t at = text.find("\n" + key + "=");
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t from = at + key.size() + 2;
    return text.substr(from, text.find('\n', from) - from);
}

// Whether this system exchanges two folders in one call (a Linux file system may not).
bool swap_supported() {
    static const bool supported = [] {
        TempDir dir;
        fs::create_directories(dir.path / "a");
        fs::create_directories(dir.path / "b");
        return swap_directories(dir.path / "a", dir.path / "b");
    }();
    return supported;
}

// `swap A B`: A and B trade places (the app's own does it with one renamex_np call).
constexpr const char* kSwapStandIn = "#!/bin/sh\n[ -e \"$1\" ] && [ -e \"$2\" ] || exit 1\n"
                                     "mv \"$1\" \"$1.swapping\" && mv \"$2\" \"$1\" && mv \"$1.swapping\" \"$2\"\n";

// Two fake bundles, a fake `open`, and the relaunch script, in a temporary folder.
struct SwapRig {
    TempDir dir;
    fs::path apps = dir.path / "Apps";
    fs::path bundle = apps / "etude.app";
    fs::path new_bundle = apps / ".etude-update-2.0.0/etude.app";
    fs::path previous = apps / ".etude-1.0.0.previous";
    fs::path update = dir.path / "update";
    fs::path swap = dir.path / "swap";
    std::string swap_env = "ETUDE_SWAP='" + swap.string() + "'";

    // `ready_for`: the version whose launch writes the ready file ("" : none does).
    explicit SwapRig(const std::string& ready_for) {
        fake_bundle(bundle, "1.0.0");
        fake_bundle(new_bundle, "2.0.0");
        fs::create_directories(update);
        write_script(swap, kSwapStandIn);
        write_file(update / "relaunch.sh", std::string(mac_relaunch_script()));
        write_script(dir.path / "open", "#!/bin/sh\nif [ \"$(cat \"$1/VERSION\")\" = '" + ready_for + "' ]; then\n"
                     "  (sleep 0.06; printf 'version=" + ready_for + "\\n' > '" + (update / "ready").string() + "') >/dev/null 2>&1 &\nfi\n");
    }

    // Runs the script for an app that has already quit (2^22 + 1 is no process); its exit code.
    int run(int wait_seconds) {
        const std::string command = "ETUDE_TICK=0.02 ETUDE_OPEN='" + (dir.path / "open").string() + "' " + swap_env + " /bin/sh '" +
                                    (update / "relaunch.sh").string() + "' 4194305 '" + bundle.string() + "' '" + new_bundle.string() + "' '" +
                                    previous.string() + "' '" + (update / "ready").string() + "' '" + (update / "status").string() +
                                    "' 2.0.0 1.0.0 " + std::to_string(wait_seconds) + " >/dev/null 2>&1";
        const int raw = std::system(command.c_str());
        return raw == -1 ? -1 : WEXITSTATUS(raw);
    }
    std::string state() const { return status_value(update / "status", "state"); }
};

const std::string kZipBytes = "PK-pretend-zip-bytes-of-a-bundle";

// Release 1.2.3 for a Mac; the running app is 1.1.0 in <root>/Apps.
struct MacRig {
    TempDir root;
    std::atomic<int> quits{0};
    std::vector<std::vector<std::string>> programs; // what the runner was asked
    std::vector<std::vector<std::string>> spawned;
    std::string plist_version = "1.2.3";
    int codesign_exit = 0;
    bool spawn_ok = true;
    bool signature_good = true;
    std::vector<std::uint8_t> checked_digest;
    fs::path apps = root.path / "Apps";
    fs::path bundle = apps / "etude.app";

    MacRig() {
        serve_release(root.path, "release-macos.json", release_json("1.2.3", "macos", kZip, "14", std::to_string(kZipBytes.size())), kZip, kZipBytes);
        write_file(bundle / "Contents/MacOS/etude", "old");
    }

    fs::path update_dir() const { return root.path / "update"; }
    fs::path staging() const { return apps / ".etude-update-1.2.3"; }

    UpdateOptions options() {
        UpdateOptions o = base_options(root.path, "macos", "14.5");
        o.mac.bundle = bundle;
        o.mac.pid = 4242;
        o.mac.quit = [this] { ++quits; };
        o.mac.run = [this](const std::vector<std::string>& argv, int) {
            programs.push_back(argv);
            ProgramResult done;
            done.started = true;
            done.exit_code = 0;
            if (argv[0] == "/usr/bin/ditto") {
                const fs::path program = fs::path(argv[4]) / "etude.app/Contents/MacOS/etude";
                write_file(program, "new");
                ::chmod(program.c_str(), 0755);
            } else if (argv[0] == "/usr/bin/codesign") {
                done.exit_code = codesign_exit;
            } else {
                done.output = plist_version + "\n";
            }
            return done;
        };
        o.mac.spawn = [this](const std::vector<std::string>& argv, const std::string&) {
            spawned.push_back(argv);
            return spawn_ok;
        };
        o.verify_digest = [this](const Digest512& digest, std::string_view, std::string_view) {
            checked_digest.assign(digest.begin(), digest.end());
            SignatureCheck result;
            if (!signature_good) {
                result.error = SignatureError::BadSignature;
            }
            return result;
        };
        return o;
    }
};

} // namespace

TEST_CASE("signing: only a genuine signature of these exact bytes and this release passes") {
    const SignatureCheck ok = verify_minisign(bytes_of(kPayload), kSignature, kTestKey, kComment);
    CHECK(ok.ok());
    CHECK(ok.trusted_comment == kComment);
    CHECK(check(kPayload, kLegacySignature) == SignatureError::None);
    CHECK(check(kPayload, kSignature, "untrusted comment: minisign public key\n" + kTestKey + "\n") == SignatureError::None);
    const struct {
        const char* what;
        SignatureError got;
        SignatureError want;
    } refused[] = {
        {"one byte added", check(kPayload + "x", kSignature), SignatureError::BadSignature},
        {"one byte cut", check(kPayload.substr(1), kSignature), SignatureError::BadSignature},
        {"empty file", check("", kSignature), SignatureError::BadSignature},
        {"legacy, changed", check(kPayload + "x", kLegacySignature), SignatureError::BadSignature},
        {"damaged signature", check(kPayload, replaced(kSignature, "PuOYHeZyoGnn", "PuOYHeZyoGnA")), SignatureError::BadSignature},
        {"another key", check(kPayload, kOtherKeySignature), SignatureError::WrongKeyId},
        {"key id forged", check(kPayload, replaced(kOtherKeySignature, "RURA6GhIuZEJev", "RUQ6O+XM6TL4f/")), SignatureError::BadSignature},
        {"trusted comment edited", check(kPayload, replaced(kSignature, "1.2.3", "9.9.9"), kTestKey, "etude 9.9.9 bookworm arm64"),
         SignatureError::BadTrustedCommentSignature},
        {"another release", check(kPayload, kSignature, kTestKey, "etude 1.2.4 bookworm arm64"), SignatureError::TrustedCommentMismatch},
        {"no signature", check(kPayload, ""), SignatureError::BadSignatureFile},
        {"cut signature", check(kPayload, kSignature.substr(0, kSignature.rfind("trusted"))), SignatureError::BadSignatureFile},
        {"not base64", check(kPayload, replaced(kSignature, "RUQ6O+", "RUQ!O+")), SignatureError::BadSignatureFile},
        {"unknown algorithm", check(kPayload, replaced(kSignature, "RUQ6O+", "SUQ6O+")), SignatureError::UnsupportedAlgorithm},
        {"no key", check(kPayload, kSignature, ""), SignatureError::BadKey},
        {"placeholder key", check(kPayload, kSignature, "MINISIGN-PUBLIC-KEY-NOT-SET"), SignatureError::BadKey},
        {"two keys", check(kPayload, kSignature, kTestKey + "\n" + kTestKey), SignatureError::BadKey},
    };
    for (const auto& r : refused) {
        CAPTURE(r.what);
        CHECK(r.got == r.want);
    }
    // Hashed in pieces, as the helper does for a big download: the same verdicts.
    FileDigest hash;
    for (std::size_t at = 0; at < kPayload.size(); at += 7) {
        hash.update({reinterpret_cast<const std::uint8_t*>(kPayload.data()) + at, std::min<std::size_t>(7, kPayload.size() - at)});
    }
    const Digest512 digest = hash.finish();
    CHECK(verify_minisign_digest(digest, kSignature, kTestKey, kComment).ok());
    CHECK(verify_minisign_digest(digest, kLegacySignature, kTestKey, kComment).error == SignatureError::UnsupportedAlgorithm);
    CHECK(verify_minisign_digest(digest, kOtherKeySignature, kTestKey, kComment).error == SignatureError::WrongKeyId);
}

TEST_CASE("signing: the placeholder key verifies and offers nothing, base64 is read strictly") {
    CHECK(update_key_is_placeholder("MINISIGN-PUBLIC-KEY-NOT-SET"));
    const std::vector<std::uint8_t> data{1, 2, 3};
    if (update_key_is_placeholder(update_public_key())) {
        CHECK_FALSE(update_key_ready());
        CHECK(verify_release_signature(data, "any text", "any comment").error == SignatureError::KeyNotSet);
    } else {
        CHECK(update_key_ready()); // the real key must be a well-formed minisign public key
        CHECK(verify_release_signature(data, "any text", "any comment").error != SignatureError::BadKey);
    }
    const auto text = [](std::string_view in) {
        const auto out = base64_decode(in);
        return out ? std::string(out->begin(), out->end()) : std::string("<refused>");
    };
    CHECK(text("Zm9vYmE=") == "fooba");
    CHECK(text("") == "");
    for (const char* bad : {"Zm9", "Zm9v\n", "Zm 9v", "Zm9-", "Zg=A", "Zh==", "Zm9=", "===="}) {
        CAPTURE(bad);
        CHECK(text(bad) == "<refused>");
    }
}

TEST_CASE("version compare: only a newer plain release is offered") {
    CHECK(compare_versions("1.10.0", "1.9.0") == VersionOrder::Newer); // numbers, not text
    CHECK(compare_versions("1.2.3", "1.2.3") == VersionOrder::Same);
    CHECK(compare_versions("1.2.2", "1.2.3") == VersionOrder::Older);
    CHECK(compare_versions("1.0.0", "1.0.0-rc.1") == VersionOrder::Newer);
    CHECK(compare_versions("1.0.0-rc.1", "1.0.0") == VersionOrder::Older);
    for (const char* bad : {"v1.2.4", "1.2", "01.2.3", "1.2.3-", ""}) {
        CAPTURE(bad);
        CHECK(compare_versions(bad, "1.2.3") == VersionOrder::Malformed); // never newer
        CHECK_FALSE(parse_semver(bad).has_value());
    }
}

TEST_CASE("tar: a release is listed below its top folder, with safe modes") {
    Tar t = good();
    t.file(kTop + "/s", "x", 04777).file(kTop + "/t", "x", 01666).symlink(kTop + "/link", "bin/etude");
    const TarResult r = read(t);
    REQUIRE(r.ok());
    const auto& e = r.archive.entries;
    REQUIRE(e.size() == 6);
    CHECK(e[2].path == "bin/etude");
    CHECK(e[2].mode == 0755);
    CHECK(e[3].mode == 0755); // set-uid and the rest never come through
    CHECK(e[4].mode == 0644);
    CHECK(e[5].kind == TarKind::Symlink);
    // The real sample tarball, with its GNU long name and its link, under its own top folder only.
    const GunzipResult unpacked = gunzip(sample_tar_gz(), 1 << 20);
    REQUIRE(unpacked.ok());
    CHECK(read_tar(unpacked.data, kTop).ok());
    CHECK(read_tar(unpacked.data, "etude-9.9.9").error == TarError::WrongTop);
}

TEST_CASE("tar: an archive with a hostile or odd entry is refused") {
    const auto with = [&](auto&& add) {
        Tar t = good();
        add(t);
        return read(t).error;
    };
    const auto up = kTop + "/../x";
    const struct {
        const char* what;
        TarError got;
        TarError want;
    } refused[] = {
        {"absolute path", with([](Tar& t) { t.file("/etc/passwd"); }), TarError::BadPath},
        {"climbing path", with([&](Tar& t) { t.file(up); }), TarError::BadPath},
        {"double slash", with([](Tar& t) { t.file(kTop + "//x"); }), TarError::BadPath},
        {"backslash", with([](Tar& t) { t.file(kTop + "/a\\b"); }), TarError::BadPath},
        {"another top folder", with([](Tar& t) { t.file("other/x"); }), TarError::WrongTop},
        {"a near top folder", with([](Tar& t) { t.dir("etude-1.2.30"); }), TarError::WrongTop},
        {"hard link", with([](Tar& t) { t.add(kTop + "/odd", '1', "", "bin/etude"); }), TarError::Unsupported},
        {"device", with([](Tar& t) { t.add(kTop + "/odd", '3'); }), TarError::Unsupported},
        {"pipe", with([](Tar& t) { t.add(kTop + "/odd", '6'); }), TarError::Unsupported},
        {"absolute link", with([](Tar& t) { t.symlink(kTop + "/l", "/etc/passwd"); }), TarError::BadLink},
        {"climbing link", with([](Tar& t) { t.symlink(kTop + "/l", "../.."); }), TarError::BadLink},
        {"climb after a name", with([](Tar& t) { t.symlink(kTop + "/l", "a/../x"); }), TarError::BadLink},
        {"chained links climb out",
         with([](Tar& t) {
             t.dir(kTop + "/a").symlink(kTop + "/a/up", "..").symlink(kTop + "/c", "a/up/..");
         }),
         TarError::BadLink},
        {"a file below a link", with([](Tar& t) { t.symlink(kTop + "/l", "bin").file(kTop + "/l/evil"); }), TarError::UnderFile},
        {"a file below a file", with([](Tar& t) { t.file(kTop + "/bin/etude/x"); }), TarError::UnderFile},
        {"the same path twice", with([](Tar& t) { t.file(kTop + "/bin/etude"); }), TarError::Duplicate},
    };
    for (const auto& r : refused) {
        CAPTURE(r.what);
        CHECK(r.got == r.want);
    }
    // Climbing first and then only going down is what releases do.
    Tar fine = good();
    fine.dir(kTop + "/share").dir(kTop + "/lib").file(kTop + "/lib/f", "f").symlink(kTop + "/share/f", "../lib/f");
    CHECK(read(fine).ok());
    // Damaged: a flipped header byte, a cut file, and the limits exactly (at it, one past).
    std::vector<std::uint8_t> flipped = good().done();
    flipped[10] ^= 1;
    CHECK(read_tar(flipped, kTop).error == TarError::BadHeader);
    Tar big = good();
    big.file(kTop + "/big", std::string(2000, 'x'));
    std::vector<std::uint8_t> cut = big.raw();
    cut.resize(cut.size() - 1200);
    CHECK(read_tar(cut, kTop).error == TarError::Truncated);
    Tar two = good();
    two.file(kTop + "/a", std::string(100, 'x'));
    TarLimits limits;
    limits.max_total_bytes = 103;
    CHECK(read(two, limits).ok());
    limits.max_total_bytes = 102;
    CHECK(read(two, limits).error == TarError::TooBig);
    limits = {};
    limits.max_file_bytes = 99;
    CHECK(read(two, limits).error == TarError::TooBig);
}

TEST_CASE("release info: a release.json is read strictly, and fits only a machine at least as new") {
    const auto json = [](const std::string& version, const std::string& os, const std::string& file, const std::string& min_os,
                         const std::string& size = "48000000") { return release_json(version, os, file, min_os, size); };
    const std::string tarball = "etude-1.2.3-bookworm-arm64.tar.gz";
    const ReleaseInfoResult r = parse_release_info(json("1.2.3", "bookworm", tarball, "12"));
    REQUIRE(r.ok());
    CHECK(release_trusted_comment(r.info) == "etude 1.2.3 bookworm arm64");
    ReleaseComment comment;
    CHECK(parse_release_trusted_comment("etude 1.2.3 trixie arm64", comment));
    CHECK_FALSE(parse_release_trusted_comment("etude 1.2.3-rc.1 trixie arm64", comment));
    // A file name must be the one the rule gives, so it cannot point elsewhere; a prerelease is never offered.
    for (const char* file : {"../etude-1.2.3-bookworm-arm64.tar.gz", "/etc/passwd", "etude-1.2.4-bookworm-arm64.tar.gz",
                             "etude-1.2.3-bookworm-arm64.tar.gz/x"}) {
        CAPTURE(file);
        CHECK(parse_release_info(json("1.2.3", "bookworm", file, "12")).field == "file");
    }
    CHECK(parse_release_info(json("2.0.0-rc.1", "bookworm", "etude-2.0.0-rc.1-bookworm-arm64.tar.gz", "12")).field == "version");
    CHECK(parse_release_info(json("1.2.3", "bookworm", tarball, "12", "99999999999")).field == "size");
    for (const char* bad : {"", "[]", "{", "{\"a\":1}{\"a\":1}", "{\"a\":{\"b\":1}}", "{\"a\":\"\\q\"}"}) {
        CAPTURE(bad);
        CHECK(parse_release_info(bad).error == ReleaseInfoError::NotJson);
    }
    CHECK(parse_release_info(replaced(json("1.2.3", "bookworm", tarball, "12"), "\"sha256\"", "\"zzz\"")).error ==
          ReleaseInfoError::MissingField);

    const auto machine = [](std::string id, std::string version, std::string arch = "arm64") {
        MachineOs m;
        m.arch = arch;
        m.id = id;
        m.version_id = version;
        return m;
    };
    const ReleaseInfoResult pi = parse_release_info(json("1.2.3", "trixie", "etude-1.2.3-trixie-arm64.tar.gz", "13"));
    REQUIRE(pi.ok());
    CHECK(release_fits_machine(pi.info, machine("debian", "13")));
    CHECK(release_fits_machine(pi.info, machine("raspbian", "14")));
    CHECK_FALSE(release_fits_machine(pi.info, machine("debian", "12.9")));
    CHECK_FALSE(release_fits_machine(pi.info, machine("debian", "13", "x86_64")));
    CHECK_FALSE(release_fits_machine(pi.info, machine("ubuntu", "24.04")));
    CHECK_FALSE(release_fits_machine(pi.info, machine("macos", "27.0")));
    const ReleaseInfoResult mac = parse_release_info(json("1.2.3", "macos", "etude-1.2.3-macos-arm64.zip", "14.2"));
    REQUIRE(mac.ok());
    CHECK(release_fits_machine(mac.info, machine("macos", "14.2.1")));
    CHECK_FALSE(release_fits_machine(mac.info, machine("macos", "14.1")));
    CHECK_FALSE(release_fits_machine(mac.info, machine("debian", "13")));
}

TEST_CASE("update feed: a latest release is read, a withdrawn or odd one is refused") {
    const LatestReleaseResult r = parse_latest_release(feed(asset("release.json") + "," + files_of(kTar)));
    REQUIRE(r.ok());
    CHECK(r.release.page_url == "https://github.com/techneissa/etude/releases/tag/v1.2.3");
    CHECK(r.release.assets.size() == 3);
    CHECK(parse_latest_release(feed("", "1.2.3")).error == FeedError::BadField);
    CHECK(parse_latest_release(feed("", "v1.2.3-rc.1")).error == FeedError::BadField);
    CHECK(parse_latest_release("{\"tag_name\": \"v1.2.3\", \"draft\": true}").error == FeedError::Withdrawn);
    CHECK(parse_latest_release("{\"tag_name\": \"v1.2.3\", \"prerelease\": true}").error == FeedError::Withdrawn);
    CHECK(parse_latest_release("{\"name\": \"x\"}").error == FeedError::MissingField);
    CHECK(parse_latest_release(std::string(kMaxFeedBytes + 1, ' ')).error == FeedError::TooLarge);
    std::string deep = "{\"tag_name\": \"v1.2.3\", \"x\": ";
    deep.append(40, '[');
    CHECK(parse_latest_release(deep).error == FeedError::NotJson);
    // The page comes from the tag, never from the answer.
    CHECK(parse_latest_release("{\"tag_name\": \"v2.0.0\", \"html_url\": \"https://evil.example/x\"}").release.page_url ==
          "https://github.com/techneissa/etude/releases/tag/v2.0.0");
    // Only an asset that is this repository's download of that name is kept.
    const std::string a = "https://github.com/other/etude/releases/download/v1.2.3/a.tar.gz";
    const LatestReleaseResult kept = parse_latest_release(feed(
        asset("a.tar.gz", a) + "," + asset("a.tar.gz", kBase + "b.tar.gz") + "," +
        asset("a", "http://github.com/techneissa/etude/releases/download/v1.2.3/a") + "," +
        asset("a", kBase + "../../../../evil/a") + ",null,7," + asset("good.txt")));
    REQUIRE(kept.ok());
    REQUIRE(kept.release.assets.size() == 1);
    CHECK(kept.release.assets[0].name == "good.txt");
    const auto hosts = update_fetch_hosts();
    const auto has = [&](std::string_view h) { return std::find(hosts.begin(), hosts.end(), h) != hosts.end(); };
    CHECK(has("api.github.com"));
    CHECK(has("release-assets.githubusercontent.com"));
    CHECK_FALSE(has("raw.githubusercontent.com"));
    CHECK_FALSE(has("example.com"));
}

TEST_CASE("update feed: the files picked are this machine's, and a Mac reads its own release file") {
    const LatestReleaseResult all = parse_latest_release(
        feed(asset("release.json") + "," + files_of(kTar) + "," + asset("release-macos.json") + "," + files_of(kZip)));
    REQUIRE(all.ok());
    CHECK(release_info_asset_for(machine("macos", "14.5").view()) == "release-macos.json");
    CHECK(release_info_asset_for(machine("debian", "13").view()) == "release.json");
    const AssetPick pi = pick_release_assets(all.release, info_for("trixie", "13"), machine("debian", "13").view());
    REQUIRE(pi.result == PickResult::Ok);
    CHECK(pi.tarball.name == kTar);
    CHECK(pi.signature.url == kBase + kTar + ".minisig");
    const AssetPick mac = pick_release_assets(all.release, info_for("macos", "14"), machine("macos", "14.5").view());
    REQUIRE(mac.result == PickResult::Ok);
    CHECK(mac.tarball.name == kZip);
    ReleaseInfo wrong_version = info_for("trixie", "13");
    wrong_version.version = "1.2.4";
    const struct {
        const char* what;
        PickResult got;
        PickResult want;
    } refused[] = {
        {"wrong version", pick_release_assets(all.release, wrong_version, machine("debian", "13").view()).result, PickResult::WrongVersion},
        {"older system", pick_release_assets(all.release, info_for("trixie", "13"), machine("debian", "12").view()).result, PickResult::NotThisMachine},
        {"other arch", pick_release_assets(all.release, info_for("trixie", "13"), machine("debian", "13", "amd64").view()).result, PickResult::NotThisMachine},
        {"older macOS", pick_release_assets(all.release, info_for("macos", "14"), machine("macos", "13.6").view()).result, PickResult::NotThisMachine},
        {"a Mac's zip for a Pi", pick_release_assets(all.release, info_for("macos", "14"), machine("debian", "13").view()).result, PickResult::NotThisMachine},
        {"a Pi's tarball for a Mac", pick_release_assets(all.release, info_for("trixie", "13"), machine("macos", "14.5").view()).result, PickResult::NotThisMachine},
    };
    for (const auto& r : refused) {
        CAPTURE(r.what);
        CHECK(r.got == r.want);
    }
    const LatestReleaseResult no_tar = parse_latest_release(feed(asset("release.json")));
    CHECK(pick_release_assets(no_tar.release, info_for("trixie", "13"), machine("debian", "13").view()).result == PickResult::MissingTarball);
    const LatestReleaseResult no_sig = parse_latest_release(feed(asset(kTar)));
    CHECK(pick_release_assets(no_sig.release, info_for("trixie", "13"), machine("debian", "13").view()).result == PickResult::MissingSignature);
}

TEST_CASE("update conf: the helper's settings are data, not shell") {
    const ConfResult r = parse_update_conf("# written by setup.sh\nPREFIX=\"/opt/etude\"\nAPP_USER=etude\n");
    REQUIRE(r.ok());
    CHECK(r.conf.prefix == "/opt/etude");
    CHECK(r.conf.max_starts == 2);
    CHECK(parse_update_conf("SERVICE_TIMEOUT=90\nREADY_TIMEOUT=120\nMAX_STARTS=4\n").ok());
    for (const char* bad : {"SERVICE_TIMEOUT=91\n", "READY_TIMEOUT=0\n", "MAX_STARTS=5\n", "MAX_STARTS=x\n"}) {
        CAPTURE(bad);
        CHECK(parse_update_conf(bad).error == ConfError::BadValue);
    }
    for (const char* shell : {"PREFIX=/opt/a b\n", "PREFIX=/opt/a;rm -rf /\n", "PREFIX=$(id)\n", "PREFIX=`id`\n",
                              "PREFIX=/opt/$HOME\n", "PREFIX=\"/opt/x\n", "PREFIX=opt/etude\n", "PREFIX=/opt/../etc\n",
                              "PREFIX=/opt/etude/\n", "APP_USER=Root\n", "APP_USER=root;id\n", "SYSTEMCTL=systemctl\n"}) {
        CAPTURE(shell);
        CHECK(parse_update_conf(shell).error == ConfError::BadValue);
    }
    CHECK(parse_update_conf("FOO=bar\n").error == ConfError::UnknownKey);
    CHECK(parse_update_conf("export PREFIX=/a\n").error == ConfError::UnknownKey);
    CHECK(parse_update_conf("PREFIX=/a\nPREFIX=/b\n").error == ConfError::Duplicate);
    CHECK(parse_update_conf("if true; then\n").error == ConfError::BadLine);
    CHECK(parse_update_conf(std::string(5000, '#')).error == ConfError::TooLarge);
}

TEST_CASE("update state: the helper's record and the app's request are strict lines") {
    PendingUpdate s;
    s.phase = UpdatePhase::Pending;
    s.from = "1.1.0";
    s.to = "1.2.0";
    s.previous = "1.0.0";
    s.unit = "etude.service";
    s.starts = 2;
    s.backed_up = true;
    s.reason = "did not\nstart";
    const PendingParse r = parse_pending_update(format_pending_update(s));
    REQUIRE(r.ok);
    CHECK(r.state.to == "1.2.0");
    CHECK(r.state.starts == 2);
    CHECK(r.state.reason == "did not start");
    const std::string head = "phase=pending\nfrom=1.0.0\nto=1.1.0\n";
    CHECK(parse_pending_update(head).ok);
    for (const char* bad : {"", "phase=odd\n", "phase=pending\nfrom=1.0\nto=1.1.0\n", "phase=pending\nfrom=1.0.0\nto=../x\n",
                            "phase=pending\nto=1.1.0\n", "nonsense\n"}) {
        CAPTURE(bad);
        CHECK_FALSE(parse_pending_update(bad).ok);
    }
    for (const char* bad : {"starts=-1\n", "starts=x\n", "unit=sshd.service\n", "backup=maybe\n"}) {
        CAPTURE(bad);
        CHECK_FALSE(parse_pending_update(head + bad).ok);
    }
    CHECK(format_update_request("1.2.3") == "version=1.2.3\n");
    for (const char* bad : {"", "1.2", "v1.2.3", "1.2.3-rc.1", "01.2.3", "1.2.3\n", "../1.2.3"}) {
        CAPTURE(bad);
        CHECK(format_update_request(bad).empty());
    }
    const UpdateStatus status = parse_update_status("state=rolled-back\nversion=1.2.0\nfrom=../x\nreason=a\tb\n");
    CHECK(status.state == UpdateState::RolledBack);
    CHECK(status.from.empty());
    CHECK(status.reason == "a b");
    CHECK(parse_update_status("state=odd\n").state == UpdateState::None);
    // The data folder is found the way SDL finds it, and a doubtful Environment line never falls back to home.
    CHECK(app_data_folder("/home/etude", "") == "/home/etude/.local/share/io.github.techneissa.etude");
    CHECK(app_data_folder("/home/etude", "share").empty());
    CHECK(environment_lookup("A=1 \"XDG_DATA_HOME=/data/share\" B=2", "XDG_DATA_HOME").value == "/data/share");
    for (const char* bad : {"XDG_DATA_HOME=\"/a", "XDG_DATA_HOME='/a'", "XDG_DATA_HOME=", "XDG_DATA_HOME=relative", "\"XDG_DATA_HOME=/a b\" X=1"}) {
        CAPTURE(bad);
        CHECK(environment_lookup(bad, "XDG_DATA_HOME").kind == EnvKind::Unreadable);
    }
}

TEST_CASE("update ready: written once the frame is up and the history opened, never after a failed history") {
    ReadyGate gate;
    CHECK_FALSE(gate.update(true, HistoryOpen::Pending));
    CHECK_FALSE(gate.update(false, HistoryOpen::Opened));
    CHECK(gate.update(true, HistoryOpen::Opened));
    CHECK_FALSE(gate.update(true, HistoryOpen::Opened)); // once
    ReadyGate failed;
    CHECK_FALSE(failed.update(false, HistoryOpen::Failed));
    CHECK_FALSE(failed.update(true, HistoryOpen::Opened));
    CHECK(failed.failed());
    ReadyGate read_only; // another etude holds the database: nothing was migrated by this one
    CHECK(history_open_for(HistoryState::ReadOnly) == HistoryOpen::Pending);
    CHECK_FALSE(read_only.update(true, history_open_for(HistoryState::ReadOnly)));
}

TEST_CASE("update service: nothing is offered without the key, nothing is fetched until asked, only a newer release is") {
    {
        Rig rig;
        UpdateOptions o = rig.options();
        o.key_ready = false;
        UpdateService s(std::move(o));
        s.check_now();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(s.update().phase == ui::UpdatePhase::NotSetUp);
        CHECK(rig.fetcher->fetches() == 0);
    }
    for (const char* version : {"1.2.3", "1.3.0"}) { // the same or an older release is up to date
        Rig rig;
        UpdateOptions o = rig.options();
        o.current_version = version;
        UpdateService s(std::move(o));
        CHECK(rig.fetcher->fetches() == 0);
        s.check_now();
        REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::UpToDate); }));
    }
    Rig rig;
    UpdateOptions o = rig.options();
    o.machine.version_id = "12"; // not built for this system: offered by its page, not installable
    UpdateService s(std::move(o));
    s.check_now();
    REQUIRE(wait_for(s, [](const ui::UpdateView& v) { return is_phase(v, ui::UpdatePhase::Available); }));
    CHECK_FALSE(s.update().installable);
}

TEST_CASE("update install: the signature is checked over the tarball's own bytes, then it is left for the helper") {
    Rig rig;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
    CHECK(read_file(rig.update_dir() / "request") == "version=1.2.3\n");
    CHECK(rig.verified == 1);
    CHECK(rig.checked_bytes == std::vector<std::uint8_t>(sample_tar_gz().begin(), sample_tar_gz().end()));
    CHECK(rig.checked_comment == "etude 1.2.3 trixie arm64");
    CHECK(tree(rig.update_dir()) == "release.tar.gz\nrelease.tar.gz.minisig\nrequest\n"); // nothing unpacked
}

TEST_CASE("update install: a bad signature, a wrong size or a hostile archive leaves nothing for the helper") {
    const std::string top = "etude-1.2.3";
    Tar climbing;
    climbing.dir(top).dir(top + "/bin").file(top + "/bin/etude", "x", 0755).symlink(top + "/bin/out", "../../..");
    Tar escaping;
    escaping.dir(top).file(top + "/../escape", "x");
    Tar device;
    device.dir(top).add(top + "/null", '3');
    const struct {
        const char* what;
        std::vector<std::uint8_t> tarball;
        std::size_t announced_size;
        bool signature_good;
        const char* failure;
    } cases[] = {
        {"bad signature", {}, 0, false, "Signature doesn't match the release key"},
        {"wrong size", {}, sample_tar_gz().size() + 7, true, "The download is not the size the release said"},
        {"a link out of the folder", gzip_stored(climbing.done()), 0, true, "The download was refused: the archive"},
        {"a path that climbs", gzip_stored(escaping.done()), 0, true, "The download was refused: the archive"},
        {"a device", gzip_stored(device.done()), 0, true, "The download was refused: the archive"},
        {"not gzip", std::vector<std::uint8_t>(300, 0x41), 0, true, "The download could not be unpacked"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.what);
        Rig rig(c.tarball, c.announced_size);
        rig.signature_good = c.signature_good;
        UpdateService s(rig.options());
        offer_update(s);
        s.update_now();
        REQUIRE(failed(s));
        CHECK(s.update().failure.rfind(c.failure, 0) == 0);
        CHECK(tree(rig.update_dir()).empty());
        if (std::string(c.what) == "wrong size") {
            CHECK(rig.verified == 0); // refused before the signature is read
        }
    }
}

TEST_CASE("update install: only from the idle screen, and the helper's refusal is shown") {
    Rig rig;
    UpdateService s(rig.options());
    offer_update(s);
    s.set_busy(true);
    s.update_now(); // a song is playing: nothing starts
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(s.update().phase == ui::UpdatePhase::Available);
    CHECK(rig.verified == 0);
    s.set_busy(false);
    s.update_now();
    REQUIRE(wait_for_file(rig.update_dir() / "request"));
    write_file(rig.update_dir() / "status", "state=failed\nversion=1.2.3\nfrom=1.1.0\nreason=the unpacked folder was refused\n");
    REQUIRE(failed(s));
    CHECK(s.update().failure == "Update to 1.2.3 failed: the unpacked folder was refused");
    CHECK_FALSE(fs::exists(rig.update_dir() / "release.tar.gz")); // what the app left is removed
}

TEST_CASE("update install: a new version tells the helper it is ready only when its history opened") {
    Rig rig;
    UpdateService s(rig.options());
    s.startup_progress(true, HistoryOpen::Failed);
    s.startup_progress(true, HistoryOpen::Opened); // not even if it then reads as opened
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(fs::exists(rig.update_dir() / "ready"));
    UpdateService t(rig.options());
    t.startup_progress(true, HistoryOpen::Pending);
    t.startup_progress(true, HistoryOpen::Opened);
    REQUIRE(wait_for_file(rig.update_dir() / "ready"));
    CHECK(read_file(rig.update_dir() / "ready") == "version=1.1.0\n");
    write_file(rig.update_dir() / "status", "state=rolled-back\nversion=1.2.0\nfrom=1.1.0\nreason=1.2.0 did not start\n");
    UpdateService after(rig.options()); // 1.1.0 again after a rollback: it says why
    CHECK(after.update().failure == "Update to 1.2.0 failed: 1.2.0 did not start");
}

TEST_CASE("update files: a failed write, fsync or rename leaves the old file whole") {
    struct Case {
        const char* name;
        FileSteps steps;
        int error;
    };
    FileSteps no_write;
    no_write.write = [](int, const void*, std::size_t) -> ssize_t { errno = ENOSPC; return -1; };
    FileSteps no_fsync;
    no_fsync.fsync = [](int) { errno = EIO; return -1; };
    FileSteps no_rename;
    no_rename.rename = [](const char*, const char*) { errno = EXDEV; return -1; };
    for (const Case& c : {Case{"write", no_write, ENOSPC}, Case{"fsync", no_fsync, EIO}, Case{"rename", no_rename, EXDEV}}) {
        CAPTURE(c.name);
        TempDir dir;
        const fs::path file = dir.path / "state.txt";
        write_file(file, "the old answer\n");
        int error = 0;
        CHECK_FALSE(write_file_atomic_with(c.steps, file, "the new answer\n", &error));
        CHECK(error == c.error);
        CHECK(read_file(file) == "the old answer\n");
        CHECK(tree(dir.path) == "state.txt\n"); // no .part left behind
    }
}

TEST_CASE("update unpack: the release is written as packed, and a folder or link already there is never written into") {
    TempDir temp;
    const GunzipResult g = gunzip(sample_tar_gz(), 1 << 20);
    REQUIRE(g.ok());
    const TarResult read = read_tar(g.data, "etude-1.2.3");
    REQUIRE(read.ok());
    const fs::path dest = temp.path / "versions/1.2.3";
    const helper::UnpackResult r = helper::unpack_tar(read.archive, g.data, dest);
    INFO(r.reason);
    REQUIRE(r.ok);
    CHECK(read_file(dest / "bin/etude") == "#!/bin/sh\necho etude 1.2.3\n");
    CHECK(read_file(dest / kLongFolder / "font.txt") == "long name");
    struct stat st {};
    REQUIRE(::lstat((dest / "bin/etude").c_str(), &st) == 0);
    CHECK((st.st_mode & 07777) == 0755);
    CHECK(fs::is_directory(dest / "share/etude/current-font")); // the link leads to the long folder
    write_file(temp.path / "taken/keep", "mine");
    CHECK_FALSE(helper::unpack_tar(read.archive, g.data, temp.path / "taken").ok);
    CHECK(read_file(temp.path / "taken/keep") == "mine");
    CHECK_FALSE(fs::exists(temp.path / "taken/bin"));
    fs::create_symlink(temp.path / "keep-away", temp.path / "linked");
    CHECK_FALSE(helper::unpack_tar(read.archive, g.data, temp.path / "linked").ok);
    CHECK_FALSE(fs::exists(temp.path / "keep-away"));
}

TEST_CASE("signed os: a release built for a Debian release fits that or a newer one, macos only a Mac") {
    const auto os_of = [](const char* os_release) {
        MachineOsText m;
        m.arch = "arm64";
        parse_os_release(os_release, m);
        return m;
    };
    const MachineOsText trixie = os_of("ID=debian\nVERSION_ID=\"13\"\n");
    const MachineOsText bookworm = os_of("ID=raspbian\nVERSION_ID=\"12\"\n");
    const MachineOsText mac = os_of("ID=macos\nVERSION_ID=\"14.5\"\n");
    CHECK(signed_os_fits_machine("trixie", trixie.view()));
    CHECK(signed_os_fits_machine("bookworm", trixie.view())); // built on an older one
    CHECK_FALSE(signed_os_fits_machine("forky", trixie.view())); // built on a newer one
    CHECK_FALSE(signed_os_fits_machine("sid", trixie.view()));   // not a known name
    CHECK_FALSE(signed_os_fits_machine("trixie", bookworm.view()));
    CHECK_FALSE(signed_os_fits_machine("trixie", os_of("ID=fedora\n").view()));
    CHECK(signed_os_fits_machine("macos", mac.view()));
    CHECK_FALSE(signed_os_fits_machine("macos", trixie.view()));
    CHECK_FALSE(signed_os_fits_machine("trixie", mac.view()));
}

TEST_CASE("mac install: the app is replaced only where it runs from a writable bundle") {
    CHECK(mac_bundle_of_executable("/Applications/etude.app/Contents/MacOS/etude") == "/Applications/etude.app");
    CHECK(mac_bundle_of_executable("/opt/build/app/etude").empty());
    CHECK(mac_bundle_of_executable("/Applications/etude.app/Contents/Resources/etude").empty());
    const auto verdict = [](std::string bundle, bool read_only, bool image, bool writable) {
        MacInstallFacts f;
        f.bundle = bundle;
        f.volume_read_only = read_only;
        f.volume_is_disk_image = image;
        f.parent_writable = writable;
        return check_mac_install(f).block;
    };
    CHECK(verdict("/Applications/etude.app", false, false, true) == MacInstallBlock::None);
    CHECK(verdict("", false, false, true) == MacInstallBlock::NotABundle);
    CHECK(verdict("/private/var/folders/T/AppTranslocation/7B1E/d/etude.app", true, false, true) == MacInstallBlock::Translocated);
    CHECK(verdict("/Volumes/etude 0.9.1/etude.app", true, true, true) == MacInstallBlock::DiskImage);
    CHECK(verdict("/Volumes/Backup/etude.app", true, false, true) == MacInstallBlock::ReadOnlyVolume);
    CHECK(verdict("/Applications/etude.app", false, false, false) == MacInstallBlock::NotWritable);
}

TEST_CASE("mac install: two folders trade places in one call, and a missing one changes nothing") {
    if (!swap_supported()) {
        return;
    }
    TempDir dir;
    write_file(dir.path / "a/name", "a");
    write_file(dir.path / "b/name", "b");
    REQUIRE(swap_directories(dir.path / "a", dir.path / "b"));
    CHECK(read_file(dir.path / "a/name") == "b");
    CHECK(read_file(dir.path / "b/name") == "a");
    CHECK_FALSE(swap_directories(dir.path / "a", dir.path / "missing"));
    CHECK(run_swap_bundle((dir.path / "a").string().c_str(), (dir.path / "gone").string().c_str()) == 1);
    CHECK(read_file(dir.path / "a/name") == "b");
}

TEST_CASE("mac install: a new bundle is used only if it unpacks alone, verifies as signed and says the released version") {
    TempDir dir;
    const ProgramRunner hung = [](const std::vector<std::string>&, double) {
        ProgramResult done;
        done.started = true;
        done.timed_out = true;
        return done;
    };
    CHECK_FALSE(unpack_release_zip(hung, dir.path / "z", dir.path / "stage").ok);
    CHECK_FALSE(fs::exists(dir.path / "stage")); // a failed unpack leaves nothing
    fake_bundle(dir.path / "one/etude.app", "1");
    CHECK(find_unpacked_bundle(dir.path / "one") == dir.path / "one/etude.app");
    fake_bundle(dir.path / "two/a.app", "1");
    fake_bundle(dir.path / "two/b.app", "1");
    CHECK(find_unpacked_bundle(dir.path / "two").empty());
    std::string version = "1.2.3";
    int code = 0;
    const ProgramRunner run = [&](const std::vector<std::string>& argv, double) {
        ProgramResult done;
        done.started = true;
        done.exit_code = argv[0] == "/usr/bin/codesign" ? code : 0;
        done.output = argv[0] == "/usr/bin/codesign" ? std::string() : version + "\n";
        return done;
    };
    const fs::path bundle = fake_bundle(dir.path / "etude.app", "1.2.3");
    CHECK(check_new_bundle(run, bundle, "1.2.3").ok);
    version = "1.2.2"; // signed, but another version than the release
    CHECK_FALSE(check_new_bundle(run, bundle, "1.2.3").ok);
    version = "1.2.3";
    code = 1; // the code signature does not hold
    CHECK_FALSE(check_new_bundle(run, bundle, "1.2.3").ok);
}

TEST_CASE("mac install: the relaunch leaves a whole app at the path, the new one only if it reaches its first frame") {
    if (!swap_supported()) {
        return;
    }
    {
        SwapRig rig("2.0.0");
        fake_bundle(rig.apps / ".etude-0.9.0.previous", "0.9.0");
        CHECK(rig.run(10) == 0);
        CHECK(version_of(rig.bundle) == "2.0.0");
        CHECK(version_of(rig.previous) == "1.0.0"); // the old one is kept
        CHECK_FALSE(fs::exists(rig.apps / ".etude-0.9.0.previous"));
        CHECK(rig.state() == "ok");
    }
    const auto rolled_back = [](SwapRig& rig, int wait, const char* state) {
        CHECK(rig.run(wait) == 1);
        CHECK(version_of(rig.bundle) == "1.0.0");
        CHECK(rig.state() == state);
    };
    {
        SwapRig rig(""); // nothing writes ready: the old one is put back
        rolled_back(rig, 1, "rolled-back");
    }
    {
        SwapRig rig(""); // a ready file left by an earlier start does not count
        write_file(rig.update / "ready", "version=2.0.0\n");
        CHECK(rig.run(1) == 1);
        CHECK(version_of(rig.bundle) == "1.0.0");
    }
    {
        SwapRig rig("2.0.0"); // no staged bundle
        fs::remove_all(rig.new_bundle);
        rolled_back(rig, 2, "failed");
    }
    {
        SwapRig rig("2.0.0"); // the swap program cannot exchange
        rig.swap_env = "ETUDE_SWAP=/usr/bin/false";
        rolled_back(rig, 2, "failed");
    }
    {
        SwapRig rig(""); // the first exchange works, the rollback's does not: still a whole app
        const fs::path flaky = rig.dir.path / "flaky";
        write_script(flaky, "#!/bin/sh\nif [ -f '" + (rig.dir.path / "used").string() + "' ]; then exit 1; fi\ntouch '" +
                                (rig.dir.path / "used").string() + "'\nexec '" + rig.swap.string() + "' \"$@\"\n");
        rig.swap_env = "ETUDE_SWAP='" + flaky.string() + "'";
        CHECK(rig.run(1) == 1);
        CHECK(fs::is_directory(rig.bundle));
        CHECK(version_of(rig.bundle) == "2.0.0"); // not a hollow folder
        CHECK(version_of(rig.previous) == "1.0.0");
        CHECK(rig.state() == "failed");
    }
}

TEST_CASE("mac install: the zip is checked by its hash, unpacked, checked, and handed to the relaunch script before the quit") {
    MacRig rig;
    UpdateService s(rig.options());
    offer_update(s);
    s.update_now();
    for (int i = 0; i < 1000 && rig.quits == 0 && s.update().phase != ui::UpdatePhase::Failed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(s.update().failure);
    REQUIRE(rig.quits == 1);
    FileDigest whole;
    whole.update({reinterpret_cast<const std::uint8_t*>(kZipBytes.data()), kZipBytes.size()});
    const Digest512 expected = whole.finish();
    CHECK(rig.checked_digest == std::vector<std::uint8_t>(expected.begin(), expected.end()));
    REQUIRE(rig.programs.size() == 3);
    CHECK(rig.programs[0][0] == "/usr/bin/ditto");
    CHECK(rig.programs[1][0] == "/usr/bin/codesign");
    REQUIRE(rig.spawned.size() == 1);
    CHECK(rig.spawned[0][3] == rig.bundle.string());
    CHECK(rig.spawned[0][4] == (rig.staging() / "etude.app").string());
    CHECK(read_file(rig.bundle / "Contents/MacOS/etude") == "old"); // the app moved nothing itself
}

TEST_CASE("mac install: a bad signature, a bad bundle or a script that will not start leaves the app untouched") {
    const struct {
        const char* what;
        void (*tweak)(MacRig&);
    } cases[] = {
        {"bad signature", [](MacRig& r) { r.signature_good = false; }},
        {"bad code signature", [](MacRig& r) { r.codesign_exit = 1; }},
        {"another version", [](MacRig& r) { r.plist_version = "1.2.2"; }},
        {"script will not start", [](MacRig& r) { r.spawn_ok = false; }},
    };
    for (const auto& c : cases) {
        CAPTURE(c.what);
        MacRig rig;
        c.tweak(rig);
        UpdateService s(rig.options());
        offer_update(s);
        s.update_now();
        REQUIRE(failed(s));
        CHECK(rig.quits == 0);
        CHECK_FALSE(fs::exists(rig.staging()));
        CHECK_FALSE(fs::exists(rig.update_dir() / "release.zip"));
        CHECK(read_file(rig.bundle / "Contents/MacOS/etude") == "old");
    }
}
