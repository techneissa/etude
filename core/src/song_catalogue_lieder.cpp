// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_catalogue.hpp"

#include "song_catalogue_internal.hpp"

#include <algorithm>

namespace etude {

using catalogue_detail::split;

// ---- OpenScore Lieder

std::string_view openscore_lieder_index_url() {
    return "https://raw.githubusercontent.com/OpenScore/Lieder/main/data/scores.yaml";
}

namespace {

// "Abbott,_Jane_Bingham" -> "Jane Bingham Abbott".
std::string lieder_name(std::string_view segment) {
    std::string s(segment);
    std::replace(s.begin(), s.end(), '_', ' ');
    if (const std::size_t comma = s.find(", "); comma != std::string::npos) {
        return s.substr(comma + 2) + " " + s.substr(0, comma);
    }
    return s;
}

// A path's bytes for a URL: letters, digits and "-._~/," as they are, the rest %XX.
std::string url_path(std::string_view path) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : path) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ',') {
            out += c;
        } else {
            out += '%';
            out += kHex[u >> 4];
            out += kHex[u & 15];
        }
    }
    return out;
}

std::string yaml_value(std::string_view v) {
    while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) {
        v.remove_suffix(1);
    }
    if (v.size() >= 2 && (v.front() == '\'' || v.front() == '"') && v.back() == v.front()) {
        const char q = v.front();
        v = v.substr(1, v.size() - 2);
        std::string out;
        for (std::size_t i = 0; i < v.size(); ++i) {
            out += v[i];
            if (q == '\'' && v[i] == '\'' && i + 1 < v.size() && v[i + 1] == '\'') {
                ++i;
            }
        }
        return out;
    }
    return std::string(v);
}

void finish_lieder(CatalogueEntry& e, std::string_view path, std::string_view name) {
    if (e.id.empty() || path.empty()) {
        return;
    }
    const std::vector<std::string_view> parts = split(path, '/');
    if (parts.size() < 3) {
        return;
    }
    e.composer = lieder_name(parts[0]);
    const std::string_view set = parts[parts.size() - 2];
    if (set != "_") {
        std::string s(set);
        std::replace(s.begin(), s.end(), '_', ' ');
        e.detail = s;
    }
    if (!name.empty()) {
        e.title = std::string(name);
    } else {
        std::string_view song = parts.back();
        // "6_Is_my_team_ploughing": a number in its set goes.
        if (set != "_") {
            std::size_t digits = 0;
            while (digits < song.size() && std::isdigit(static_cast<unsigned char>(song[digits]))) {
                ++digits;
            }
            if (digits > 0 && digits < song.size() && song[digits] == '_') {
                song.remove_prefix(digits + 1);
            }
        }
        e.title = std::string(song);
        std::replace(e.title.begin(), e.title.end(), '_', ' ');
    }
    e.url = "https://raw.githubusercontent.com/OpenScore/Lieder/main/scores/" + url_path(path) +
            "/lc" + e.id + ".mscx";
    e.page = "https://github.com/OpenScore/Lieder/tree/main/scores/" + url_path(path);
    e.licence = "CC0";
    e.licence_url = "https://creativecommons.org/publicdomain/zero/1.0/";
}

} // namespace

void parse_openscore_lieder(std::string_view text, std::vector<CatalogueEntry>& out) {
    CatalogueEntry e;
    std::string path;
    std::string name;
    auto flush = [&] {
        finish_lieder(e, path, name);
        if (!e.url.empty() && !e.title.empty() && catalogue_file_allowed(e)) {
            out.push_back(std::move(e));
        }
        e = {};
        e.source = CatalogueId::OpenScoreLieder;
        path.clear();
        name.clear();
    };
    e.source = CatalogueId::OpenScoreLieder;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line[0] != ' ') {
            // "6583477:" starts a score.
            flush();
            if (line.back() == ':' && line.size() > 1 &&
                std::all_of(line.begin(), line.end() - 1,
                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
                e.id = std::string(line.substr(0, line.size() - 1));
            }
            continue;
        }
        const std::size_t colon = line.find(": ");
        if (colon == std::string_view::npos) {
            continue;
        }
        std::string_view field = line.substr(0, colon);
        while (!field.empty() && field.front() == ' ') {
            field.remove_prefix(1);
        }
        if (field == "path") {
            path = yaml_value(line.substr(colon + 2));
        } else if (field == "name") {
            name = yaml_value(line.substr(colon + 2));
        }
    }
    flush();
}

} // namespace etude
