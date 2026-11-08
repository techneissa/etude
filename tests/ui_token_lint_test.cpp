// SPDX-License-Identifier: GPL-3.0-or-later
// A ratchet on the touch UI's sizes: a size passed to `k.s()` as a raw number instead of a design
// token (app/ui/tokens.hpp) is drift. The count of them in app/ui may go down, never up; when it
// goes down, lower `kCeiling` to the new count so it stays down.
//
// A second ratchet on where controls are drawn: a pressable made outside the kit (an ImGui button
// or an invisible button over a hand-drawn face), a label drawn outside it (`text_at` or ImGui's
// AddText, which place text by its line box and leave the audit blind to it), and a line of
// centring maths on a text's measured size (`(h - ts.y) * 0.5f`, the line box again) each count,
// and the count may only go down. Text in a control goes through `label_at` / `label_stack`
// (kit.hpp) and a text chip through `chip`, which size and centre themselves.
#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kCeiling = 155;
// Today's counts of hand-made controls and labels in app/ui outside kit.cpp (see the header); they
// were 48, 142 and 130 before the kit placed text by its ink. What is left: pressables over a face
// the caller drew (the library's round buttons and keys, the colour pickers, the bar's tempo chip
// and hand toggles), top-left text (headings, the details' lines, a bar's status) and the centring
// of a group of icon and words, or of a pill round a label. The centring count takes every name
// given a text's size (an ImVec2, or a float from its `.x` / `.y`, however assigned), so it is
// larger than when only declared ImVec2 names counted (14); 39 of the 52 lines are the kit's own.
constexpr int kPressableCeiling = 45;
constexpr int kLabelCeiling = 25;
constexpr int kCentringCeiling = 52;

bool is_ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// The index one past the `)` that closes the `(` at `open`, or the end of the text.
std::size_t after_matching_paren(const std::string& s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        depth += (s[i] == '(') - (s[i] == ')');
        if (depth == 0) {
            return i + 1;
        }
    }
    return s.size();
}

bool starts_k_s(const std::string& s, std::size_t i) {
    return s.compare(i, 4, "k.s(") == 0 && (i == 0 || !is_ident(s[i - 1]));
}

// Whether `arg` holds a numeric literal of its own: `12.0f`, `.5f`, `2` after an operator, `?` or
// `(`. A digit inside a name (`kS2`, `x1`) is not one. A nested `k.s(...)` is skipped: it counts
// on its own.
bool has_literal(const std::string& arg) {
    for (std::size_t i = 0; i < arg.size(); ++i) {
        if (starts_k_s(arg, i)) {
            i = after_matching_paren(arg, i + 3) - 1;
            continue;
        }
        const bool before_ok = i == 0 || !(is_ident(arg[i - 1]) || arg[i - 1] == '.');
        if (is_digit(arg[i]) && before_ok) {
            return true;
        }
        if (arg[i] == '.' && i + 1 < arg.size() && is_digit(arg[i + 1]) &&
            (i == 0 || !is_ident(arg[i - 1]))) {
            return true;
        }
    }
    return false;
}

// Calls of `k.s(...)` with a numeric literal anywhere in the argument list: `k.s(12.0f)`,
// `k.s(2.0f + 16.0f * t)`, `k.s(grabbed ? 2.4f : 1.6f)`. One per call.
int raw_sizes(const std::string& text) {
    int n = 0;
    for (std::size_t i = 0; i + 4 <= text.size(); ++i) {
        if (!starts_k_s(text, i)) {
            continue;
        }
        const std::size_t end = after_matching_paren(text, i + 3);
        if (has_literal(text.substr(i + 4, end - i - 5))) {
            ++n;
        }
    }
    return n;
}

// Calls of `name(` that are not part of a longer name, in `text`.
int calls_of(const std::string& text, const std::string& name) {
    int n = 0;
    for (std::size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1)) {
        const bool before_ok = at == 0 || !(is_ident(text[at - 1]) || text[at - 1] == '_');
        if (before_ok) {
            ++n;
        }
    }
    return n;
}

