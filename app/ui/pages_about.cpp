// SPDX-License-Identifier: GPL-3.0-or-later
// The About page: one row with the app's icon, its name and version and the links (GitHub and Buy Me
// a Coffee), the Updates in two boxes, and the row of icon colours.
// The Updates are the status box (one status line, what is new in an update that is out and its
// button, a failure's short line with its (!) for the whole reason) and the settings box (the daily
// check's switch, "Check now" and when the last check was); side by side in a wide window, else one
// under the other. The page only reads the AboutPort's view and calls its actions: the check runs on
// a thread of its own (app/update_service.hpp). Laid out by about_layout.hpp. On a machine with no
// browser a tap on a link opens a sheet with the address's QR code (pages_about_link.cpp).
// The icon colours (pages_about_icons.cpp) come under the Updates, not under the icon, so that on a
// small panel the update status stays in view.
#include "pages.hpp"

#include "about_layout.hpp"
#include "about_port.hpp"
#include "brand_marks.hpp"
#include "icon_audit.hpp"
#include "fitted_text.hpp"
#include "icons.hpp"
#include "kit_internal.hpp"

#include "../update_status.hpp"

#include "etude/update_feed.hpp"
#include "etude/version.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

constexpr const char* kGithubName = "techneissa";

// The same for a text cut into at most two lines, each fitted to the room.
struct Wrapped {
    std::string from;
    ImFont* font = nullptr;
    float size = 0.0f;
    float room = -1.0f;
    std::vector<std::string> lines;
};

const std::vector<std::string>& wrapped(Wrapped& slot, ImFont* font, float size,
                                        const std::string& text, float room) {
    if (slot.font != font || slot.size != size || slot.room != room || slot.from != text) {
        slot.font = font;
        slot.size = size;
        slot.room = room;
        slot.from = text;
        slot.lines = wrap_words(text, room, 2, [&](const std::string& line) {
            return label_width(font, size, line.c_str());
        });
        for (std::string& line : slot.lines) {
            line = fit(font, size, line, room);
        }
    }
    return slot.lines;
}

Box box_at(ImVec2 origin, const Rect& r) {
    return {origin.x + r.x, origin.y + r.y, origin.x + r.right(), origin.y + r.bottom()};
}

ImVec2 centre_of(const Box& b) { return {b.cx(), b.cy()}; }

const char* phase_icon(UpdatePhase p) {
    switch (p) {
    case UpdatePhase::UpToDate:
        return icon::kCircleCheck;
    case UpdatePhase::Available:
    case UpdatePhase::Downloading:
        return icon::kLoad;
    case UpdatePhase::Installing:
        return icon::kSettings;
    case UpdatePhase::Failed:
        return icon::kWarning;
    case UpdatePhase::Checking:
        return icon::kReset;
    case UpdatePhase::NotSetUp:
    case UpdatePhase::NotChecked:
        break;
    }
    return icon::kInfo;
}

ImVec4 phase_ink(UpdatePhase p) {
    switch (p) {
    case UpdatePhase::UpToDate:
    case UpdatePhase::Available:
    case UpdatePhase::Downloading:
    case UpdatePhase::Installing:
        return col::kAccent;
    case UpdatePhase::Failed:
        return col::kWarning;
    case UpdatePhase::NotSetUp:
    case UpdatePhase::NotChecked:
    case UpdatePhase::Checking:
        break;
    }
    return col::kText2;
}

