// SPDX-License-Identifier: GPL-3.0-or-later
// A guard on the release rule (the touch rules): a control fires when the finger lifts inside
// the control the press started in, never when it lands. The controls get that from ImGui's button,
// which fires on the release over the item the press began on, and from `tapped` (app/ui/tap.hpp).
// This scans the source of the app for what would fire on the press instead: a button flagged to
// press on click, `IsItemClicked`, `IsMouseClicked` and `IsItemActivated` as a trigger. A use that
// is not a control firing (a scroll or a drag that begins at the touch, a dismissal, the right
// button, the press tracker itself) is on the list below, one exact line each with its reason, and
// each must still be there: the list shrinks with the code. A new use fails this test until it is
// either made a release or put on the list with a reason (a reviewer reads the reason).
#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// What fires on the press, or is how one is made.
constexpr std::string_view kPressTriggers[] = {
    "ImGuiButtonFlags_PressedOn", // PressedOnClick, PressedOnClickRelease, ...DoubleClick: a button
                                  // that is told when to fire
    "IsItemClicked(",
    "IsMouseClicked(",
    "IsMouseDoubleClicked(",
    "IsItemActivated(", // true on the frame a press lands on the item
    "ButtonBehavior(",  // the primitive that takes those flags
};

// The code of a line: a trimmed copy with a trailing `//` comment cut; empty for a comment line.
std::string code_of(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, 2, "//") == 0 || line[first] == '*') {
        return {};
    }
    std::string s = line.substr(first);
    const std::size_t comment = s.find("//");
    if (comment != std::string::npos) {
        s.resize(comment);
    }
    const std::size_t last = s.find_last_not_of(" \t\r");
    return last == std::string::npos ? std::string() : s.substr(0, last + 1);
}

