// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_feed.hpp"

#include "etude/semver.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <memory>
#include <utility>

namespace etude {
namespace {

constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxAssets = 64;

// A parsed JSON value. Numbers are kept as their text, which is all this file needs of them.
struct Json {
    enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool flag = false;
    std::string text; // a string's characters, a number's digits
    std::vector<std::pair<std::string, Json>> members;
    std::vector<Json> items;

    const Json* get(std::string_view key) const {
        for (const auto& [name, value] : members) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
    const std::string* string_of(std::string_view key) const {
        const Json* v = get(key);
        return v != nullptr && v->kind == Kind::String ? &v->text : nullptr;
    }
    bool true_of(std::string_view key) const {
        const Json* v = get(key);
        return v != nullptr && v->kind == Kind::Bool && v->flag;
    }
};

class Reader {
  public:
    explicit Reader(std::string_view text) : text_(text) {}

    bool document(Json& out) {
        skip_space();
        if (!value(out, 0)) {
            return false;
        }
        skip_space();
        return at_ == text_.size();
    }

  private:
    std::string_view text_;
    std::size_t at_ = 0;

    bool peek(char c) const { return at_ < text_.size() && text_[at_] == c; }
    bool take(char c) {
        if (!peek(c)) {
            return false;
        }
        ++at_;
        return true;
    }
    void skip_space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }
    bool word(std::string_view w) {
        if (text_.substr(at_, w.size()) != w) {
            return false;
        }
        at_ += w.size();
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > kMaxDepth || at_ >= text_.size()) {
            return false;
        }
        const char c = text_[at_];
        if (c == '{') {
            return object(out, depth);
        }
        if (c == '[') {
            return array(out, depth);
        }
        if (c == '"') {
            out.kind = Json::Kind::String;
            return string(out.text);
        }
        if (word("true")) {
            out.kind = Json::Kind::Bool;
            out.flag = true;
            return true;
        }
        if (word("false")) {
            out.kind = Json::Kind::Bool;
            return true;
        }
        if (word("null")) {
            out.kind = Json::Kind::Null;
            return true;
        }
        return number(out);
    }

    bool number(Json& out) {
        const std::size_t start = at_;
        while (at_ < text_.size() &&
               (text_[at_] == '-' || text_[at_] == '+' || text_[at_] == '.' || text_[at_] == 'e' ||
                text_[at_] == 'E' || (text_[at_] >= '0' && text_[at_] <= '9'))) {
            ++at_;
        }
        if (at_ == start) {
            return false;
        }
        out.kind = Json::Kind::Number;
        out.text.assign(text_.substr(start, at_ - start));
        return true;
    }

    bool object(Json& out, int depth) {
        out.kind = Json::Kind::Object;
        take('{');
        skip_space();
        if (take('}')) {
            return true;
        }
        for (;;) {
            skip_space();
            std::string key;
            if (!string(key)) {
                return false;
            }
            skip_space();
            if (!take(':')) {
                return false;
            }
            skip_space();
            Json member;
            if (!value(member, depth + 1)) {
                return false;
            }
            out.members.emplace_back(std::move(key), std::move(member));
            skip_space();
            if (take('}')) {
                return true;
            }
            if (!take(',')) {
                return false;
            }
        }
    }

    bool array(Json& out, int depth) {
        out.kind = Json::Kind::Array;
        take('[');
        skip_space();
        if (take(']')) {
            return true;
        }
        for (;;) {
            skip_space();
            Json item;
            if (!value(item, depth + 1)) {
                return false;
            }
            out.items.push_back(std::move(item));
            skip_space();
            if (take(']')) {
                return true;
            }
            if (!take(',')) {
                return false;
            }
        }
    }

    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool hex4(unsigned& code) {
        if (at_ + 4 > text_.size()) {
            return false;
        }
        const auto [end, error] =
            std::from_chars(text_.data() + at_, text_.data() + at_ + 4, code, 16);
        if (error != std::errc() || end != text_.data() + at_ + 4) {
            return false;
        }
        at_ += 4;
        return true;
    }

    bool unicode_escape(std::string& out) {
        unsigned code = 0;
        if (!hex4(code)) {
            return false;
        }
        if (code >= 0xDC00 && code < 0xE000) {
            return false; // a low half with no high half
        }
        if (code >= 0xD800 && code < 0xDC00) {
            unsigned low = 0;
            if (!word("\\u") || !hex4(low) || low < 0xDC00 || low >= 0xE000) {
                return false;
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        }
        append_utf8(out, code);
        return true;
    }

    bool string(std::string& out) {
        if (!take('"')) {
            return false;
        }
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_ >= text_.size()) {
                return false;
            }
            const char e = text_[at_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                if (!unicode_escape(out)) {
                    return false;
                }
                break;
            default: return false;
            }
        }
        return false;
    }
};

constexpr std::string_view kDownloadPrefix =
    "https://github.com/techneissa/etude/releases/download/";