// The first frame of a visit to the page: a link sheet left open on the last visit is closed. The
// very first visit keeps what a developer flag opened.
void on_page_visit(AboutPort& port) {
    static int last_frame = -10;
    static bool visited = false;
    const int frame = ImGui::GetFrameCount();
    if (frame - last_frame > 1) {
        if (visited) {
            port.set_link_sheet("");
        }
        visited = true;
    }
    last_frame = frame;
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void draw_header(Kit& k, ImVec2 origin, const AboutTop& g, const std::string& version,
                 etude::IconColour colour) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box icon_box = box_at(origin, g.icon);
    draw_app_icon(k, dl, colour, IconSlot::Header, centre_of(icon_box), icon_box.w());
    const Box name = box_at(origin, g.name);
    static Fitted name_slot;
    static Fitted version_slot;
    label_at(k, dl, k.bold, k.s(tok::kTitle), name, LabelAlign::Left, col::kText,
             measured(name_slot, k.bold, k.s(tok::kTitle), "Etude", name.w()).out.c_str());
    const Box words = box_at(origin, g.version);
    label_at(k, dl, k.regular, k.s(tok::kBody), words, LabelAlign::Left, col::kText2,
             measured(version_slot, k.regular, k.s(tok::kBody), version, words.w()).out.c_str());
}

// A link's pressable area with the face drawn under it; true on a tap.
bool link_area(Kit& k, const char* id, const Box& hit, const Box& face, State* state) {
    const bool pressed = press_area(k, id, hit);
    *state = item_state(k, false);
    draw_state(k, ImGui::GetWindowDrawList(), id, face, hit, *state, Idle::Fill,
               face.h() * 0.5f);
    return tapped(pressed);
}

// The row of two links. Returns the address tapped, or empty.
std::string draw_links(Kit& k, ImVec2 origin, const AboutTop& g) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::string tapped_url;
    const float face_h = k.s(tok::kFace);
    State st;

    const Box github = box_at(origin, g.github);
    const Box github_face{github.x0, github.cy() - face_h * 0.5f, github.x1,
                          github.cy() + face_h * 0.5f};
    if (link_area(k, "##about_github", github, github_face, &st)) {
        tapped_url = std::string(kRepositoryUrl);
    }
    audit::note("##about_github", {github.x0, github.y0}, {github.x1, github.y1},
                InkKind::Target);
    const ImVec4 ink = state_ink(st, col::kText);
    const float mark = k.s(about_tok::kMark);
    const float mark_x = github_face.x0 + k.s(tok::kS3) + mark * 0.5f;
    draw_brand(k, dl, Brand::GitHub, {mark_x, github_face.cy()}, mark, ink);
    const float text_x = mark_x + mark * 0.5f + k.s(tok::kS2);
    label_at(k, dl, k.regular, k.s(tok::kBody),
             {text_x, github_face.y0, github_face.x1 - k.s(tok::kS3), github_face.y1},
             LabelAlign::Left, ink, kGithubName);

    const Box coffee = box_at(origin, g.coffee);
    const Box coffee_face{coffee.cx() - face_h * 0.5f, coffee.cy() - face_h * 0.5f,
                          coffee.cx() + face_h * 0.5f, coffee.cy() + face_h * 0.5f};
    if (link_area(k, "##about_coffee", coffee, coffee_face, &st)) {
        tapped_url = std::string(kBuyMeACoffeeUrl);
    }
    audit::note("##about_coffee", {coffee_face.x0, coffee_face.y0},
                {coffee_face.x1, coffee_face.y1});
    draw_brand(k, dl, Brand::BuyMeACoffee, centre_of(coffee_face), k.s(about_tok::kCup), col::kText);
    return tapped_url;
}

// The page's strings. They change with the view or, for the time of the last check, with the clock
// each half minute, so they are built then and not in every frame.
struct AboutTexts {
    UpdateView seen;
    std::int64_t slot = -1;
    bool valid = false;
    std::string status;  // the whole line; for a failure the whole reason
    std::string summary; // its short form (the same unless a failure has one)
    std::string detail;
    std::string last;
};

bool same_view(const UpdateView& a, const UpdateView& b) {
    return a.phase == b.phase && a.percent == b.percent && a.installable == b.installable &&
           a.busy == b.busy && a.cancellable == b.cancellable &&
           a.last_checked_s == b.last_checked_s && a.version == b.version &&
           a.failure == b.failure && a.failure_summary == b.failure_summary && a.detail == b.detail;
}

