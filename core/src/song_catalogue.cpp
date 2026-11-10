// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_catalogue.hpp"

#include "etude/song_versions.hpp"

#include "song_catalogue_internal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <unordered_map>

namespace etude {

namespace {

constexpr std::array<std::string_view, 1> kMutopiaHosts{"www.mutopiaproject.org"};
constexpr std::array<std::string_view, 1> kPianoMidiHosts{"www.piano-midi.de"};
constexpr std::array<std::string_view, 1> kLiederHosts{"raw.githubusercontent.com"};

constexpr std::array<CatalogueInfo, kCatalogueCount> kInfos{{
    {CatalogueId::Mutopia, "mutopia", "Mutopia", "Mutopia", "https://www.mutopiaproject.org",
     "Public domain or Creative Commons, per piece",
     "Free sheet music typeset by volunteers of the Mutopia Project: solo piano pieces.", "",
     kMutopiaHosts, "https://www.mutopiaproject.org/ftp/", true},
    {CatalogueId::PianoMidi, "piano-midi", "piano-midi.de", "piano-midi.de",
     "http://www.piano-midi.de", "CC BY-SA 3.0 Germany",
     "Classical piano pieces played in by Bernd Krueger. Files from it keep the same licence.",
     // Off: it serves no valid HTTPS (an untrusted chain, an old TLS version) and answers
     // automated requests with HTTP 418.
     "Bernd Krueger, http://www.piano-midi.de", kPianoMidiHosts, "https://www.piano-midi.de/",
     false},
    {CatalogueId::OpenScoreLieder, "openscore-lieder", "OpenScore Lieder", "OpenScore",
     "https://github.com/OpenScore/Lieder", "CC0 (public domain)",
     "Songs for voice and piano from the OpenScore Lieder corpus; the voice plays along.", "",
     kLiederHosts, "https://raw.githubusercontent.com/OpenScore/Lieder/main/scores/", true},
}};

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Entity {
    std::string_view name;
    std::uint32_t cp;
};
constexpr std::array<Entity, 22> kEntities{{
    {"amp", '&'},     {"lt", '<'},       {"gt", '>'},       {"quot", '"'},    {"apos", '\''},
    {"nbsp", ' '},    {"ndash", 0x2013}, {"mdash", 0x2014}, {"rsquo", 0x2019}, {"lsquo", 0x2018},
    {"rdquo", 0x201D}, {"ldquo", 0x201C}, {"auml", 0xE4},   {"ouml", 0xF6},   {"uuml", 0xFC},
    {"Auml", 0xC4},   {"Ouml", 0xD6},    {"Uuml", 0xDC},    {"szlig", 0xDF},  {"eacute", 0xE9},
    {"egrave", 0xE8}, {"aacute", 0xE1},
}};

// The text between `open` (found from `from`) and the next `close`; npos-safe.
std::string_view between(std::string_view s, std::string_view open, std::string_view close,
                         std::size_t& from) {
    const std::size_t a = s.find(open, from);
    if (a == std::string_view::npos) {
        from = std::string_view::npos;
        return {};
    }
    const std::size_t b = s.find(close, a + open.size());
    if (b == std::string_view::npos) {
        from = std::string_view::npos;
        return {};
    }
    from = b + close.size();
    return s.substr(a + open.size(), b - a - open.size());
}

// The cells (<td>...</td>) of a block of HTML, as raw HTML.
std::vector<std::string_view> cells_of(std::string_view block) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at != std::string_view::npos) {
        const std::size_t open = block.find("<td", at);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t gt = block.find('>', open);
        const std::size_t close = block.find("</td>", gt == std::string_view::npos ? open : gt);
        if (gt == std::string_view::npos || close == std::string_view::npos) {
            break;
        }
        out.push_back(block.substr(gt + 1, close - gt - 1));
        at = close + 5;
    }
    return out;
}

// The first href="..." in `html` that ends with `suffix` (case aside).
std::string href_ending(std::string_view html, std::string_view suffix) {
    std::size_t at = 0;
    while ((at = html.find("href=\"", at)) != std::string_view::npos) {
        at += 6;
        const std::size_t end = html.find('"', at);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view url = html.substr(at, end - at);
        if (url.size() >= suffix.size() &&
            iequals(url.substr(url.size() - suffix.size()), suffix)) {
            return std::string(url);
        }
        at = end;
    }
    return {};
}