bool plain_name(std::string_view name) {
    if (name.empty() || name.size() > 200) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return name != "." && name != "..";
}

// The address a release asset has: the repository's own download folder of that tag, then the name.
bool asset_url_ok(std::string_view url, std::string_view tag, std::string_view name) {
    if (!plain_name(name) || url.substr(0, kDownloadPrefix.size()) != kDownloadPrefix) {
        return false;
    }
    std::string_view rest = url.substr(kDownloadPrefix.size());
    if (rest.substr(0, tag.size()) != tag || rest.size() <= tag.size() ||
        rest[tag.size()] != '/') {
        return false;
    }
    return rest.substr(tag.size() + 1) == name;
}

bool tag_version(std::string_view tag, std::string& version) {
    if (tag.size() < 6 || tag.front() != 'v') {
        return false;
    }
    const std::optional<SemVer> parsed = parse_semver(tag.substr(1));
    if (!parsed || !parsed->prerelease.empty() || tag.find('+') != std::string_view::npos) {
        return false;
    }
    version.assign(tag.substr(1));
    return true;
}

void read_assets(const Json& root, LatestRelease& out) {
    const Json* list = root.get("assets");
    if (list == nullptr || list->kind != Json::Kind::Array) {
        return;
    }
    for (const Json& item : list->items) {
        if (out.assets.size() >= kMaxAssets) {
            break;
        }
        if (item.kind != Json::Kind::Object) {
            continue;
        }
        const std::string* name = item.string_of("name");
        const std::string* url = item.string_of("browser_download_url");
        if (name == nullptr || url == nullptr || !asset_url_ok(*url, out.tag, *name)) {
            continue;
        }
        ReleaseAsset asset;
        asset.name = *name;
        asset.url = *url;
        if (const Json* size = item.get("size"); size != nullptr && size->kind == Json::Kind::Number) {
            std::from_chars(size->text.data(), size->text.data() + size->text.size(), asset.size);
        }
        out.assets.push_back(std::move(asset));
    }
}

LatestReleaseResult fail(FeedError error, std::string field = {}) {
    LatestReleaseResult r;
    r.error = error;
    r.field = std::move(field);
    return r;
}

} // namespace

std::span<const std::string_view> update_fetch_hosts() {
    static constexpr std::array<std::string_view, 5> kHosts{
        "api.github.com",
        "github.com",
        "objects.githubusercontent.com",
        "release-assets.githubusercontent.com",
        "github-releases.githubusercontent.com",
    };
    return kHosts;
}

LatestReleaseResult parse_latest_release(std::string_view json) {
    if (json.size() > kMaxFeedBytes) {
        return fail(FeedError::TooLarge);
    }
    Json root;
    if (!Reader(json).document(root) || root.kind != Json::Kind::Object) {
        return fail(FeedError::NotJson);
    }
    if (root.true_of("draft") || root.true_of("prerelease")) {
        return fail(FeedError::Withdrawn);
    }
    const std::string* tag = root.string_of("tag_name");
    if (tag == nullptr) {
        return fail(FeedError::MissingField, "tag_name");
    }
    LatestReleaseResult result;
    LatestRelease& out = result.release;
    if (!tag_version(*tag, out.version)) {
        return fail(FeedError::BadField, "tag_name");
    }
    out.tag = *tag;
    if (const std::string* body = root.string_of("body")) {
        out.notes = *body;
    }
    // The page "Update" opens where updates are not installed: this repository's page for the tag,
    // built here rather than taken from the answer.
    out.page_url = std::string(kRepositoryUrl) + "/releases/tag/" + out.tag;
    read_assets(root, out);
    return result;
}

std::string_view release_info_asset_for(const MachineOs& machine) {
    return machine.id == kMacOsName ? kReleaseInfoAssetMac : kReleaseInfoAsset;
}

const ReleaseAsset* find_release_asset(const LatestRelease& release, std::string_view name) {
    for (const ReleaseAsset& asset : release.assets) {
        if (asset.name == name) {
            return &asset;
        }
    }
    return nullptr;
}

AssetPick pick_release_assets(const LatestRelease& release, const ReleaseInfo& info,
                              const MachineOs& machine) {
    AssetPick pick;
    if (info.version != release.version) {
        pick.result = PickResult::WrongVersion;
        return pick;
    }
    if (!release_fits_machine(info, machine)) {
        pick.result = PickResult::NotThisMachine;
        return pick;
    }
    const ReleaseAsset* tarball = find_release_asset(release, info.file);
    if (tarball == nullptr) {
        pick.result = PickResult::MissingTarball;
        return pick;
    }
    const ReleaseAsset* signature = find_release_asset(release, info.file + ".minisig");
    if (signature == nullptr) {
        pick.result = PickResult::MissingSignature;
        return pick;
    }
    pick.result = PickResult::Ok;
    pick.tarball = *tarball;
    pick.signature = *signature;
    return pick;
}

} // namespace etude
