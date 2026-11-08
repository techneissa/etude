// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_collections.hpp"

#include "etude/library.hpp"

#include <algorithm>

namespace etude {

bool in_collection(std::string_view folder, std::string_view path) {
    if (path.empty()) {
        return true;
    }
    return folder.size() >= path.size() && folder.substr(0, path.size()) == path &&
           (folder.size() == path.size() || folder[path.size()] == '/');
}

void child_collections(std::span<const std::string> folders, std::string_view parent,
                       std::vector<Collection>& out) {
    out.clear();
    for (const std::string& f : folders) {
        if (!in_collection(f, parent) || f.size() == parent.size()) {
            continue;
        }
        const std::string_view rest =
            std::string_view(f).substr(parent.empty() ? 0 : parent.size() + 1);
        const std::string_view name = rest.substr(0, rest.find('/'));
        if (name.empty()) {
            continue;
        }
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const Collection& c) { return c.name == name; });
        if (it == out.end()) {
            Collection c;
            c.name.assign(name);
            c.path = parent.empty() ? c.name : std::string(parent) + "/" + c.name;
            out.push_back(std::move(c));
            it = out.end() - 1;
        }
        ++it->songs;
    }
    std::sort(out.begin(), out.end(), [](const Collection& a, const Collection& b) {
        const std::string fa = fold_for_search(a.name);
        const std::string fb = fold_for_search(b.name);
        return fa != fb ? fa < fb : a.name < b.name;
    });
}

void breadcrumb(std::string_view path, std::vector<Crumb>& out) {
    out.clear();
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        if (end > start) {
            out.push_back({path.substr(start, end - start), path.substr(0, end)});
        }
        start = end + 1;
    }
}

bool is_new_import(std::int64_t added_utc_s, std::int64_t now_utc_s, bool played, bool grouped) {
    constexpr std::int64_t kDay = 24 * 60 * 60;
    return added_utc_s > 0 && !played && !grouped && now_utc_s >= added_utc_s &&
           now_utc_s - added_utc_s < kInboxDays * kDay;
}

int alpha_slot(std::string_view folded_title) {
    for (const char ch : folded_title) {
        if (ch >= 'a' && ch <= 'z') {
            return ch - 'a';
        }
        if (ch != ' ' && ch != '"' && ch != '\'' && ch != '(' && ch != '[') {
            break; // a digit or a sign: "#"
        }
    }
    return kAlphaSlots - 1;
}

void alpha_index(std::span<const std::uint32_t> order, std::span<const std::string> folded_titles,
                 AlphaIndex& out) {
    out.fill(-1);
    for (std::size_t k = 0; k < order.size(); ++k) {
        const std::uint32_t song = order[k];
        if (song >= folded_titles.size()) {
            continue;
        }
        const int slot = alpha_slot(folded_titles[song]);
        if (out[static_cast<std::size_t>(slot)] < 0) {
            out[static_cast<std::size_t>(slot)] = static_cast<std::int32_t>(k);
        }
    }
}

int alpha_target(const AlphaIndex& index, int slot) {
    slot = std::clamp(slot, 0, kAlphaSlots - 1);
    for (int s = slot; s < kAlphaSlots; ++s) {
        if (index[static_cast<std::size_t>(s)] >= 0) {
            return s;
        }
    }
    for (int s = slot - 1; s >= 0; --s) {
        if (index[static_cast<std::size_t>(s)] >= 0) {
            return s;
        }
    }
    return -1;
}

} // namespace etude