const AboutTexts& about_texts(const UpdateView& v) {
    static AboutTexts t;
    const std::int64_t now = now_seconds();
    if (t.valid && t.slot == now / 30 && same_view(t.seen, v)) {
        return t;
    }
    t.seen = v;
    t.valid = true;
    t.slot = now / 30;
    t.status = update_status_text(v);
    if (v.phase == UpdatePhase::Downloading) {
        t.status += " · " + std::to_string(v.percent) + "%";
    }
    t.summary = v.phase == UpdatePhase::Failed && !v.failure_summary.empty() ? v.failure_summary
                                                                              : t.status;
    t.detail = v.detail;
    if (v.phase == UpdatePhase::Available && t.detail.empty() && !v.installable) {
        t.detail = "Update opens the release page";
    } else if (v.phase == UpdatePhase::Available && t.detail.empty() && v.busy) {
        t.detail = "Stop the song to update";
    }
    t.last = last_checked_text(now, v.last_checked_s);
    if (t.last.empty()) {
        t.last = "Never checked";
    }
    return t;
}

// "Update" as wide as its icon, its words and a side pad each.
float update_button_width(const Kit& k) {
    static Fitted word;
    return measured(word, k.bold, k.s(tok::kLabel), "Update", 1.0e6f).natural +
           k.s(tok::kIconSm) + k.s(tok::kS2) + 2.0f * k.s(tok::kS4);
}

// The status row. A failure whose whole reason does not fit the line (`with_info`) shows its short
// form, and the whole of it opens from the status icon, a button then, in the bubble.
void draw_status(Kit& k, AboutPort& port, const UpdateView& v, const AboutTexts& t, ImVec2 origin,
                 const AboutStatus& s, bool failed, bool downloading, bool with_info) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box icon_box = box_at(origin, s.icon);
    if (!with_info) {
        glyph(k, dl, phase_icon(v.phase), centre_of(icon_box), k.s(tok::kIcon), phase_ink(v.phase));
    }
    const Box text = box_at(origin, s.text);
    if (with_info) {
        // The short form on one line, or two where one is not enough.
        static Wrapped summary_slot;
        const float size = k.s(tok::kBody);
        const std::vector<std::string>& lines =
            wrapped(summary_slot, k.regular, size, t.summary, text.w());
        const float line_h = size * phone_tok::kLineRatio;
        const float top = text.cy() - line_h * 0.5f * static_cast<float>(lines.size());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const float y = top + line_h * static_cast<float>(i);
            label_at(k, dl, k.regular, size, {text.x0, y, text.x1, y + line_h}, LabelAlign::Left,
                     col::kText, lines[i].c_str());
        }
    } else {
        static Fitted status_slot;
        label_at(k, dl, k.regular, k.s(tok::kBody), text, LabelAlign::Left, col::kText,
                 measured(status_slot, k.regular, k.s(tok::kBody), t.status, text.w())
                     .out.c_str());
    }
    if (with_info) {
        info_button(k, t.status.c_str(), centre_of(icon_box), icon::kWarning, &col::kWarning,
                    k.s(tok::kIcon));
    }
    const bool cancellable = v.cancellable && !failed;
    if (failed || cancellable) {
        ChipOpts o;
        o.icon = failed ? icon::kReset : icon::kClose;
        o.width = s.action.w;
        o.fixed = true;
        const bool tapped_chip = chip(k, failed ? "##about_retry" : "##about_cancel",
                                      failed ? "Retry" : "Cancel",
                                      {origin.x + s.action.x, origin.y + s.action.y}, o);
        if (tapped_chip && failed) {
            v.retry_is_check ? port.check_now() : port.update_now();
        } else if (tapped_chip) {
            port.cancel();
        }
    }
    if (downloading) {
        const Box bar = box_at(origin, s.bar);
        dl->AddRectFilled({bar.x0, bar.y0}, {bar.x1, bar.y1}, u32(col::kFill), bar.h());
        const float fill = std::clamp(static_cast<float>(v.percent) / 100.0f, 0.0f, 1.0f);
        dl->AddRectFilled({bar.x0, bar.y0}, {bar.x0 + bar.w() * fill, bar.y1}, u32(col::kAccent),
                          bar.h());
    }
    if (!t.detail.empty()) {
        const Box line_box = box_at(origin, s.detail);
        static Fitted detail_slot;
        label_at(k, dl, k.regular, k.s(tok::kLabel), line_box, LabelAlign::Left, col::kText2,
                 measured(detail_slot, k.regular, k.s(tok::kLabel), t.detail, line_box.w())
                     .out.c_str());
    }
}

