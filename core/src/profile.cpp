// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/profile.hpp"

#include "etude/library.hpp"

#include <algorithm>
#include <cstddef>

namespace etude {

namespace {

bool is_blank(char c) { return c == ' ' || c == '\t'; }

bool is_control(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
}

char lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y) { return lower_ascii(x) == lower_ascii(y); });
}

} // namespace

std::string clean_profile_name(std::string_view text) {
    if (!is_valid_utf8(text)) {
        return {};
    }
    std::string out;
    bool pending_space = false;
    for (const char c : text) {
        if (is_blank(c) || is_control(c)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(c);
    }
    if (out.size() > kMaxProfileNameBytes) {
        std::size_t end = kMaxProfileNameBytes;
        while (end > 0 && (static_cast<unsigned char>(out[end]) & 0xC0) == 0x80) {
            --end; // not inside a UTF-8 sequence
        }
        out.resize(end);
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
    }
    return out;
}

Profile sanitized(const Profile& profile) {
    Profile out = profile;
    out.name = clean_profile_name(profile.name);
    if (out.name.empty()) {
        out.name = std::string(kDefaultProfileName);
    }
    out.colour = profile.colour & 0xFFFFFFu;
    return out;
}

ProfileSet::ProfileSet() { profiles_.push_back(Profile{}); }

ProfileSet::ProfileSet(std::span<const Profile> rows) {
    // The default profile first, so its name is never the one a repeat gives up.
    const auto take = [&](bool defaults) {
        for (const Profile& row : rows) {
            const Profile clean = sanitized(row);
            if (clean.is_default() != defaults || clean.id <= 0 || contains(clean.id) ||
                name_taken(clean.name, 0)) {
                continue;
            }
            profiles_.push_back(clean);
        }
    };
    take(true);
    if (!contains(kDefaultProfileId)) {
        profiles_.push_back(Profile{});
    }
    take(false);
    if (profiles_.size() > kMaxProfiles) {
        profiles_.resize(kMaxProfiles);
    }
}

const Profile* ProfileSet::find(std::int64_t id) const {
    for (const Profile& p : profiles_) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::int64_t ProfileSet::next_id() const {
    std::int64_t top = kDefaultProfileId;
    for (const Profile& p : profiles_) {
        top = std::max(top, p.id);
    }
    return top + 1;
}

std::uint32_t ProfileSet::unused_colour() const {
    std::array<int, kProfileColours.size()> used{};
    for (const Profile& p : profiles_) {
        for (std::size_t i = 0; i < kProfileColours.size(); ++i) {
            if (kProfileColours[i] == p.colour) {
                ++used[i];
            }
        }
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < used.size(); ++i) {
        if (used[i] < used[best]) {
            best = i;
        }
    }
    return kProfileColours[best];
}

bool ProfileSet::name_taken(std::string_view name, std::int64_t except_id) const {
    return std::any_of(profiles_.begin(), profiles_.end(), [&](const Profile& p) {
        return p.id != except_id && same_name(p.name, name);
    });
}

ProfileResult ProfileSet::check_name(std::string_view name, std::int64_t except_id) const {
    const std::string clean = clean_profile_name(name);
    if (clean.empty()) {
        return ProfileResult::EmptyName;
    }
    return name_taken(clean, except_id) ? ProfileResult::NameTaken : ProfileResult::Ok;
}

ProfileResult ProfileSet::add(std::string_view name, std::int64_t now_utc_s, Profile* made) {
    const std::string clean = clean_profile_name(name);
    if (clean.empty()) {
        return ProfileResult::EmptyName;
    }
    if (profiles_.size() >= kMaxProfiles) {
        return ProfileResult::TooMany;
    }
    if (name_taken(clean, 0)) {
        return ProfileResult::NameTaken;
    }
    Profile p;
    p.id = next_id();
    p.name = clean;
    p.colour = unused_colour();
    p.created_utc_s = now_utc_s;
    profiles_.push_back(p);
    if (made != nullptr) {
        *made = p;
    }
    return ProfileResult::Ok;
}

ProfileResult ProfileSet::rename(std::int64_t id, std::string_view name) {
    const std::string clean = clean_profile_name(name);
    for (Profile& p : profiles_) {
        if (p.id != id) {
            continue;
        }
        if (clean.empty()) {
            return ProfileResult::EmptyName;
        }
        if (name_taken(clean, id)) {
            return ProfileResult::NameTaken;
        }
        p.name = clean;
        return ProfileResult::Ok;
    }
    return ProfileResult::NoSuchProfile;
}

ProfileResult ProfileSet::recolour(std::int64_t id, std::uint32_t colour) {
    for (Profile& p : profiles_) {
        if (p.id == id) {
            p.colour = colour & 0xFFFFFFu;
            return ProfileResult::Ok;
        }
    }
    return ProfileResult::NoSuchProfile;
}

bool confirms_profile_name(std::string_view typed, std::string_view name) {
    while (!typed.empty() && typed.front() == ' ') {
        typed.remove_prefix(1);
    }
    while (!typed.empty() && typed.back() == ' ') {
        typed.remove_suffix(1);
    }
    return !name.empty() && typed == name;
}

ProfileResult ProfileSet::check_delete(std::int64_t id) const {
    if (!contains(id)) {
        return ProfileResult::NoSuchProfile;
    }
    if (profiles_.size() <= 1) {
        return ProfileResult::LastProfile;
    }
    return id == kDefaultProfileId ? ProfileResult::FirstProfile : ProfileResult::Ok;
}

ProfileResult ProfileSet::remove(std::int64_t id) {
    const ProfileResult result = check_delete(id);
    if (result == ProfileResult::Ok) {
        std::erase_if(profiles_, [&](const Profile& p) { return p.id == id; });
    }
    return result;
}

std::int64_t ProfileSet::successor(std::int64_t leaving, std::span<const ProfileUse> uses) const {
    std::int64_t best = leaving;
    std::int64_t best_at = -1;
    for (const Profile& p : profiles_) {
        if (p.id == leaving) {
            continue;
        }
        std::int64_t at = p.created_utc_s;
        for (const ProfileUse& use : uses) {
            if (use.id == p.id) {
                at = std::max(at, use.last_used_utc_s);
            }
        }
        if (at > best_at) { // the first of equals stays
            best = p.id;
            best_at = at;
        }
    }
    return best;
}

} // namespace etude