// Calls of `prefix` followed by more name letters and `(`: `ImGui::Button(` and `ImGui::ButtonEx(`
// for "ImGui::Button", `ImGui::Text(`, `ImGui::TextColored(` and `ImGui::TextUnformatted(` for
// "ImGui::Text".
int prefixed_calls_of(const std::string& text, const std::string& prefix) {
    int n = 0;
    for (std::size_t at = text.find(prefix); at != std::string::npos;
         at = text.find(prefix, at + 1)) {
        const bool before_ok = at == 0 || !(is_ident(text[at - 1]) || text[at - 1] == '_');
        std::size_t end = at + prefix.size();
        while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end])) != 0) {
            ++end;
        }
        if (before_ok && end < text.size() && text[end] == '(') {
            ++n;
        }
    }
    return n;
}

// Pressables made by hand: an ImGui button of any kind, a selectable or a checkbox, or an invisible
// button over a face drawn by the caller.
int hand_pressables(const std::string& text) {
    return calls_of(text, "InvisibleButton(") + prefixed_calls_of(text, "ImGui::Button") +
           calls_of(text, "ImGui::SmallButton(") + calls_of(text, "Selectable(") +
           calls_of(text, "Checkbox(");
}

// Labels drawn by hand: the kit's `text_at` (a top left pen position), ImGui's own AddText or one
// of its `Text` widgets.
int hand_labels(const std::string& text) {
    return calls_of(text, "text_at(") + calls_of(text, "AddText(") +
           prefixed_calls_of(text, "ImGui::Text");
}

// A name given a text's measured size (`text_size(` or ImGui's `CalcTextSize`), whatever its type
// and however the assignment is written: `const ImVec2 ts = text_size(...)`, `ts = ...`,
// `w += text_size(...).x`, `const float h = ok ? text_size(...).y : 0.0f`, or the `=` on the line
// above. A `vec` name is used as `name.x` / `name.y`; a float read from `.x` / `.y` is used bare.
struct SizeName {
    std::string name;
    bool vec = true;
};

std::vector<SizeName> text_size_names(const std::string& text) {
    std::vector<SizeName> names;
    for (const char* marker : {"text_size(", "CalcTextSize"}) {
        for (std::size_t at = text.find(marker); at != std::string::npos;
             at = text.find(marker, at + 1)) {
            if (at > 0 && (is_ident(text[at - 1]) || text[at - 1] == '.')) {
                continue; // `my_text_size(`, `x.text_size(`: not the kit's call
            }
            // The assignment this call is the value of: the nearest `=` before it with no
            // statement end between (and not part of `==`, `<=`, `>=` or `!=`).
            std::size_t eq = std::string::npos;
            for (std::size_t i = at; i-- > 0;) {
                const char c = text[i];
                if (c == ';' || c == '{' || c == '}') {
                    break;
                }
                if (c == '=') {
                    const char before = i > 0 ? text[i - 1] : ' ';
                    const char after = i + 1 < text.size() ? text[i + 1] : ' ';
                    if (before != '=' && before != '<' && before != '>' && before != '!' &&
                        after != '=') {
                        eq = i;
                        break;
                    }
                }
            }
            if (eq == std::string::npos) {
                continue;
            }
            std::size_t end = eq;
            if (end > 0 && std::strchr("+-*/", text[end - 1]) != nullptr) {
                --end; // `+=`
            }
            while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
                --end;
            }
            std::size_t begin = end;
            while (begin > 0 && is_ident(text[begin - 1])) {
                --begin;
            }
            if (begin == end) {
                continue;
            }
            const std::size_t open = text.find('(', at);
            const std::size_t close = open == std::string::npos ? open : after_matching_paren(text, open);
            const bool axis = close != std::string::npos && close + 1 < text.size() &&
                              text[close] == '.' && (text[close + 1] == 'x' || text[close + 1] == 'y');
            SizeName entry{text.substr(begin, end - begin), !axis};
            const bool known = std::any_of(names.begin(), names.end(), [&](const SizeName& n) {
                return n.name == entry.name && n.vec == entry.vec;
            });
            if (!known) {
                names.push_back(std::move(entry));
            }
        }
    }
    return names;
}