bool fires_on_press(const std::string& line) {
    const std::string code = code_of(line);
    for (const std::string_view t : kPressTriggers) {
        if (code.find(t) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> sources_under(const std::filesystem::path& dir) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        const std::string ext = e.path().extension().string();
        if (!e.is_regular_file() || (ext != ".cpp" && ext != ".hpp")) {
            continue;
        }
        std::ifstream in(e.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        out.emplace_back(e.path().filename().string(), text.str());
    }
    return out;
}

struct Exempt {
    const char* file;
    const char* line; // the code of the line, trimmed
    int count;        // how many times it stands in the file
    const char* why;
};

// Each is a press that starts something that is not a control firing: a gesture that begins at the
// touch (and ends, or fires, on the lift), a dismissal, or the right button.
constexpr Exempt kExempt[] = {
    {"tap.cpp", "if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {", 1,
     "the tap tracker's own landing: it starts the press that `tapped` judges on the lift"},
    {"kit_actions.cpp", "const bool clicked = ImGui::IsMouseClicked(0);", 1,
     "drag_scroll: a touch on a scroll area starts a scroll gesture, no control fires"},
    {"sheet.cpp", "const bool clicked = ImGui::IsMouseClicked(0);", 1,
     "a press anywhere but on an armed button puts it back (confirm_arm.hpp): a disarm, nothing "
     "fires; the armed button's own second tap fires on the lift"},
    {"kit_toast.cpp", "if (ImGui::IsMouseClicked(0)) {", 1,
     "a press anywhere but on an (i) closes the open (i) bubble: a dismissal, nothing fires; on an (i) "
     "the release decides (info_toggle.hpp)"},
    {"host_bar_reveal.cpp", "if (!modal && (pinned || (ImGui::IsMouseClicked(0) && in_row) || hovering ||", 1,
     "a press on the top row shows the bar; the bar's buttons fire on their own lift"},
    {"learn_scale_card.cpp", "if (ImGui::IsMouseClicked(0) && hovered) {", 1,
     "the chip strip's scroll gesture begins at the touch; a chip fires on the lift (a drag cancels it)"},
    {"learn_melodies.cpp", "if (ImGui::IsMouseClicked(0) && hovered) {", 2,
     "the Melodies page's scroll gestures begin at the touch (down the page, sideways along a row); a tile fires on the lift (a drag cancels it)"},
    {"learn_drill_ui.cpp", "if (ImGui::IsMouseClicked(0) && !inside && !on_button) {", 1,
     "a press off the drill settings' card closes it: a dismissal, nothing fires"},
    {"learn_ear_card.cpp", "if (ImGui::IsMouseClicked(0) && !at(e.popover) && !at(e.settings)) {", 1,
     "a press off the ear card's Settings card closes it: a dismissal, nothing fires"},
    {"library_screen_places.cpp", "if (ImGui::IsMouseClicked(0) && hovered) {", 1,
     "a sideways row's scroll gesture begins at the touch; its chips fire on the lift"},
    {"tempo.cpp", "} else if (bpm_focus_ && ImGui::IsMouseClicked(0) && !field_hover &&", 1,
     "a press elsewhere takes the focus off the tempo field: a dismissal"},
    {"profile_confirm.cpp", "ImGui::IsMouseClicked(0) && *profile_delete_reason(can) != '\\0') {", 1,
     "a press on the disabled Delete says why it is off (a toast); nothing is deleted"},
    {"accompaniment.cpp",
     "const bool right = live && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);", 1,
     "the right button is a long press (the menu): no left press fires from it"},
    {"tap.cpp", "if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {", 1,
     "the right button is a long press (a card's or a menu's actions)"},
    {"editor_gestures.cpp", "if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {", 1,
     "the right button is a long press on the section editor's roll"},
    {"editor_gestures.cpp", "if (ImGui::IsItemActivated()) {", 1,
     "the roll's gesture recogniser takes the mouse as a finger: it judges tap, drag and hold on the lift"},
    {"section_band.cpp", "if (live && ImGui::IsItemActivated()) {", 1,
     "the section band's track starts a BandTouch: a tap loops on the lift, a drag moves a cut"},
    {"results_card.cpp", "if (ImGui::IsItemActivated()) {", 1,
     "the order list's grab handle starts a drag (reordering); nothing fires until it is dropped"},
    {"kit_sliders.cpp", "if (ImGui::IsItemActivated()) {", 1,
     "a slider's track takes the finger where it lands: a drag of a value, not a button"},
    {"tempo.cpp", "if (ImGui::IsItemActivated()) {", 1,
     "the tempo slider's track takes the finger where it lands: a drag of a value"},
    {"colour_widgets_picker.cpp", "if (ImGui::IsItemActivated()) {", 2,
     "the colour hue slider and the colour square take the finger where it lands: drags of a value"},
};

} // namespace

TEST_CASE("guard: the scanner finds a control that fires on the press, and only code") {
    CHECK(fires_on_press("    if (ImGui::IsItemClicked()) {"));
    CHECK(fires_on_press("    if (ImGui::IsMouseClicked(0) && hovered) {"));
    CHECK(fires_on_press("    if (ImGui::IsItemActivated()) { toggle(); }"));
    CHECK(fires_on_press("    ImGui::ButtonBehavior(bb, id, &hover, &held, flags);"));
    CHECK(fires_on_press("    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_PressedOnClick);"));
    CHECK(fires_on_press("    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_PressedOnClickRelease);"));
    // A comment, or text after one, is not code.
    CHECK_FALSE(fires_on_press("    // a press (IsMouseClicked) starts it"));
    CHECK_FALSE(fires_on_press("    foo(); // not IsItemClicked(...)"));
    CHECK_FALSE(fires_on_press("     * IsItemActivated( in a block comment"));
    // The release path every control uses.
    CHECK_FALSE(fires_on_press("    const bool pressed = ImGui::InvisibleButton(id, size);"));
    CHECK_FALSE(fires_on_press("    if (tapped(pressed)) {"));
    CHECK_FALSE(fires_on_press("    const bool held = ImGui::IsItemActive();"));
    CHECK_FALSE(fires_on_press("    if (ImGui::IsMouseReleased(0)) {"));
}

TEST_CASE("guard: no app code fires a control on the press; every exception is listed with its "
          "reason") {
    const std::filesystem::path root = std::filesystem::path(ETUDE_SOURCE_DIR) / "app";
    std::vector<int> used(sizeof(kExempt) / sizeof(kExempt[0]), 0);
    std::vector<std::string> unlisted;
    int found = 0;
    for (const auto& [name, text] : sources_under(root)) {
        std::istringstream lines(text);
        std::string line;
        int number = 0;
        while (std::getline(lines, line)) {
            ++number;
            if (!fires_on_press(line)) {
                continue;
            }
            ++found;
            const std::string code = code_of(line);
            bool listed = false;
            for (std::size_t e = 0; e < used.size(); ++e) {
                if (name == kExempt[e].file && code == kExempt[e].line) {
                    ++used[e];
                    listed = true;
                    break;
                }
            }
            if (!listed) {
                unlisted.push_back(name + ":" + std::to_string(number) + ": " + code);
            }
        }
    }
    CHECK(found > 0); // the scan reads the tree it is meant to (a wrong path finds nothing)
    for (const std::string& u : unlisted) {
        INFO("fires on the press (make it a release: ImGui's button + tapped(); or list it in "
             "kExempt with the reason it is no control): " << u);
        CHECK(false);
    }
    for (std::size_t e = 0; e < used.size(); ++e) {
        INFO("an exemption that no longer matches the code, or whose count changed (remove or "
             "update it): " << kExempt[e].file << ": " << kExempt[e].line);
        CHECK(used[e] == kExempt[e].count);
    }
}

// The kit measures a label the way it draws it: `label_at` and `text_size` both go through
// `screen_text` (F# is drawn F♯), and `label_width` measures with `text_size`, so an info button
// placed from it sits on the label's end when the label holds an accidental. ImGui is not linked
// into the tests, so the three are read from the source.
TEST_CASE("guard: label_width measures the text as label_at draws it, accidentals included") {
    const std::filesystem::path path =
        std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "ui" / "kit_text.cpp";
    std::ifstream in(path, std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();
    const std::string text = all.str();
    const auto body = [&](const std::string& head) {
        const std::size_t at = text.find(head);
        REQUIRE(at != std::string::npos);
        const std::size_t open = text.find('{', at);
        std::size_t depth = 0;
        for (std::size_t i = open; i < text.size(); ++i) {
            depth += text[i] == '{' ? 1 : 0;
            if (text[i] == '}' && --depth == 0) {
                return text.substr(open, i - open + 1);
            }
        }
        return std::string();
    };
    CHECK(body("ImVec2 text_size(ImFont* font, float size, const char* text)").find("screen_text(text)") !=
          std::string::npos);
    CHECK(body("float label_width(ImFont* font, float size, const char* text)").find("text_size(") !=
          std::string::npos);
    CHECK(body("void label_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,")
              .find("text = screen_text(text);") != std::string::npos);
}

// The rows of a list of choices (the radio rows: the mic list, the MIDI ports) fire by the kit's
// one tap rule, `tapped(pressed)`, never on the raw return of the button: a drag along a row or a
// second finger picks nothing.
TEST_CASE("guard: a radio row fires through tapped()") {
    const std::filesystem::path ui = std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "ui";
    const auto read = [&](const char* file) {
        std::ifstream in(ui / file, std::ios::binary);
        std::ostringstream all;
        all << in.rdbuf();
        return all.str();
    };
    const std::string kit = read("kit_controls.cpp");
    const std::size_t at = kit.find("bool radio_choice(Kit& k,");
    REQUIRE(at != std::string::npos);
    const std::size_t end = kit.find("\n}\n", at);
    REQUIRE(end != std::string::npos);
    const std::string body = kit.substr(at, end - at);
    CHECK(body.find("return tapped(pressed);") != std::string::npos);
    CHECK(body.find("return pressed") == std::string::npos);
}