// What is new in the update that is out, and its button.
void draw_update(Kit& k, AboutPort& port, const UpdateView& v, ImVec2 origin,
                 const AboutUpdate& u, int lines) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    static Fitted note_slots[4];
    if (lines > 0) {
        label_at(k, dl, k.bold, k.s(tok::kCaption), box_at(origin, u.caption), LabelAlign::Left,
                 col::kText3, "What's new");
        for (int i = 0; i < lines; ++i) {
            Rect line = u.notes;
            line.y += static_cast<float>(i) * about_tok::kNotesLineH * k.u;
            line.h = about_tok::kNotesLineH * k.u;
            const Box box = box_at(origin, line);
            label_at(k, dl, k.regular, k.s(tok::kLabel), box, LabelAlign::Left, col::kText2,
                     measured(note_slots[i], k.regular, k.s(tok::kLabel),
                              v.notes[static_cast<std::size_t>(i)], box.w())
                         .out.c_str());
        }
    }
    const Box button = box_at(origin, u.button);
    // Only from the idle screen: while a song plays or a run is on, the button waits.
    const bool enabled = !(v.installable && v.busy);
    if (action_button(k, "##about_update", icon::kLoad, "Update", {button.x0, button.y0},
                      {button.w(), button.h()}, enabled, true)) {
        port.update_now();
    }
}

// "Check now" and, at its left, when the last check was: a row `w` wide at the cursor.
void check_row(Kit& k, AboutPort& port, const UpdateView& v, const AboutTexts& t, float w) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const AboutCheckRow g = about_check_row(w, k.u);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box text = box_at(origin, g.text);
    glyph(k, dl, icon::kClock, {origin.x + k.s(tok::kS3) + k.s(tok::kIcon) * 0.5f, text.cy()},
          k.s(tok::kIcon), col::kText3);
    static Fitted last_slot;
    label_at(k, dl, k.regular, k.s(tok::kLabel), text, LabelAlign::Left, col::kText2,
             measured(last_slot, k.regular, k.s(tok::kLabel), t.last, text.w()).out.c_str());
    ChipOpts o;
    o.icon = icon::kReset;
    o.width = g.button.w;
    o.fixed = true;
    o.live = can_check_now(v);
    if (chip(k, "##about_check", "Check now", {origin.x + g.button.x, origin.y + g.button.y}, o)) {
        port.check_now();
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({w, g.row.h});
}