// Lines that centre by a text's measured size: one of those names' `.x` or `.y` with a 0.5f on the
// same line.
int centring_lines(const std::string& text) {
    const std::vector<SizeName> names = text_size_names(text);
    int n = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.find("0.5f") == std::string::npos) {
            continue;
        }
        for (const SizeName& entry : names) {
            bool hit = false;
            // A text's size is used as `name.x` / `name.y`; a float read from it, bare.
            const std::vector<std::string> uses =
                entry.vec ? std::vector<std::string>{entry.name + ".x", entry.name + ".y"}
                          : std::vector<std::string>{entry.name};
            for (const std::string& use : uses) {
                for (std::size_t p = line.find(use); p != std::string::npos;
                     p = line.find(use, p + 1)) {
                    const bool before_ok = p == 0 || !(is_ident(line[p - 1]) || line[p - 1] == '.');
                    const std::size_t after = p + use.size();
                    const bool after_ok = after >= line.size() || !(is_ident(line[after]) ||
                                                                 (!entry.vec && line[after] == '.'));
                    hit = hit || (before_ok && after_ok);
                }
            }
            if (hit) {
                ++n;
                break;
            }
        }
    }
    return n;
}

} // namespace

TEST_CASE("ui token lint: raw numbers passed to k.s() in app/ui never grow") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(ETUDE_SOURCE_DIR) / "app" / "ui";
    REQUIRE(fs::is_directory(dir));
    int total = 0;
    std::ostringstream per_file;
    for (const fs::directory_entry& e : fs::directory_iterator(dir)) {
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp") {
            continue;
        }
        std::ifstream in(e.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const int n = raw_sizes(text.str());
        if (n > 0) {
            per_file << e.path().filename().string() << " " << n << "\n";
        }
        total += n;
    }
    INFO("raw k.s() sizes per file:\n" << per_file.str());
    CHECK(total <= kCeiling);
}

TEST_CASE("ui token lint: what counts as a raw size") {
    CHECK(raw_sizes("k.s(12.0f)") == 1);
    CHECK(raw_sizes("k.s( 2.0f + 16.0f * t)") == 1);
    CHECK(raw_sizes("k.s(-3.0f)") == 1);
    CHECK(raw_sizes("k.s(.5f)") == 1);
    CHECK(raw_sizes("k.s(tok::kS2)") == 0);
    CHECK(raw_sizes("k.s(kCell)") == 0);
    CHECK(raw_sizes("kk.s(4.0f)") == 0);
    CHECK(raw_sizes("k.s(tok::kS2) + k.s(4.0f)") == 1);
    // A literal anywhere in the argument list counts, once per call.
    CHECK(raw_sizes("k.s(grabbed ? 2.4f : 1.6f)") == 1);
    CHECK(raw_sizes("k.s(cond ? 4 : 6)") == 1);
    CHECK(raw_sizes("k.s((snap_bar_ ? kBar : kBeat) + 12.0f * t)") == 1);
    CHECK(raw_sizes("k.s(a ? kBar : 3.0f)") == 1);
    CHECK(raw_sizes("k.s(kNoteIcon + (dotted ? kDot : 0.0f))") == 1);
    CHECK(raw_sizes("k.s(\n    a ? kA : 5.0f)") == 1);
    CHECK(raw_sizes("k.s(a ? kA : kB)") == 0);
    CHECK(raw_sizes("k.s(tok::kS2 * x2 + y.z1 + w_3)") == 0);
    CHECK(raw_sizes("k.s(f(1, kB))") == 1);
    // A nested call counts on its own, not twice.
    CHECK(raw_sizes("k.s(k.s(3.0f))") == 1);
    CHECK(raw_sizes("k.s(kA + k.s(tok::kS1))") == 0);
    CHECK(raw_sizes("-k.s(4.0f)") == 1);
}

