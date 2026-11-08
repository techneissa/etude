// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_prefs.hpp"

#include "etude/library.hpp"

#include <algorithm>

namespace etude {

namespace {

std::string clean(std::string_view text) {
    const std::string valid = utf8_from_midi_text(text);
    std::string out;
    bool space = false;
    for (const char ch : valid) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c == 0x7F) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out.push_back(' ');
            space = false;
        }
        out.push_back(ch);
    }
    if (out.size() > kMaxSongPrefsText) {
        std::size_t cut = kMaxSongPrefsText;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
    }
    return out;
}

} // namespace

SongPrefs sanitized(const SongPrefs& prefs) {
    SongPrefs out;
    out.title = clean(prefs.title);
    out.composer = clean(prefs.composer);
    out.genre = clean(prefs.genre);
    out.favourite = prefs.favourite;
    out.added_utc_s = std::max<std::int64_t>(0, prefs.added_utc_s);
    out.chosen_utc_s = std::max<std::int64_t>(0, prefs.chosen_utc_s);
    return out;
}

void apply_edit(SongPrefs& prefs, const SongPrefEdit& edit) {
    switch (edit.field) {
    case SongPrefEdit::Field::Title:
        prefs.title = edit.text;
        break;
    case SongPrefEdit::Field::Composer:
        prefs.composer = edit.text;
        break;
    case SongPrefEdit::Field::Favourite:
        prefs.favourite = edit.on;
        break;
    case SongPrefEdit::Field::Chosen:
        prefs.chosen_utc_s = edit.time_utc_s;
        break;
    }
}

std::vector<std::pair<std::uint64_t, SongPrefs>>
apply_edits(const std::unordered_map<std::uint64_t, SongPrefs>& stored,
            std::span<const SongPrefEdit> edits) {
    std::vector<std::pair<std::uint64_t, SongPrefs>> rows;
    for (const SongPrefEdit& e : edits) {
        if (e.hash == 0) {
            continue;
        }
        auto row = std::find_if(rows.begin(), rows.end(),
                                [&](const auto& r) { return r.first == e.hash; });
        if (row == rows.end()) {
            const auto it = stored.find(e.hash);
            rows.emplace_back(e.hash, it != stored.end() ? it->second : SongPrefs{});
            row = rows.end() - 1;
        }
        apply_edit(row->second, e);
    }
    return rows;
}

} // namespace etude