// "by F. Abt (1819–1885)" -> "F. Abt".
std::string mutopia_composer(std::string text) {
    if (text.rfind("by ", 0) == 0) {
        text.erase(0, 3);
    }
    if (const std::size_t paren = text.rfind(" ("); paren != std::string::npos &&
                                                     !text.empty() && text.back() == ')') {
        text.erase(paren);
    }
    return text;
}

// "2.5", "3.0", "4.0": digits, a dot, digits.
bool licence_version(std::string_view v) {
    const std::size_t dot = v.find('.');
    const auto digits = [](std::string_view s) {
        return !s.empty() && s.size() <= 2 &&
               std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    return dot != std::string_view::npos && digits(v.substr(0, dot)) && digits(v.substr(dot + 1));
}

} // namespace

bool mutopia_licence(std::string_view text, CatalogueEntry& e) {
    if (iequals(text, "Public Domain")) {
        e.licence = "Public domain";
        e.licence_url = "https://www.mutopiaproject.org/legal.html#publicdomain";
        e.credit.clear();
        return true;
    }
    // "Creative Commons Attribution 2.5", "Creative Commons Attribution-ShareAlike 3.0"; any
    // other (NonCommercial, NoDerivs, no version, none) is not one the app may hand on.
    constexpr std::string_view kBy = "Creative Commons Attribution ";
    constexpr std::string_view kBySa = "Creative Commons Attribution-ShareAlike ";
    const bool sa = text.rfind(kBySa, 0) == 0;
    if (!sa && text.rfind(kBy, 0) != 0) {
        return false;
    }
    const std::string_view version = text.substr(sa ? kBySa.size() : kBy.size());
    if (!licence_version(version)) {
        return false;
    }
    e.licence = std::string(sa ? "CC BY-SA " : "CC BY ") + std::string(version);
    e.licence_url = std::string("https://creativecommons.org/licenses/") + (sa ? "by-sa/" : "by/") +
                    std::string(version) + "/";
    e.credit = "The Mutopia Project, https://www.mutopiaproject.org";
    return true;
}

namespace {

using catalogue_detail::split;

void escape_field(std::string& out, std::string_view s) {
    for (const char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out += c;
        }
    }
}

std::string unescape_field(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

// `base` is the page a relative link was found on.
std::string resolve(std::string_view base, std::string_view link) {
    if (link.find("://") != std::string_view::npos) {
        return std::string(link);
    }
    const std::size_t scheme = base.find("://");
    if (scheme == std::string_view::npos) {
        return std::string(link);
    }
    if (!link.empty() && link[0] == '/') {
        return std::string(base.substr(0, base.find('/', scheme + 3))) + std::string(link);
    }
    const std::size_t slash = base.rfind('/');
    return std::string(base.substr(0, slash + 1)) + std::string(link);
}

constexpr std::string_view kCacheMagic = "etude-catalogue";
constexpr int kCacheVersion = 1;
constexpr std::size_t kCacheFields = 9; // id title composer detail licence licence_url credit url page

} // namespace

const CatalogueInfo& catalogue_info(CatalogueId id) {
    return kInfos[static_cast<std::size_t>(id)];
}

std::span<const CatalogueInfo, kCatalogueCount> catalogue_infos() { return kInfos; }

bool catalogue_from_key(std::string_view key, CatalogueId& out) {
    for (const CatalogueInfo& info : kInfos) {
        if (info.key == key) {
            out = info.id;
            return true;
        }
    }
    return false;
}