// The settings box: a card with "Check automatically" and the check row in it, `w` wide.
void settings_box(Kit& k, UiModel& model, AboutPort& port, const UpdateView& v, const AboutTexts& t,
                  ImVec2 origin, float w, float height) {
    const AboutSettings g = about_settings(w, k.u);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    card(k, dl, origin, {origin.x + w, origin.y + height}, k.s(tok::kRadiusCard), col::kFill,
         ImDrawFlags_RoundCornersAll, 0.0f);
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##about_settings", {w, height}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    ImGui::SetCursorScreenPos({origin.x, origin.y + g.switch_row.y});
    catalogue_sweep::drawn(Setting::UpdateAutoCheck);
    bool automatic = model.app.update_auto_check;
    if (switch_row(k, icon::kHistory, "Check automatically", &automatic,
                   "Looks for a new version once a day. It never installs by itself.")) {
        model.app.update_auto_check = automatic;
        port.set_auto_check(automatic);
    }
    ImGui::Dummy({w, g.check_row.y - g.switch_row.bottom()});
    check_row(k, port, v, t, w);
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// The Updates: the status box (the status row and, for an update that is out, its notes and button)
// and the settings box, side by side where the page is wide, else one under the other.
void updates_boxes(Kit& k, UiModel& model, AboutPort& port, const UpdateView& v, const AboutTexts& t,
                   float w, bool with_settings) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool failed = v.phase == UpdatePhase::Failed;
    const bool downloading = v.phase == UpdatePhase::Downloading;
    const bool available = v.phase == UpdatePhase::Available;
    const bool with_action = failed || (v.cancellable && !failed);
    const AboutSettings settings = about_settings(w, k.u); // only its height matters below
    const float settings_h = with_settings ? settings.height : 0.0f;
    const AboutBoxes widths = about_boxes(w, k.u, 0.0f, settings_h);
    const float status_w = with_settings ? widths.status.w : w; // alone it has the page
    const bool wide = widths.side_by_side;
    // The reason goes behind its icon when it does not fit the line.
    const AboutStatus plain =
        about_status(status_w, k.u, with_action, downloading, !t.detail.empty());
    static Fitted reason_slot;
    const bool with_info =
        failed &&
        measured(reason_slot, k.regular, k.s(tok::kBody), t.status, 1.0e6f).natural > plain.text.w;
    const AboutStatus s = about_status(status_w, k.u, with_action, downloading, !t.detail.empty(),
                                       with_info);
    const int lines = available ? static_cast<int>(std::min<std::size_t>(v.notes.size(), 4)) : 0;
    const AboutUpdate up =
        about_update(status_w, k.u, s.height, lines, wide ? update_button_width(k) : 0.0f);
    const float status_h = available ? s.height + up.height : s.height + k.s(tok::kS1);
    AboutBoxes b = about_boxes(w, k.u, status_h, settings_h);
    if (!with_settings) {
        b.status = {0.0f, 0.0f, w, status_h};
        b.height = status_h;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 at{origin.x + b.status.x, origin.y + b.status.y};
    card(k, dl, at, {at.x + b.status.w, at.y + b.status.h}, k.s(tok::kRadiusCard), col::kFill,
         ImDrawFlags_RoundCornersAll, 0.0f);
    draw_status(k, port, v, t, at, s, failed, downloading, with_info);
    if (available) {
        draw_update(k, port, v, at, up, lines);
    }
    if (with_settings) {
        settings_box(k, model, port, v, t, {origin.x + b.settings.x, origin.y + b.settings.y},
                     b.settings.w, b.settings.h);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({w, b.height});
}

} // namespace

void page_about(Kit& k, UiModel& model, int columns) {
    (void)columns;
    static NullAboutPort none;
    AboutPort& port = k.about != nullptr ? *k.about : none;
    port.set_auto_check(model.app.update_auto_check);
    on_page_visit(port);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    if (!port.link_sheet().empty()) {
        const float body_h = ImGui::GetContentRegionAvail().y;
        draw_link_sheet(k, port, origin, w, body_h);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy({w, body_h});
        return;
    }
    static const std::string version = "Version " + std::string(app_version());
    static Fitted owner_slot;
    static Fitted title_slot;
    static Fitted words_slot;
    const float name_w =
        measured(owner_slot, k.regular, k.s(tok::kBody), kGithubName, 1.0e6f).natural;
    // A pixel or two over what the words measure, so fitting them to their box never cuts one.
    const float text_w =
        std::max(measured(title_slot, k.bold, k.s(tok::kTitle), "Etude", 1.0e6f).natural,
                 measured(words_slot, k.regular, k.s(tok::kBody), version, 1.0e6f).natural) +
        k.s(tok::kS1);
    const AboutTop top = about_top(w, k.u, text_w, name_w);
    draw_header(k, origin, top, version, model.app.icon_colour);
    const std::string url = draw_links(k, origin, top);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({w, top.height});
    if (!url.empty()) {
        port.open_link(url);
    }

    section(k, "Updates");
    const UpdateView& v = port.update();
    updates_boxes(k, model, port, v, about_texts(v), w, v.phase != UpdatePhase::NotSetUp);

    section(k, "Icon colour");
    about_icon_picker(k, model);
}

} // namespace etude::app::ui
