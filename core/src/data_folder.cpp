// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/data_folder.hpp"

#include <vector>

namespace etude {

std::string app_data_folder(std::string_view home, std::string_view xdg_data_home) {
    std::string base;
    if (!xdg_data_home.empty()) {
        if (xdg_data_home.front() != '/') {
            return {};
        }
        base.assign(xdg_data_home);
    } else if (!home.empty() && home.front() == '/') {
        base = std::string(home) + "/.local/share";
    } else {
        return {};
    }
    while (base.size() > 1 && base.back() == '/') {
        base.pop_back();
    }
    return base + "/" + kAppDataFolderName;
}

namespace {

// Splits the text into words; false when a quote is not closed or an escape is not one systemd
// writes.
bool split_words(std::string_view text, std::vector<std::string>& words) {
    std::size_t at = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            ++at;
            continue;
        }
        std::string word;
        if (c == '"') {
            ++at;
            bool closed = false;
            while (at < text.size()) {
                const char d = text[at++];
                if (d == '"') {
                    closed = true;
                    break;
                }
                if (d == '\\') {
                    if (at >= text.size() || (text[at] != '"' && text[at] != '\\')) {
                        return false;
                    }
                    word += text[at++];
                } else {
                    word += d;
                }
            }
            if (!closed) {
                return false;
            }
        } else {
            while (at < text.size() && text[at] != ' ' && text[at] != '\n' && text[at] != '\r' &&
                   text[at] != '\t') {
                if (text[at] == '"' || text[at] == '\\' || text[at] == '\'') {
                    return false;
                }
                word += text[at++];
            }
        }
        words.push_back(std::move(word));
    }
    return true;
}

} // namespace

EnvLookup environment_lookup(std::string_view text, std::string_view key) {
    std::vector<std::string> words;
    if (!split_words(text, words)) {
        return {EnvKind::Unreadable, {}};
    }
    EnvLookup found;
    for (const std::string& word : words) {
        if (word.size() <= key.size() || word.compare(0, key.size(), key) != 0 ||
            word[key.size()] != '=') {
            continue;
        }
        const std::string value = word.substr(key.size() + 1);
        bool plain = !value.empty() && value.front() == '/';
        for (const char c : value) {
            plain = plain && c != ' ' && c != '"' && c != '\'' && c != '\\' &&
                    static_cast<unsigned char>(c) >= 0x20;
        }
        found = plain ? EnvLookup{EnvKind::Value, value} : EnvLookup{EnvKind::Unreadable, {}};
    }
    return found;
}

} // namespace etude