std::string html_text(std::string_view html) {
    std::string out;
    out.reserve(html.size());
    bool space = false;
    auto put_space = [&] {
        if (!out.empty()) {
            space = true;
        }
    };
    for (std::size_t i = 0; i < html.size(); ++i) {
        const char c = html[i];
        if (c == '<') {
            const std::size_t end = html.find('>', i);
            if (end == std::string_view::npos) {
                break;
            }
            // A line break or a cell's end parts words.
            const std::string_view tag = html.substr(i + 1, end - i - 1);
            if (iequals(tag.substr(0, 2), "br") || iequals(tag.substr(0, 3), "/td") ||
                iequals(tag.substr(0, 2), "/p")) {
                put_space();
            }
            i = end;
            continue;
        }
        std::uint32_t cp = 0;
        bool decoded = false;
        if (c == '&') {
            constexpr std::size_t kLongestEntity = 10;
            const std::size_t near = html.substr(i, kLongestEntity + 1).find(';');
            const std::size_t semi = near == std::string_view::npos ? near : i + near;
            if (semi != std::string_view::npos) {
                const std::string_view name = html.substr(i + 1, semi - i - 1);
                if (!name.empty() && name[0] == '#') {
                    const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
                    const std::string_view digits = name.substr(hex ? 2 : 1);
                    std::uint32_t v = 0;
                    const auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(),
                                                         v, hex ? 16 : 10);
                    if (ec == std::errc() && p == digits.data() + digits.size()) {
                        cp = v;
                        decoded = true;
                    }
                } else {
                    for (const Entity& e : kEntities) {
                        if (e.name == name) {
                            cp = e.cp;
                            decoded = true;
                            break;
                        }
                    }
                }
                if (decoded) {
                    i = semi;
                }
            }
        }
        if (!decoded) {
            cp = static_cast<unsigned char>(c);
            // A no-break space written out in UTF-8.
            if (c == '\xC2' && i + 1 < html.size() && html[i + 1] == '\xA0') {
                ++i;
                put_space();
                continue;
            }
        }
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || (decoded && cp == 0xA0)) {
            put_space();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        if (decoded) {
            append_utf8(out, cp);
        } else {
            out += c;
        }
    }
    return out;
}

std::string catalogue_file_extension(const CatalogueEntry& e) {
    std::string_view url = e.url;
    url = url.substr(0, url.find_first_of("?#"));
    const std::size_t slash = url.rfind('/');
    const std::size_t dot = url.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    std::string ext(url.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext;
}

std::string catalogue_file_name(const CatalogueEntry& e) {
    std::string base = e.composer.empty() ? e.title : e.composer + " - " + e.title;
    if (base.empty()) {
        base = e.id;
    }
    std::string out;
    for (const char c : base) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            out += '_';
        } else {
            out += c;
        }
    }
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) {
        out.erase(0, 1);
    }
    constexpr std::size_t kMax = 120;
    if (out.size() > kMax) {
        std::size_t cut = kMax;
        // Not inside a UTF-8 character.
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty()) {
        out = "song";
    }
    return out + catalogue_file_extension(e);
}

SongOrigin song_origin_of(const CatalogueEntry& e, std::int64_t downloaded_utc_s) {
    SongOrigin s;
    s.catalogue = std::string(catalogue_info(e.source).key);
    s.id = e.id;
    s.title = e.title;
    s.composer = e.composer;
    s.licence = e.licence;
    s.licence_url = e.licence_url;
    s.credit = e.credit;
    s.url = e.url;
    s.page = e.page;
    s.downloaded_utc_s = downloaded_utc_s;
    return s;
}

// ---- Mutopia

std::string mutopia_page_url(std::size_t start) {
    return "https://www.mutopiaproject.org/cgibin/make-table.cgi?startat=" + std::to_string(start) +
           "&searchingfor=&Composer=&Instrument=Piano&Style=&collection=&id=&solo=1&recent=&"
           "timelength=&timeunit=&lilyversion=&preview=";
}

