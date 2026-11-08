// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_groups.hpp"

#include "etude/song_prefs.hpp"

#include <algorithm>
#include <tuple>

namespace etude {

std::string clean_group_name(std::string_view name) {
    SongPrefs p;
    p.title.assign(name);
    std::string out = sanitized(p).title;
    if (out.size() > kMaxGroupName) {
        std::size_t cut = kMaxGroupName;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
    }
    return out.empty() ? std::string("New group") : out;
}

void LibraryGroups::sort_groups() {
    std::stable_sort(groups_.begin(), groups_.end(), [](const SongGroup& a, const SongGroup& b) {
        return std::tie(a.position, a.created_utc_s, a.id) <
               std::tie(b.position, b.created_utc_s, b.id);
    });
}

const SongGroup* LibraryGroups::find(std::uint64_t id) const {
    for (const SongGroup& g : groups_) {
        if (g.id == id) {
            return &g;
        }
    }
    return nullptr;
}

bool LibraryGroups::apply(const GroupEdit& e) {
    const std::uint64_t id = e.group.id;
    if (id == 0) {
        return false;
    }
    const auto it = std::find_if(groups_.begin(), groups_.end(),
                                 [&](const SongGroup& g) { return g.id == id; });
    bool changed = false;
    switch (e.kind) {
    case GroupEdit::Kind::Create:
        if (it == groups_.end()) {
            SongGroup g = e.group;
            g.name = clean_group_name(g.name);
            groups_.push_back(std::move(g));
            sort_groups();
            changed = true;
        }
        break;
    case GroupEdit::Kind::Rename:
        if (it != groups_.end()) {
            std::string name = clean_group_name(e.group.name);
            changed = name != it->name;
            it->name = std::move(name);
        }
        break;
    case GroupEdit::Kind::Remove:
        if (it != groups_.end()) {
            groups_.erase(it);
            songs_.erase(id);
            changed = true;
        }
        break;
    case GroupEdit::Kind::Add:
        if (it != groups_.end() && e.hash != 0) {
            std::vector<std::uint64_t>& s = songs_[id];
            if (std::find(s.begin(), s.end(), e.hash) == s.end()) {
                s.push_back(e.hash);
                changed = true;
            }
        }
        break;
    case GroupEdit::Kind::Drop:
        if (const auto s = songs_.find(id); s != songs_.end()) {
            const auto m = std::find(s->second.begin(), s->second.end(), e.hash);
            if (m != s->second.end()) {
                s->second.erase(m);
                changed = true;
            }
        }
        break;
    case GroupEdit::Kind::Move:
        if (it != groups_.end() && it->position != e.group.position) {
            it->position = e.group.position;
            sort_groups();
            changed = true;
        }
        break;
    }
    if (changed) {
        ++version_;
    }
    return changed;
}

void LibraryGroups::replay(std::span<const SongGroup> groups, std::span<const Member> members,
                           std::span<const GroupEdit> edits) {
    groups_.assign(groups.begin(), groups.end());
    sort_groups();
    songs_.clear();
    std::vector<Member> sorted(members.begin(), members.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const Member& a, const Member& b) {
        return a.added_utc_s < b.added_utc_s;
    });
    for (const Member& m : sorted) {
        if (find(m.group) != nullptr && m.hash != 0) {
            std::vector<std::uint64_t>& s = songs_[m.group];
            if (std::find(s.begin(), s.end(), m.hash) == s.end()) {
                s.push_back(m.hash);
            }
        }
    }
    for (const GroupEdit& e : edits) {
        apply(e);
    }
    ++version_;
}

std::span<const std::uint64_t> LibraryGroups::songs_of(std::uint64_t id) const {
    const auto it = songs_.find(id);
    return it == songs_.end() ? std::span<const std::uint64_t>{} : std::span(it->second);
}

bool LibraryGroups::contains(std::uint64_t id, std::uint64_t hash) const {
    const std::span<const std::uint64_t> s = songs_of(id);
    return std::find(s.begin(), s.end(), hash) != s.end();
}

std::vector<std::uint64_t> LibraryGroups::groups_of(std::uint64_t hash) const {
    std::vector<std::uint64_t> out;
    for (const SongGroup& g : groups_) {
        if (contains(g.id, hash)) {
            out.push_back(g.id);
        }
    }
    return out;
}

std::int32_t LibraryGroups::next_position() const {
    std::int32_t p = 0;
    for (const SongGroup& g : groups_) {
        p = std::max(p, g.position + 1);
    }
    return p;
}

} // namespace etude