TEST_CASE("ui lint: pressables and labels drawn outside the kit, and centring by a text's size, "
          "never grow") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(ETUDE_SOURCE_DIR) / "app" / "ui";
    REQUIRE(fs::is_directory(dir));
    int pressables = 0;
    int labels = 0;
    int centring = 0;
    std::ostringstream per_file;
    for (const fs::directory_entry& e : fs::directory_iterator(dir)) {
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp") {
            continue;
        }
        std::ifstream in(e.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const std::string body = text.str();
        // The centring maths is counted everywhere; the controls and labels only outside the kit.
        const int c = centring_lines(body);
        centring += c;
        int p = 0;
        int l = 0;
        const std::string file = e.path().filename().string();
        if (file != "kit.cpp" && !file.starts_with("kit_")) { // the kit is split across kit_*.cpp
            p = hand_pressables(body);
            l = hand_labels(body);
        }
        pressables += p;
        labels += l;
        if (p + l + c > 0) {
            per_file << e.path().filename().string() << " pressables " << p << " labels " << l
                     << " centring " << c << "\n";
        }
    }
    INFO("per file:\n" << per_file.str());
    CHECK(pressables <= kPressableCeiling);
    CHECK(labels <= kLabelCeiling);
    CHECK(centring <= kCentringCeiling);
    MESSAGE("pressables " << pressables << ", labels " << labels << ", centring " << centring);
}

TEST_CASE("ui lint: what counts as a hand-made control, label or centring") {
    CHECK(hand_pressables("ImGui::InvisibleButton(id, size); ImGui::Button(\"x\")") == 2);
    CHECK(hand_pressables("kitInvisibleButton(") == 0);
    CHECK(hand_pressables("ImGui::ButtonEx(a); ImGui::SmallButton(b); ImGui::Selectable(c); "
                          "ImGui::Checkbox(d, &e); Selectable(f); Checkbox(g, &h)") == 6);
    CHECK(hand_pressables("my_Checkbox(a); ImGui::Buttons_(b); ImGui::Button;") == 0);
    CHECK(hand_labels("text_at(k, dl, f, s, p, c, t); dl->AddText(f, s, p, c, t)") == 2);
    CHECK(hand_labels("label_at(k, dl, f, s, slot, a, c, t); my_text_at(") == 0);
    CHECK(hand_labels("ImGui::Text(a); ImGui::TextColored(b); ImGui::TextUnformatted(c)") == 3);
    CHECK(hand_labels("ImGui::TextWrapped; kit_Text(a)") == 0);
    const std::string code = "const ImVec2 ts = text_size(k.regular, size, shown);\n"
                             "const float y = pos.y + (h - ts.y) * 0.5f;\n"
                             "const float w = ts.x + pad;\n"         // no 0.5f
                             "const float x = (w - ts.x) * 0.5f;\n"  // counts
                             "const float z = (h - ots.y) * 0.5f;\n" // another name
                             "const float h2 = block.y * 0.5f;\n";   // not a text's size
    CHECK(centring_lines(code) == 2);
    // Any name given a text's size counts, however it is declared or assigned.
    const std::string forms =
        "float cw = 0.0f;\n"
        "cw += gap + text_size(k.regular, s, t).x;\n"
        "const float a = cw * 0.5f;\n" // counts: a float built from a text's width
        "ImVec2 box;\n"
        "box = k.regular->CalcTextSizeA(s, FLT_MAX, 0.0f, t);\n"
        "const float b = (h - box.y) * 0.5f;\n" // counts: assigned, not declared
        "const float top_h =\n"
        "    ok ? text_size(f, s, t).y : 0.0f;\n"
        "const float c = top_h * 0.5f;\n" // counts: the `=` is on the line above
        "const ImVec2 ls = ok ? text_size(f, s, t) : ImVec2{0.0f, 0.0f};\n"
        "const float d = ls.x * 0.5f;\n"   // counts: a ternary
        "const float e = other.x * 0.5f;\n"; // not a text's size
    CHECK(centring_lines(forms) == 4);
    // A comparison is not an assignment, and a float is not used as `.x`.
    CHECK(centring_lines("if (text_size(f, s, t).x <= room) { w = 1; }\nconst float a = w * 0.5f;\n") == 0);
    CHECK(centring_lines("const float ts = text_size(f, s, t).x;\nconst float a = ts.y * 0.5f;\n") == 0);
}