void parse_mutopia_page(std::string_view html, std::vector<CatalogueEntry>& out, bool& more) {
    more = html.find(">Next 10<") != std::string_view::npos;
    constexpr std::string_view kTable = "<table class=\"table-bordered result-table\">";
    std::size_t at = 0;
    while (true) {
        const std::string_view block = between(html, kTable, "</table>", at);
        if (at == std::string_view::npos) {
            break;
        }
        const std::vector<std::string_view> cells = cells_of(block);
        if (cells.size() < 13) {
            continue;
        }
        CatalogueEntry e;
        e.source = CatalogueId::Mutopia;
        e.url = href_ending(block, ".mid");
        const std::size_t id_at = block.find("piece-info.cgi?id=");
        if (e.url.empty() || id_at == std::string_view::npos) {
            continue;
        }
        std::size_t id_end = id_at + 18;
        while (id_end < block.size() && std::isdigit(static_cast<unsigned char>(block[id_end]))) {
            ++id_end;
        }
        e.id = std::string(block.substr(id_at + 18, id_end - id_at - 18));
        if (e.id.empty() || !catalogue_file_allowed(e)) {
            continue;
        }
        e.page = "https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=" + e.id;
        e.title = html_text(cells[0]);
        e.composer = mutopia_composer(html_text(cells[1]));
        const std::string opus = html_text(cells[2]);
        const std::string style = html_text(cells[6]);
        e.detail = opus;
        if (!style.empty()) {
            e.detail += e.detail.empty() ? style : ", " + style;
        }
        if (!mutopia_licence(html_text(cells[9]), e) || e.title.empty()) {
            continue;
        }
        out.push_back(std::move(e));
    }
}

// ---- piano-midi.de

std::string_view piano_midi_start_url() { return "https://www.piano-midi.de/midi_files.htm"; }

std::vector<std::string> parse_piano_midi_composers(std::string_view html) {
    // The composers' pages are links to "name.htm" in the menu; the site's other pages are not.
    constexpr std::array<std::string_view, 16> kNotComposers{
        "about.htm",   "contact.htm",  "copy.htm",      "datenschutz.htm", "impressum.htm",
        "indeks.htm",  "links.htm",    "midi_files.htm", "midi_files_d.htm", "midicoll.htm",
        "news.htm",    "search.htm",   "statistic.htm", "other.htm",       "history.htm",
        "technic.htm"};
    std::vector<std::string> out;
    std::size_t at = 0;
    while ((at = html.find("href=\"", at)) != std::string_view::npos) {
        at += 6;
        const std::size_t end = html.find('"', at);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view link = html.substr(at, end - at);
        at = end;
        const bool page = link.ends_with(".htm") && !link.ends_with("_d.htm") &&
                          link.size() > 4 && link.find('/') == std::string_view::npos &&
                          link.find(':') == std::string_view::npos;
        if (!page || std::find(kNotComposers.begin(), kNotComposers.end(), link) !=
                         kNotComposers.end()) {
            continue;
        }
        std::string url = resolve(piano_midi_start_url(), link);
        if (std::find(out.begin(), out.end(), url) == out.end()) {
            out.push_back(std::move(url));
        }
        if (out.size() >= kPianoMidiMaxComposers) {
            break;
        }
    }
    return out;
}

void parse_piano_midi_composer(std::string_view html, std::string_view page,
                               std::vector<CatalogueEntry>& out) {
    const CatalogueInfo& info = catalogue_info(CatalogueId::PianoMidi);
    std::size_t at = 0;
    std::string composer;
    {
        std::size_t h1 = 0;
        const std::string_view head = between(html, "<h1 class=\"head\">", "</h1>", h1);
        composer = html_text(head);
    }
    // Each work is an <h2> and a table of its parts, a row each.
    std::string work;
    // The next heading is looked for again only once it is passed (a page of rows and no heading
    // left would otherwise scan to its end for each row).
    std::size_t h2 = html.find("<h2>", at);
    while (at < html.size()) {
        if (h2 != std::string_view::npos && h2 < at) {
            h2 = html.find("<h2>", at);
        }
        const std::size_t row = html.find("<tr class=\"midi\"><td", at);
        if (row == std::string_view::npos) {
            break;
        }
        if (h2 != std::string_view::npos && h2 < row) {
            std::size_t from = h2;
            work = html_text(between(html, "<h2>", "</h2>", from));
            at = from == std::string_view::npos ? html.size() : from;
            continue;
        }
        const std::size_t row_end = html.find("</tr>", row);
        const std::string_view block =
            html.substr(row, row_end == std::string_view::npos ? std::string_view::npos
                                                                : row_end - row);
        at = row_end == std::string_view::npos ? html.size() : row_end + 5;
        const std::vector<std::string_view> cells = cells_of(block);
        if (cells.empty()) {
            continue;
        }
        const std::string link = href_ending(cells[0], ".mid");
        if (link.empty() || link.find("format0") != std::string::npos) {
            continue;
        }
        CatalogueEntry e;
        e.source = CatalogueId::PianoMidi;
        e.url = resolve(page, link);
        if (!catalogue_file_allowed(e)) {
            continue;
        }
        e.id = link;
        const std::string part = html_text(cells[0]);
        // "Etudes, Opus 10 (1832)": the year is left for the detail line.
        std::string name = work;
        std::string year;
        if (const std::size_t paren = name.rfind(" ("); paren != std::string::npos &&
                                                         !name.empty() && name.back() == ')') {
            year = name.substr(paren + 2, name.size() - paren - 3);
            name.erase(paren);
        }
        e.title = name.empty() ? part : part.empty() || part == name ? name : name + ": " + part;
        e.composer = composer;
        e.detail = cells.size() > 2 ? html_text(cells[1]) : std::string();
        if (!year.empty()) {
            e.detail += e.detail.empty() ? year : ", " + year;
        }
        e.licence = "CC BY-SA 3.0 DE";
        e.licence_url = "https://creativecommons.org/licenses/by-sa/3.0/de/deed.en";
        e.credit = std::string(info.credit);
        e.page = std::string(page);
        if (!e.title.empty()) {
            out.push_back(std::move(e));
        }
    }
}


// ---- The cache

std::string write_catalogue_cache(CatalogueId id, std::int64_t fetched_utc_s,
                                  std::span<const CatalogueEntry> entries) {
    std::string out;
    out += kCacheMagic;
    out += '\t';
    out += std::to_string(kCacheVersion);
    out += '\t';
    out += catalogue_info(id).key;
    out += '\t';
    out += std::to_string(fetched_utc_s);
    out += '\n';
    for (const CatalogueEntry& e : entries) {
        const std::array<const std::string*, kCacheFields> fields{
            &e.id, &e.title, &e.composer, &e.detail, &e.licence, &e.licence_url,
            &e.credit, &e.url, &e.page};
        for (std::size_t f = 0; f < fields.size(); ++f) {
            if (f > 0) {
                out += '\t';
            }
            escape_field(out, *fields[f]);
        }
        out += '\n';
    }
    return out;
}

bool read_catalogue_cache(std::string_view text, CatalogueId id, std::int64_t& fetched_utc_s,
                          std::vector<CatalogueEntry>& out) {
    const std::size_t eol = text.find('\n');
    if (eol == std::string_view::npos) {
        return false;
    }
    const std::vector<std::string_view> head = split(text.substr(0, eol), '\t');
    if (head.size() != 4 || head[0] != kCacheMagic || head[1] != std::to_string(kCacheVersion) ||
        head[2] != catalogue_info(id).key) {
        return false;
    }
    std::int64_t when = 0;
    if (std::from_chars(head[3].data(), head[3].data() + head[3].size(), when).ec != std::errc()) {
        return false;
    }
    std::vector<CatalogueEntry> entries;
    std::size_t at = eol + 1;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> f = split(line, '\t');
        if (f.size() != kCacheFields) {
            return false;
        }
        CatalogueEntry e;
        e.source = id;
        e.id = unescape_field(f[0]);
        e.title = unescape_field(f[1]);
        e.composer = unescape_field(f[2]);
        e.detail = unescape_field(f[3]);
        e.licence = unescape_field(f[4]);
        e.licence_url = unescape_field(f[5]);
        e.credit = unescape_field(f[6]);
        e.url = unescape_field(f[7]);
        e.page = unescape_field(f[8]);
        if (catalogue_file_allowed(e)) {
            entries.push_back(std::move(e));
        }
    }
    fetched_utc_s = when;
    out = std::move(entries);
    return true;
}

bool catalogue_file_allowed(const CatalogueEntry& e) {
    const std::string_view files = catalogue_info(e.source).files;
    const std::string_view url = e.url;
    if (files.empty() || url.size() <= files.size() || url.rfind(files, 0) != 0 ||
        url.find_first_of("?#\\") != std::string_view::npos) {
        return false;
    }
    for (std::size_t at = url.find('%'); at != std::string_view::npos; at = url.find('%', at + 1)) {
        const char c = at + 2 < url.size() ? url[at + 2] : '\0';
        if (c != '\0' && url[at + 1] == '2' && (c == 'e' || c == 'E' || c == 'f' || c == 'F')) {
            return false;
        }
    }
    for (const std::string_view part : split(url.substr(files.size()), '/')) {
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
    }
    return true;
}

bool catalogue_stale(std::int64_t fetched_utc_s, std::int64_t now_utc_s) {
    return fetched_utc_s <= 0 || now_utc_s - fetched_utc_s >= kCatalogueRefreshS ||
           now_utc_s < fetched_utc_s;
}

CatalogueVersions group_catalogue_versions(std::span<const CatalogueEntry> entries) {
    const std::size_t n = entries.size();
    CatalogueVersions out;
    out.group_of.assign(n, CatalogueVersions::kNone);
    out.member_of.assign(n, 0);
    out.offsets.assign(1, 0);
    // Entries that say a composer and a number, in one bucket for each pair of them.
    std::unordered_map<std::string, std::vector<std::uint32_t>> buckets;
    std::vector<std::string> bucket_order;
    for (std::uint32_t i = 0; i < n; ++i) {
        const CatalogueEntry& e = entries[i];
        std::string number = catalogue_number(e.title);
        if (number.empty()) {
            number = catalogue_number(e.detail);
        }
        const std::string composer = composer_surname_key(e.composer);
        if (number.empty() || composer.empty() || piece_title_key(e.title).empty()) {
            continue;
        }
        const std::string key = composer + '|' + number;
        auto [it, fresh] = buckets.try_emplace(key);
        if (fresh) {
            bucket_order.push_back(key);
        }
        it->second.push_back(i);
    }
    struct Found {
        std::vector<std::uint32_t> entries; // chip order
        std::vector<std::string> names;
        std::string piece;
        std::uint32_t first = 0;
    };
    std::vector<Found> found;
    for (const std::string& key : bucket_order) {
        const std::vector<std::uint32_t>& ids = buckets[key];
        if (ids.size() < 2) {
            continue;
        }
        std::vector<VersionSong> songs;
        songs.reserve(ids.size());
        for (const std::uint32_t id : ids) {
            VersionSong s;
            s.title = entries[id].title;
            s.composer = entries[id].composer;
            songs.push_back(std::move(s));
        }
        for (const VersionGroup& g : group_versions(songs)) {
            Found f;
            std::unordered_map<std::string, int> used;
            // A version with no level word is the original; two of them in one group are told
            // apart by the catalogue each is from.
            const auto unlabelled = std::count_if(g.members.begin(), g.members.end(),
                                                  [](const VersionMember& m) { return !m.labelled; });
            for (const VersionMember& m : g.members) {
                const std::uint32_t id = ids[m.song];
                f.entries.push_back(id);
                std::string name = m.name;
                if (!m.labelled) {
                    name = "Original";
                    if (unlabelled > 1) {
                        name += " \xC2\xB7 " + std::string(catalogue_info(entries[id].source).group);
                    }
                }
                const int times = ++used[name];
                if (times > 1) {
                    name += " " + std::to_string(times);
                }
                f.names.push_back(std::move(name));
                std::string piece = piece_name(entries[id].title);
                if (f.piece.empty() || piece.size() < f.piece.size()) {
                    f.piece = std::move(piece);
                }
            }
            f.first = *std::min_element(f.entries.begin(), f.entries.end());
            found.push_back(std::move(f));
        }
    }
    // The groups in the order of their first entry.
    std::sort(found.begin(), found.end(),
              [](const Found& a, const Found& b) { return a.first < b.first; });
    for (const Found& f : found) {
        const auto g = static_cast<std::uint32_t>(out.pieces.size());
        for (std::size_t k = 0; k < f.entries.size(); ++k) {
            const std::uint32_t id = f.entries[k];
            out.group_of[id] = g;
            out.member_of[id] = static_cast<std::uint32_t>(k);
            out.members.push_back(id);
            out.names.push_back(f.names[k]);
        }
        out.offsets.push_back(static_cast<std::uint32_t>(out.members.size()));
        out.pieces.push_back(f.piece);
    }
    return out;
}

} // namespace etude
